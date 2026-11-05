#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Result.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "env.h"
#include "params.h"
#include "queries.h"

namespace fs = std::filesystem;
using namespace drogon;

namespace {

Json::Value errorBody(const std::string &code, const std::string &message) {
    Json::Value body;
    body["error"]["code"] = code;
    body["error"]["message"] = message;
    return body;
}

HttpResponsePtr jsonResponse(const Json::Value &body, HttpStatusCode status = k200OK) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(status);
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    resp->setBody(Json::FastWriter().write(body));
    resp->addHeader("Cache-Control", "no-store");
    return resp;
}

void sendError(const std::function<void(const HttpResponsePtr &)> &cb, HttpStatusCode status,
               const std::string &code, const std::string &message) {
    cb(jsonResponse(errorBody(code, message), status));
}

// Тип колонки для клиента: берём из метаданных запроса, иначе — по виду значения (ADR: см. 1.2).
std::string columnType(const std::string &name,
                       const std::map<std::string, std::string> &declared,
                       const orm::Result &res) {
    const auto it = declared.find(name);
    if (it != declared.end()) return it->second;

    for (const auto &row : res) {
        const orm::Field f = row[name];
        if (f.isNull()) continue;
        const std::string v = f.as<std::string>();
        if (v.find_first_of(".eE") != std::string::npos) return "float";
        const bool digits = !v.empty() && std::all_of(v.begin(), v.end(), [](char c) {
                                return std::isdigit(static_cast<unsigned char>(c)) || c == '-';
                            });
        return digits ? "int" : "text";
    }
    return "text";
}

Json::Value fieldToJson(const orm::Field &f, const std::string &type) {
    if (f.isNull()) return Json::Value(Json::nullValue);
    const std::string v = f.as<std::string>();
    if (type == "int" || type == "float") {
        Json::Value num;
        Json::Reader reader;
        if (reader.parse(v, num) && num.isNumeric()) return num;
        return Json::Value(v);
    }
    return Json::Value(v);
}

struct TemplateInfo {
    std::string id;
    std::string title;
    std::string queryId;
    std::map<std::string, std::string> datasetParams;  // имя параметра запроса → "$param" шаблона
};

bool loadTemplate(const fs::path &file, TemplateInfo &out) {
    std::ifstream in(file);
    if (!in) return false;
    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    if (!Json::parseFromStream(b, in, &root, &errs)) return false;

    out.id = root.get("id", file.stem().string()).asString();
    out.title = root.get("title", out.id).asString();
    const Json::Value &datasets = root["datasets"];
    if (!datasets.isObject() || datasets.empty()) return true;  // шаблон без данных
    const auto first = datasets.begin();
    out.queryId = (*first).get("queryId", "").asString();
    const Json::Value &p = (*first)["params"];
    if (p.isObject()) {
        for (auto it = p.begin(); it != p.end(); ++it) out.datasetParams[it.name()] = it->asString();
    }
    return true;
}

void runQuery(const orm::DbClientPtr &db, const std::string &sql,
              const std::vector<std::string> &values,
              std::function<void(const orm::Result &)> onOk,
              std::function<void(const std::exception_ptr &)> onErr) {
    auto binder = *db << sql;
    for (const auto &v : values) binder << v;
    binder << orm::Mode::NonBlocking;
    binder >> std::move(onOk);
    binder >> std::move(onErr);
    binder.exec();
}

}  // namespace

int main() {
    rep::loadDotEnv(rep::envOr("REP_ENV_FILE", "backend/.env"));

    const std::string host = rep::envOr("REP_HOST", "0.0.0.0");
    const auto port = static_cast<uint16_t>(std::stoi(rep::envOr("REP_PORT", "8080")));
    const std::string queriesPath = rep::envOr("REP_QUERIES", "backend/config/queries.json");
    const std::string templatesDir = rep::envOr("REP_TEMPLATES", "backend/templates");
    const size_t maxRows = std::stoul(rep::envOr("REP_MAX_ROWS", "50000"));
    const size_t poolSize = std::stoul(rep::envOr("REP_DB_POOL", "8"));
    const unsigned threads = std::stoul(rep::envOr("REP_THREADS", "4"));

    // Реестр запросов валидируется до старта: с битым конфигом сервис не поднимается (ADR-4).
    rep::QueryRegistry registry;
    try {
        registry.load(queriesPath);
    } catch (const std::exception &e) {
        LOG_FATAL << "queries.json: " << e.what();
        return 1;
    }
    LOG_INFO << "loaded " << registry.size() << " queries from " << queriesPath;

    // Шаблоны читаются один раз на старте (dev: перезапуск сервиса).
    std::map<std::string, TemplateInfo> templates;
    if (fs::is_directory(templatesDir)) {
        for (const auto &entry : fs::directory_iterator(templatesDir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            TemplateInfo info;
            if (!loadTemplate(entry.path(), info)) {
                LOG_ERROR << "шаблон не разобран: " << entry.path();
                continue;
            }
            templates[info.id] = info;
        }
    }
    LOG_INFO << "loaded " << templates.size() << " templates from " << templatesDir;

    auto db = orm::DbClient::newPgClient(rep::connInfoFromEnv(), poolSize);

    app().registerHandler(
        "/api/health",
        [db, maxRows](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
            runQuery(
                db, "SELECT 1", {},
                [cb, maxRows](const orm::Result &res) {
                    Json::Value body;
                    body["status"] = "ok";
                    body["schemaVersion"] = 1;
                    body["db"] = !res.empty();
                    body["maxRows"] = static_cast<Json::UInt64>(maxRows);
                    cb(jsonResponse(body));
                },
                [cb](const std::exception_ptr &eptr) {
                    std::string msg = "database unavailable";
                    try {
                        if (eptr) std::rethrow_exception(eptr);
                    } catch (const std::exception &e) {
                        msg = e.what();
                    }
                    sendError(cb, k503ServiceUnavailable, "DB_UNAVAILABLE", msg);
                });
        },
        {Get});

    app().registerHandler(
        "/api/reports",
        [templates](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
            Json::Value list(Json::arrayValue);
            for (const auto &kv : templates) {
                Json::Value item;
                item["id"] = kv.second.id;
                item["title"] = kv.second.title;
                list.append(item);
            }
            cb(jsonResponse(list));
        },
        {Get});

    app().registerHandler(
        "/api/reports/{id}",
        [templates, templatesDir](const HttpRequestPtr &req,
                                  std::function<void(const HttpResponsePtr &)> &&cb,
                                  const std::string &id) {
            (void)req;
            std::ifstream in(fs::path(templatesDir) / (id + ".json"));
            if (!in) {
                sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + id + "' не найден");
                return;
            }
            std::stringstream ss;
            ss << in.rdbuf();
            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(ss.str());
            resp->addHeader("Cache-Control", "no-store");
            cb(resp);
        },
        {Get});

    app().registerHandler(
        "/api/report/data",
        [db, &registry, templates, maxRows](const HttpRequestPtr &req,
                                            std::function<void(const HttpResponsePtr &)> &&cb) {
            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                sendError(cb, k400BadRequest, "INVALID_PARAMS", "ожидался JSON-объект");
                return;
            }

            std::string queryId = (*body).get("queryId", "").asString();
            const std::string reportId = (*body).get("reportId", "").asString();
            const TemplateInfo *tpl = nullptr;

            if (!reportId.empty()) {
                const auto it = templates.find(reportId);
                if (it == templates.end()) {
                    sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + reportId + "' не найден");
                    return;
                }
                tpl = &it->second;
                if (queryId.empty()) queryId = tpl->queryId;
            }
            if (queryId.empty()) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS",
                          "нужен reportId с объявленным datasets или queryId");
                return;
            }

            const rep::CompiledQuery *query = registry.find(queryId);
            if (query == nullptr) {
                sendError(cb, k500InternalServerError, "QUERY_NOT_FOUND",
                          "запрос '" + queryId + "' не зарегистрирован");
                return;
            }

            rep::ParamBag bag;
            const Json::Value &rawParams = (*body)["params"];
            if (rawParams.isObject()) {
                for (auto it = rawParams.begin(); it != rawParams.end(); ++it) {
                    if (it->isString()) {
                        bag.set(it.name(), it->asString());
                    } else if (it->isInt64() || it->isUInt64() || it->isDouble() || it->isBool()) {
                        bag.set(it.name(), it->asString());
                    } else {
                        sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS",
                                  "параметр '" + it.name() + "' должен быть скаляром");
                        return;
                    }
                }
            }

            // Значения приходят по имени шаблона ("$from"), а уходят в запрос как значения параметра.
            if (tpl != nullptr) {
                rep::ParamBag mapped;
                for (const auto &kv : tpl->datasetParams) {
                    const std::string &src = kv.second;
                    const std::string name = (!src.empty() && src[0] == '$') ? src.substr(1) : src;
                    if (const auto *v = bag.find(name)) mapped.set(kv.first, *v);
                }
                for (const auto &kv : bag.values())
                    if (!mapped.has(kv.first)) mapped.set(kv.first, kv.second);
                bag = mapped;
            }

            rep::applyDefaults(query->binding, bag);
            const std::string err = rep::validateParams(query->binding, bag);
            if (!err.empty()) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", err);
                return;
            }

            std::vector<std::string> values;
            values.reserve(query->binding.size());
            for (const auto &d : query->binding) {
                const auto *v = bag.find(d.name);
                values.push_back(v != nullptr ? *v : std::string());
            }

            runQuery(
                db, query->sql, values,
                [cb, query, maxRows](const orm::Result &res) {
                    if (res.size() > maxRows) {
                        sendError(cb, k422UnprocessableEntity, "ROW_LIMIT",
                                  "результат " + std::to_string(res.size()) + " строк > лимита " +
                                      std::to_string(maxRows) + ": сузьте параметры");
                        return;
                    }

                    Json::Value columns(Json::arrayValue);
                    const auto ncols = static_cast<size_t>(res.columns());
                    std::vector<std::string> names;
                    std::vector<std::string> types;
                    names.reserve(ncols);
                    types.reserve(ncols);
                    for (size_t c = 0; c < ncols; ++c) {
                        names.emplace_back(res.columnName(static_cast<unsigned long>(c)));
                        types.push_back(columnType(names.back(), query->columns, res));
                        Json::Value col;
                        col["name"] = names.back();
                        col["type"] = types.back();
                        columns.append(col);
                    }

                    Json::Value rows(Json::arrayValue);
                    for (const auto &row : res) {
                        Json::Value obj(Json::objectValue);
                        for (size_t c = 0; c < ncols; ++c) obj[names[c]] = fieldToJson(row[c], types[c]);
                        rows.append(obj);
                    }

                    Json::Value out;
                    out["columns"] = columns;
                    out["rows"] = rows;
                    out["rowCount"] = static_cast<Json::UInt64>(res.size());
                    out["truncated"] = false;
                    cb(jsonResponse(out));
                },
                [cb](const std::exception_ptr &eptr) {
                    std::string msg = "query failed";
                    try {
                        if (eptr) std::rethrow_exception(eptr);
                    } catch (const std::exception &e) {
                        msg = e.what();
                    }
                    if (msg.find("statement timeout") != std::string::npos ||
                        msg.find("canceling statement") != std::string::npos) {
                        sendError(cb, k504GatewayTimeout, "SQL_TIMEOUT", msg);
                        return;
                    }
                    if (msg.find("Connection refused") != std::string::npos ||
                        msg.find("could not connect") != std::string::npos) {
                        sendError(cb, k503ServiceUnavailable, "DB_UNAVAILABLE", msg);
                        return;
                    }
                    sendError(cb, k500InternalServerError, "QUERY_FAILED", msg);
                });
        },
        {Post});

    LOG_INFO << "listening on " << host << ":" << port;
    app().addListener(host, port).setThreadNum(threads).run();
    return 0;
}

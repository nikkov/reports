#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Result.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <chrono>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <vector>

#include "cache.h"
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
        if (v.size() >= 10 && v[4] == '-' && v[7] == '-') return "date";
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
    std::string datasetSql;                             // SQL набора данных из шаблона (ADR-27)
    std::map<std::string, std::string> datasetParams;  // имя параметра запроса → "$param" шаблона
};


// Текст ошибки из std::exception_ptr (для ответов админки).
std::string describeError(const std::exception_ptr &e) {
    std::string msg = "ошибка выполнения";
    try {
        if (e) std::rethrow_exception(e);
    } catch (const std::exception &ex) {
        msg = ex.what();
    }
    return msg;
}

bool parseTemplate(const std::string &text, const std::string &fallbackId, TemplateInfo &out) {
    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream in(text);
    if (!Json::parseFromStream(b, in, &root, &errs)) return false;

    out = TemplateInfo{};
    out.id = root.get("id", fallbackId).asString();
    out.title = root.get("title", out.id).asString();
    const Json::Value &datasets = root["datasets"];
    if (!datasets.isObject() || datasets.empty()) return true;  // шаблон без данных
    const auto first = datasets.begin();
    // SQL набора данных живёт в шаблоне (ADR-27); queryId из реестра больше не поддерживается
    if ((*first).isMember("queryId")) return false;
    out.datasetSql = (*first).get("sql", "").asString();
    const Json::Value &p = (*first)["params"];
    if (p.isObject()) {
        for (auto it = p.begin(); it != p.end(); ++it) out.datasetParams[it.name()] = it->asString();
    }
    return true;
}

bool loadTemplateFile(const fs::path &file, TemplateInfo &out) {
    std::ifstream in(file);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    return parseTemplate(ss.str(), file.stem().string(), out);
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
    // Кэш результатов (ADR-32): TTL в секундах, 0 — выключен; второй лимит — число записей
    const int cacheTtl = std::stoi(rep::envOr("REP_CACHE_TTL_SECONDS", "60"));
    const size_t cacheMax = std::stoul(rep::envOr("REP_CACHE_MAX_ENTRIES", "200"));
    const size_t cacheMaxBody = std::stoul(rep::envOr("REP_CACHE_MAX_BODY_BYTES", "2097152"));
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

    // Клиент БД нужен и для данных, и для загрузки шаблонов (ADR-21).
    auto db = orm::DbClient::newPgClient(rep::connInfoFromEnv(), poolSize);

    const std::string templatesSource = rep::envOr("REP_TEMPLATES_SOURCE", "db");
    std::map<std::string, TemplateInfo> templates;
    std::map<std::string, std::string> templatesRaw;  // id → исходный JSON, отдаётся клиенту как есть

    if (templatesSource == "files") {
        if (fs::is_directory(templatesDir)) {
            for (const auto &entry : fs::directory_iterator(templatesDir)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
                TemplateInfo info;
                if (!loadTemplateFile(entry.path(), info)) {
                    LOG_ERROR << "шаблон не разобран: " << entry.path();
                    continue;
                }
                templates[info.id] = info;
                std::ifstream in(entry.path());
                std::stringstream ss;
                ss << in.rdbuf();
                templatesRaw[info.id] = ss.str();
            }
        }
        LOG_INFO << "loaded " << templates.size() << " templates from " << templatesDir;
    } else {
        try {
            auto rows = db->execSqlSync("SELECT id, title, body::text FROM report_templates ORDER BY id");
            for (const auto &row : rows) {
                const std::string id = row["id"].as<std::string>();
                const std::string body = row["body"].as<std::string>();
                TemplateInfo info;
                if (!parseTemplate(body, id, info)) {
                    LOG_ERROR << "шаблон в БД не разобран: " << id;
                    continue;
                }
                info.id = id;
                info.title = row["title"].as<std::string>();
                templates[id] = info;
                templatesRaw[id] = body;
            }
            LOG_INFO << "loaded " << templates.size() << " templates from database";
        } catch (const std::exception &e) {
            LOG_FATAL << "не удалось загрузить шаблоны из БД: " << e.what();
            return 1;
        }
    }

    // --- Администрирование отчётов (ADR-26) ---
    // Включается только REP_ADMIN=1; иначе маршруты записи отвечают 404.
    const bool adminEnabled = rep::envOr("REP_ADMIN", "0") == "1";
    const size_t maxTemplateBytes = std::stoul(rep::envOr("REP_MAX_TEMPLATE_BYTES", "262144"));
    std::mutex templatesMutex;
    // Версия шаблонов: входит в ключ кэша, поэтому публикация/удаление отчёта
    // автоматически «сбрасывают» устаревшие результаты.
    std::atomic<std::uint64_t> templatesVersion{1};
    rep::ResultCache resultCache(cacheTtl, cacheMax, cacheMaxBody);

    auto adminGuard = [adminEnabled](std::function<void(const HttpResponsePtr &)> &cb) {
        if (adminEnabled) return true;
        sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена: запустите сервис с REP_ADMIN=1");
        return false;
    };

    // Разрешён только чтение: админка не должна давать исполнять произвольный SQL под ролью postgres (5.9).
    auto sqlGuard = [](const std::string &sql, std::string &err) {
        std::string s;
        for (char c : sql) {
            if (!std::isspace(static_cast<unsigned char>(c))) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (s.rfind("select", 0) != 0 && s.rfind("with", 0) != 0) {
            err = "SQL справочника должен начинаться с SELECT или WITH";
            return false;
        }
        if (sql.find(';') != std::string::npos || sql.find("--") != std::string::npos ||
            sql.find("/*") != std::string::npos) {
            err = "SQL не может содержать ';', '--' или '/*'";
            return false;
        }
        static const std::vector<std::string> forbidden = {
            "insert", "update", "delete", "drop", "alter", "create", "truncate",
            "grant", "revoke", "copy", "merge", "call", "do", "vacuum", "reindex"};
        for (const auto &word : forbidden) {
            size_t pos = 0;
            while ((pos = s.find(word, pos)) != std::string::npos) {
                const bool leftOk = pos == 0 || !std::isalnum(static_cast<unsigned char>(s[pos - 1]));
                const bool rightOk = pos + word.size() >= s.size() ||
                                     !std::isalnum(static_cast<unsigned char>(s[pos + word.size()]));
                if (leftOk && rightOk) {
                    err = "SQL содержит запрещённую операцию «" + word + "»";
                    return false;
                }
                pos += word.size();
            }
        }
        return true;
    };

    // Структурная проверка шаблона на сервере (полная семантика — на клиенте, см. ADR-26).
    auto validateTemplate = [maxTemplateBytes, &sqlGuard](const std::string &raw, const std::string &urlId,
                                                          std::string &err) {
        if (raw.size() > maxTemplateBytes) {
            err = "шаблон больше " + std::to_string(maxTemplateBytes) + " байт";
            return false;
        }
        Json::CharReaderBuilder rb;
        Json::Value root;
        std::string parseErr;
        std::istringstream in(raw);
        if (!Json::parseFromStream(rb, in, &root, &parseErr) || !root.isObject()) {
            err = "шаблон не является JSON-объектом: " + parseErr;
            return false;
        }
        if (root.get("version", Json::Value(0)).asInt() != 1) {
            err = "version должен быть 1";
            return false;
        }
        if (!urlId.empty() && root.get("id", Json::Value("")).asString() != urlId) {
            err = "id в шаблоне не совпадает с URL";
            return false;
        }
        if (root.get("title", Json::Value("")).asString().empty()) {
            err = "title обязателен";
            return false;
        }
        if (!root["blocks"].isArray() || root["blocks"].empty()) {
            err = "blocks должен быть непустым массивом";
            return false;
        }
        if (!root["params"].isNull() && !root["params"].isArray()) {
            err = "params должен быть массивом";
            return false;
        }
        if (!root["datasets"].isNull() && !root["datasets"].isObject()) {
            err = "datasets должен быть объектом";
            return false;
        }
        for (const auto &ds : root["datasets"]) {
            if (ds.isMember("queryId")) {
                err = "queryId больше не поддерживается: SQL набора данных задаётся в datasets.<key>.sql";
                return false;
            }
            if (ds.get("sql", Json::Value("")).asString().empty()) {
                err = "у набора данных нет sql";
                return false;
            }
        }
        for (const auto &param : root["params"]) {
            const Json::Value &source = param["source"];
            if (!source.isObject() || source.get("kind", Json::Value("")).asString() != "lookup") continue;
            if (source.isMember("queryId")) {
                err = "queryId больше не поддерживается: SQL справочника задаётся в source.sql";
                return false;
            }
            const std::string sql = source.get("sql", Json::Value("")).asString();
            if (sql.empty()) {
                err = "у справочника " + param.get("name", Json::Value("")).asString() + " нет source.sql";
                return false;
            }
            std::string sqlErr;
            if (!sqlGuard(sql, sqlErr)) {
                err = sqlErr;
                return false;
            }
        }
        return true;
    };

    // Атомарная замена шаблона в памяти: сначала разбор, потом запись под мьютексом.
    auto applyTemplateInMemory = [&templates, &templatesRaw, &templatesMutex, &templatesVersion,
                                  &resultCache](const std::string &id, const std::string &raw,
                                                const std::string &title) {
        TemplateInfo info;
        if (!parseTemplate(raw, id, info)) return false;
        info.id = id;
        info.title = title;
        {
            std::lock_guard<std::mutex> lock(templatesMutex);
            templates[id] = info;
            templatesRaw[id] = raw;
        }
        // новый шаблон — новые данные: старые результаты больше не валидны
        templatesVersion.fetch_add(1);
        resultCache.clear();
        return true;
    };

    auto dropTemplateFromMemory = [&templates, &templatesRaw, &templatesMutex, &templatesVersion,
                                   &resultCache](const std::string &id) {
        {
            std::lock_guard<std::mutex> lock(templatesMutex);
            templates.erase(id);
            templatesRaw.erase(id);
        }
        templatesVersion.fetch_add(1);
        resultCache.clear();
    };

    // Сырой шаблон для редактора: с SQL справочников (публичный /api/reports/{id} его вырезает).
    app().registerHandler(
        "/api/admin/reports/{id}",
        [adminEnabled, &templatesRaw, &templatesMutex](const HttpRequestPtr &,
                                                       std::function<void(const HttpResponsePtr &)> &&cb,
                                                       const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            std::string raw;
            {
                std::lock_guard<std::mutex> lock(templatesMutex);
                const auto it = templatesRaw.find(id);
                if (it == templatesRaw.end()) {
                    sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + id + "' не найден");
                    return;
                }
                raw = it->second;
            }
            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(raw);
            resp->addHeader("Cache-Control", "no-store");
            cb(resp);
        },
        {Get});

    app().registerHandler(
        "/api/admin/reports",
        [db, adminEnabled](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена: запустите сервис с REP_ADMIN=1");
                return;
            }
            runQuery(
                db,
                "SELECT id, title, char_length(body::text) AS size, to_char(updated_at, 'YYYY-MM-DD HH24:MI') AS updated, "
                "(draft IS NOT NULL) AS has_draft, to_char(draft_updated_at, 'YYYY-MM-DD HH24:MI') AS draft_at "
                "FROM report_templates ORDER BY id",
                {},
                [cb](const orm::Result &res) {
                    Json::Value list(Json::arrayValue);
                    for (const auto &row : res) {
                        Json::Value item;
                        item["id"] = row["id"].as<std::string>();
                        item["title"] = row["title"].as<std::string>();
                        item["size"] = static_cast<Json::UInt64>(row["size"].as<long long>());
                        item["updated"] = row["updated"].as<std::string>();
                        item["hasDraft"] = row["has_draft"].as<bool>();
                        if (!row["draft_at"].isNull()) item["draftAt"] = row["draft_at"].as<std::string>();
                        list.append(item);
                    }
                    cb(jsonResponse(list));
                },
                [cb](const std::exception_ptr &e) {
                    sendError(cb, k500InternalServerError, "ADMIN_LIST_FAILED", describeError(e));
                });
        },
        {Get});

    app().registerHandler(
        "/api/reports",
        [db, adminEnabled, maxTemplateBytes, &validateTemplate, &applyTemplateInMemory](
            const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена: запустите сервис с REP_ADMIN=1");
                return;
            }
            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                sendError(cb, k400BadRequest, "INVALID_PARAMS", "ожидался JSON-объект");
                return;
            }
            const std::string id = body->get("id", "").asString();
            if (id.empty() || !std::isalpha(static_cast<unsigned char>(id[0]))) {
                sendError(cb, k422UnprocessableEntity, "INVALID_ID", "id задаётся латиницей: ^[a-z][a-z0-9-]*$");
                return;
            }
            for (char c : id) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') {
                    sendError(cb, k422UnprocessableEntity, "INVALID_ID", "id задаётся латиницей: ^[a-z][a-z0-9-]*$");
                    return;
                }
            }
            Json::Value tpl = (*body)["body"].isObject() ? (*body)["body"] : Json::Value(Json::objectValue);
            tpl["id"] = id; // id всегда из URL: «сохранить как» меняет только адрес
            if (!tpl.isMember("title") && body->isMember("title")) tpl["title"] = (*body)["title"];
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "";
            const std::string raw = Json::writeString(wb, tpl);
            std::string err;
            if (!validateTemplate(raw, id, err)) {
                sendError(cb, k422UnprocessableEntity, "TEMPLATE_INVALID", err);
                return;
            }
            const std::string title = tpl.get("title", id).asString();

            runQuery(
                db, "SELECT 1 FROM report_templates WHERE id = $1", {id},
                [cb, db, id, raw, title, &applyTemplateInMemory](const orm::Result &res) {
                    if (!res.empty()) {
                        sendError(cb, k409Conflict, "REPORT_EXISTS", "отчёт '" + id + "' уже существует");
                        return;
                    }
                    runQuery(
                        db, "INSERT INTO report_templates (id, title, body, updated_at) VALUES ($1, $2, $3::jsonb, now())",
                        {id, title, raw},
                        [cb, id, raw, title, &applyTemplateInMemory](const orm::Result &) {
                            if (!applyTemplateInMemory(id, raw, title)) {
                                sendError(cb, k422UnprocessableEntity, "TEMPLATE_INVALID", "шаблон не разбирается");
                                return;
                            }
                            Json::Value out;
                            out["id"] = id;
                            out["saved"] = true;
                            cb(jsonResponse(out));
                        },
                        [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "SAVE_FAILED", describeError(e)); });
                },
                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "SAVE_FAILED", describeError(e)); });
        },
        {Post});

    app().registerHandler(
        "/api/reports/{id}",
        [db, adminEnabled, &validateTemplate, &applyTemplateInMemory](const HttpRequestPtr &req,
                                                                      std::function<void(const HttpResponsePtr &)> &&cb,
                                                                      const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                sendError(cb, k400BadRequest, "INVALID_PARAMS", "ожидался JSON-объект");
                return;
            }
            Json::Value tpl = (*body)["body"].isObject() ? (*body)["body"] : *body;
            tpl["id"] = id;
            if (!tpl.isMember("title") && body->isMember("title")) tpl["title"] = (*body)["title"];
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "";
            const std::string raw = Json::writeString(wb, tpl);
            std::string err;
            if (!validateTemplate(raw, id, err)) {
                sendError(cb, k422UnprocessableEntity, "TEMPLATE_INVALID", err);
                return;
            }
            const std::string title = tpl.get("title", id).asString();

            const std::string tag = "$h" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "$";
            const std::string history =
                "INSERT INTO report_templates_history (report_id, title, body) "
                "SELECT id, title, body FROM report_templates WHERE id = " + tag + id + tag + ";";
            (void)history;

            // 1) снапшот старой версии, 2) обновление, 3) правка памяти
            runQuery(
                db,
                "INSERT INTO report_templates_history (report_id, title, body) "
                "SELECT id, title, body FROM report_templates WHERE id = $1",
                {id},
                [cb, db, id, raw, title, &applyTemplateInMemory](const orm::Result &) {
                    runQuery(
                        db, "UPDATE report_templates SET title = $2, body = $3::jsonb, updated_at = now() WHERE id = $1",
                        {id, title, raw},
                        [cb, id, raw, title, &applyTemplateInMemory](const orm::Result &updated) {
                            if (updated.affectedRows() == 0) {
                                sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + id + "' не найден");
                                return;
                            }
                            if (!applyTemplateInMemory(id, raw, title)) {
                                sendError(cb, k422UnprocessableEntity, "TEMPLATE_INVALID", "шаблон не разбирается");
                                return;
                            }
                            Json::Value out;
                            out["id"] = id;
                            out["saved"] = true;
                            cb(jsonResponse(out));
                        },
                        [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "SAVE_FAILED", describeError(e)); });
                },
                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "SAVE_FAILED", describeError(e)); });
        },
        {Put});

    app().registerHandler(
        "/api/reports/{id}",
        [db, adminEnabled, &dropTemplateFromMemory](const HttpRequestPtr &req,
                                                    std::function<void(const HttpResponsePtr &)> &&cb,
                                                    const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            runQuery(
                db,
                "INSERT INTO report_templates_history (report_id, title, body) "
                "SELECT id, title, body FROM report_templates WHERE id = $1",
                {id},
                [cb, db, id, &dropTemplateFromMemory](const orm::Result &) {
                    runQuery(
                        db, "DELETE FROM report_templates WHERE id = $1", {id},
                        [cb, id, &dropTemplateFromMemory](const orm::Result &del) {
                            dropTemplateFromMemory(id);
                            Json::Value out;
                            out["id"] = id;
                            out["deleted"] = del.affectedRows() > 0;
                            cb(jsonResponse(out));
                        },
                        [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "DELETE_FAILED", describeError(e)); });
                },
                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "DELETE_FAILED", describeError(e)); });
        },
        {Delete});

    app().registerHandler(
        "/api/reports/{id}/validate",
        [adminEnabled, &validateTemplate](const HttpRequestPtr &req,
                                          std::function<void(const HttpResponsePtr &)> &&cb,
                                          const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                sendError(cb, k400BadRequest, "INVALID_PARAMS", "ожидался JSON-объект");
                return;
            }
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "";
            const std::string raw = Json::writeString(wb, (*body)["body"] ? (*body)["body"] : *body);
            std::string err;
            Json::Value out;
            out["ok"] = validateTemplate(raw, id, err);
            if (!out["ok"].asBool()) out["error"] = err;
            cb(jsonResponse(out));
        },
        {Post});

    app().registerHandler(
        "/api/admin/reports/{id}/history",
        [db, adminEnabled](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb,
                           const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            runQuery(
                db,
                "SELECT id, title, char_length(body::text) AS size, to_char(saved_at, 'YYYY-MM-DD HH24:MI:SS') AS saved "
                "FROM report_templates_history WHERE report_id = $1 ORDER BY id DESC LIMIT 20",
                {id},
                [cb](const orm::Result &res) {
                    Json::Value list(Json::arrayValue);
                    for (const auto &row : res) {
                        Json::Value item;
                        item["id"] = static_cast<Json::UInt64>(row["id"].as<long long>());
                        item["title"] = row["title"].as<std::string>();
                        item["size"] = static_cast<Json::UInt64>(row["size"].as<long long>());
                        item["saved"] = row["saved"].as<std::string>();
                        list.append(item);
                    }
                    cb(jsonResponse(list));
                },
                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "HISTORY_FAILED", describeError(e)); });
        },
        {Get});

    app().registerHandler(
        "/api/admin/reports/{id}/rollback/{snapshot}",
        [db, adminEnabled, &applyTemplateInMemory](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb,
                                                   const std::string &id, const std::string &snapshot) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            runQuery(
                db, "SELECT title, body::text AS body FROM report_templates_history WHERE id = $1 AND report_id = $2",
                {snapshot, id},
                [cb, db, id, &applyTemplateInMemory](const orm::Result &res) {
                    if (res.empty()) {
                        sendError(cb, k404NotFound, "SNAPSHOT_NOT_FOUND", "версия не найдена");
                        return;
                    }
                    const std::string title = res[0]["title"].as<std::string>();
                    const std::string raw = res[0]["body"].as<std::string>();
                    runQuery(
                        db,
                        "INSERT INTO report_templates_history (report_id, title, body) "
                        "SELECT id, title, body FROM report_templates WHERE id = $1",
                        {id},
                        [cb, db, id, raw, title, &applyTemplateInMemory](const orm::Result &) {
                            runQuery(
                                db, "UPDATE report_templates SET title = $2, body = $3::jsonb, updated_at = now() WHERE id = $1",
                                {id, title, raw},
                                [cb, id, raw, title, &applyTemplateInMemory](const orm::Result &) {
                                    applyTemplateInMemory(id, raw, title);
                                    Json::Value out;
                                    out["id"] = id;
                                    out["restored"] = true;
                                    cb(jsonResponse(out));
                                },
                                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "ROLLBACK_FAILED", describeError(e)); });
                        },
                        [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "ROLLBACK_FAILED", describeError(e)); });
                },
                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "ROLLBACK_FAILED", describeError(e)); });
        },
        {Post});

    // --- Черновик и публикация (ADR-31) -------------------------------------
    // Правка в админке больше не попадает в отчёт сразу: она сохраняется в draft,
    // публичный /api/reports/{id} продолжает отдавать опубликованное тело, пока не вызван publish.

    // GET /api/admin/reports/{id}/draft — тело черновика для редактора
    app().registerHandler(
        "/api/admin/reports/{id}/draft",
        [db, adminEnabled](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb,
                           const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            runQuery(
                db,
                "SELECT draft::text AS draft, to_char(draft_updated_at, 'YYYY-MM-DD HH24:MI') AS draft_at "
                "FROM report_templates WHERE id = $1 AND draft IS NOT NULL",
                {id},
                [cb](const orm::Result &res) {
                    if (res.empty()) {
                        sendError(cb, k404NotFound, "DRAFT_NOT_FOUND", "черновика нет");
                        return;
                    }
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(res[0]["draft"].as<std::string>());
                    resp->addHeader("Cache-Control", "no-store");
                    if (!res[0]["draft_at"].isNull()) resp->addHeader("X-Draft-Updated", res[0]["draft_at"].as<std::string>());
                    cb(resp);
                },
                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "DRAFT_FAILED", describeError(e)); });
        },
        {Get});

    // PUT /api/admin/reports/{id}/draft — сохранить черновик (валидируется, в память не идёт)
    app().registerHandler(
        "/api/admin/reports/{id}/draft",
        [db, adminEnabled, &validateTemplate](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb,
                           const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                sendError(cb, k400BadRequest, "INVALID_PARAMS", "ожидался JSON-объект");
                return;
            }
            Json::Value tpl = (*body)["body"].isObject() ? (*body)["body"] : *body;
            tpl["id"] = id;
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "";
            const std::string raw = Json::writeString(wb, tpl);
            std::string err;
            if (!validateTemplate(raw, id, err)) {
                sendError(cb, k422UnprocessableEntity, "TEMPLATE_INVALID", err);
                return;
            }
            runQuery(
                db, "UPDATE report_templates SET draft = $2::jsonb, draft_updated_at = now() WHERE id = $1",
                {id, raw},
                [cb, id](const orm::Result &updated) {
                    if (updated.affectedRows() == 0) {
                        sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + id + "' не найден");
                        return;
                    }
                    Json::Value out;
                    out["id"] = id;
                    out["draft"] = true;
                    cb(jsonResponse(out));
                },
                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "DRAFT_FAILED", describeError(e)); });
        },
        {Put});

    // DELETE /api/admin/reports/{id}/draft — отменить черновик
    app().registerHandler(
        "/api/admin/reports/{id}/draft",
        [db, adminEnabled](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb,
                           const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            runQuery(
                db, "UPDATE report_templates SET draft = NULL, draft_updated_at = NULL WHERE id = $1", {id},
                [cb, id](const orm::Result &) {
                    Json::Value out;
                    out["id"] = id;
                    out["discarded"] = true;
                    cb(jsonResponse(out));
                },
                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "DRAFT_FAILED", describeError(e)); });
        },
        {Delete});

    // POST /api/admin/reports/{id}/publish — опубликовать черновик: снапшот прежней версии,
    // тело = draft, draft очищается, память обновляется (hot-reload без рестарта)
    app().registerHandler(
        "/api/admin/reports/{id}/publish",
        [db, adminEnabled, &applyTemplateInMemory, &validateTemplate](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb,
                                                   const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            runQuery(
                db, "SELECT draft::text AS draft FROM report_templates WHERE id = $1", {id},
                [cb, db, id, &applyTemplateInMemory, &validateTemplate](const orm::Result &res) {
                    if (res.empty()) {
                        sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + id + "' не найден");
                        return;
                    }
                    if (res[0]["draft"].isNull()) {
                        sendError(cb, k409Conflict, "NO_DRAFT", "у отчёта нет черновика");
                        return;
                    }
                    const std::string raw = res[0]["draft"].as<std::string>();
                    std::string err;
                    if (!validateTemplate(raw, id, err)) {
                        sendError(cb, k422UnprocessableEntity, "TEMPLATE_INVALID", err);
                        return;
                    }
                    Json::CharReaderBuilder builder;
                    Json::Value parsed;
                    std::string errs;
                    std::istringstream in(raw);
                    Json::parseFromStream(builder, in, &parsed, &errs);
                    const std::string title = parsed.get("title", id).asString();
                    runQuery(
                        db,
                        "INSERT INTO report_templates_history (report_id, title, body) "
                        "SELECT id, title, body FROM report_templates WHERE id = $1",
                        {id},
                        [cb, db, id, raw, title, &applyTemplateInMemory](const orm::Result &) {
                            runQuery(
                                db,
                                "UPDATE report_templates SET title = $2, body = $3::jsonb, updated_at = now(), "
                                "draft = NULL, draft_updated_at = NULL WHERE id = $1",
                                {id, title, raw},
                                [cb, id, raw, title, &applyTemplateInMemory](const orm::Result &) {
                                    applyTemplateInMemory(id, raw, title);
                                    Json::Value out;
                                    out["id"] = id;
                                    out["published"] = true;
                                    cb(jsonResponse(out));
                                },
                                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "PUBLISH_FAILED", describeError(e)); });
                        },
                        [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "PUBLISH_FAILED", describeError(e)); });
                },
                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "PUBLISH_FAILED", describeError(e)); });
        },
        {Post});

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
        [&templatesRaw, &templatesMutex](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
            std::map<std::string, std::string> snapshot;
            {
                std::lock_guard<std::mutex> lock(templatesMutex);
                snapshot = templatesRaw;
            }
            // список для экрана отчётов: заголовок, описание и состав параметров
            Json::Value list(Json::arrayValue);
            for (const auto &kv : snapshot) {
                Json::Value item;
                item["id"] = kv.first;

                Json::CharReaderBuilder builder;
                Json::Value body;
                std::string errs;
                std::istringstream in(kv.second);
                if (Json::parseFromStream(builder, in, &body, &errs)) {
                    item["title"] = body.get("title", Json::Value("")).asString();
                    item["description"] = body.get("description", Json::Value("")).asString();
                    item["folder"] = body.get("folder", Json::Value("")).asString();
                    item["tags"] = body.get("tags", Json::Value(Json::arrayValue));

                    Json::Value params(Json::arrayValue);
                    const Json::Value &declared = body["params"];
                    for (const auto &p : declared) {
                        const Json::Value &src = p["source"];
                        Json::Value one;
                        one["name"] = p.get("name", Json::Value("")).asString();
                        one["label"] = p.get("label", Json::Value("")).asString();
                        one["kind"] = src.isObject() ? src.get("kind", Json::Value("")).asString() : "text";
                        one["multiple"] = p.get("multiple", Json::Value(false)).asBool();
                        one["required"] = p.get("required", Json::Value(false)).asBool();
                        params.append(one);
                    }
                    item["params"] = params;
                } else {
                    item["title"] = "";
                    item["description"] = "";
                    item["params"] = Json::Value(Json::arrayValue);
                }
                list.append(item);
            }
            cb(jsonResponse(list));
        },
        {Get});

    app().registerHandler(
        "/api/reports/{id}",
        [&templatesRaw, &templatesMutex](const HttpRequestPtr &req,
                                          std::function<void(const HttpResponsePtr &)> &&cb,
                                          const std::string &id) {
            (void)req;
            std::map<std::string, std::string> snapshot;
            {
                std::lock_guard<std::mutex> lock(templatesMutex);
                snapshot = templatesRaw;
            }
            const auto it = snapshot.find(id);
            if (it == snapshot.end()) {
                sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + id + "' не найден");
                return;
            }
            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            // SQL справочников остаётся на сервере: клиенту не нужен и не показывается
            Json::CharReaderBuilder rb;
            Json::Value body;
            std::string errs;
            std::istringstream in(it->second);
            if (Json::parseFromStream(rb, in, &body, &errs)) {
                for (auto &p : body["params"]) {
                    if (p["source"].isObject()) p["source"].removeMember("sql");
                }
                for (auto &ds : body["datasets"]) {
                    if (ds.isObject()) ds.removeMember("sql");
                }
                Json::StreamWriterBuilder wb;
                wb["indentation"] = "";
                resp->setBody(Json::writeString(wb, body));
            } else {
                resp->setBody(it->second);
            }
            resp->addHeader("Cache-Control", "no-store");
            cb(resp);
        },
        {Get});

    // Справочники параметров живут в шаблоне (ADR-24): SQL берётся из source.sql.
    // Кэша на сервере нет — списки кэширует клиент; защита от тяжёлого SQL — statement_timeout.
    app().registerHandler(
        "/api/reports/{reportId}/lookups/{param}",
        [db, &templatesRaw, &templatesMutex](const HttpRequestPtr &req,
                                              std::function<void(const HttpResponsePtr &)> &&cb,
                                              const std::string &reportId, const std::string &param) {
            std::map<std::string, std::string> snapshot;
            {
                std::lock_guard<std::mutex> lock(templatesMutex);
                snapshot = templatesRaw;
            }
            const auto it = snapshot.find(reportId);
            if (it == snapshot.end()) {
                sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + reportId + "' не найден");
                return;
            }

            Json::CharReaderBuilder builder;
            Json::Value body;
            std::string errs;
            std::istringstream in(it->second);
            if (!Json::parseFromStream(builder, in, &body, &errs)) {
                sendError(cb, k500InternalServerError, "TEMPLATE_BROKEN", "шаблон отчёта не разбирается");
                return;
            }

            std::string sql;
            for (const auto &p : body["params"]) {
                if (p.get("name", Json::Value("")).asString() != param) continue;
                const Json::Value &src = p["source"];
                if (src.isObject() && src.get("kind", Json::Value("")).asString() == "lookup") {
                    sql = src.get("sql", Json::Value("")).asString();
                }
            }
            if (sql.empty()) {
                sendError(cb, k404NotFound, "LOOKUP_NOT_FOUND",
                          "у параметра '" + param + "' нет source.sql (SQL справочника задаётся в шаблоне)");
                return;
            }

            // ':name' → '$N', значения из query-строки; '::' остаётся кастом типа
            std::string compiled;
            std::vector<std::string> values;
            for (size_t i = 0; i < sql.size();) {
                const char c = sql[i];
                if (c == ':') {
                    if (i + 1 < sql.size() && sql[i + 1] == ':') {
                        compiled += "::";
                        i += 2;
                        continue;
                    }
                    size_t j = i + 1;
                    while (j < sql.size() && (std::isalnum(static_cast<unsigned char>(sql[j])) || sql[j] == '_')) ++j;
                    const std::string name = sql.substr(i + 1, j - i - 1);
                    if (name.empty()) {
                        compiled.push_back(c);
                        ++i;
                        continue;
                    }
                    const std::string v = req->getParameter(name);
                    if (v.empty()) {
                        sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS",
                                  "для справочника нужен параметр '" + name + "'");
                        return;
                    }
                    values.push_back(v);
                    compiled += "$" + std::to_string(values.size());
                    i = j;
                    continue;
                }
                compiled.push_back(c);
                ++i;
            }

            runQuery(
                db, compiled, values,
                [cb](const orm::Result &res) {
                    Json::Value list(Json::arrayValue);
                    for (const auto &row : res) {
                        const orm::Field v = row[0];
                        const orm::Field l = row[1];
                        Json::Value item;
                        item["value"] = v.isNull() ? "" : v.as<std::string>();
                        item["label"] = l.isNull() ? (v.isNull() ? "" : v.as<std::string>()) : l.as<std::string>();
                        list.append(item);
                    }
                    cb(jsonResponse(list));
                },
                [cb](const std::exception_ptr &eptr) {
                    std::string msg = "lookup failed";
                    try {
                        if (eptr) std::rethrow_exception(eptr);
                    } catch (const std::exception &e) {
                        msg = e.what();
                    }
                    sendError(cb, k500InternalServerError, "LOOKUP_FAILED", msg);
                });
        },
        {Get});

    app().registerHandler(
        "/api/report/data",
        [db, &registry, &templates, &templatesMutex, maxRows, &resultCache, &templatesVersion](
            const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                sendError(cb, k400BadRequest, "INVALID_PARAMS", "ожидался JSON-объект");
                return;
            }

            const std::string reportId = (*body).get("reportId", "").asString();
            if (reportId.empty()) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS",
                          "нужен reportId: SQL набора данных берётся из шаблона");
                return;
            }

            std::string datasetSql;
            std::map<std::string, std::string> datasetParams;
            {
                std::lock_guard<std::mutex> lock(templatesMutex);
                const auto it = templates.find(reportId);
                if (it == templates.end()) {
                    sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + reportId + "' не найден");
                    return;
                }
                datasetSql = it->second.datasetSql;
                datasetParams = it->second.datasetParams;
            }
            if (datasetSql.empty()) {
                sendError(cb, k422UnprocessableEntity, "TEMPLATE_INVALID",
                          "в шаблоне не задан datasets.<key>.sql");
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

            // Значения приходят по имени шаблона ("$from"), уходят в SQL как значения параметра.
            if (!datasetParams.empty()) {
                rep::ParamBag mapped;
                for (const auto &kv : datasetParams) {
                    const std::string &src = kv.second;
                    const std::string name = (!src.empty() && src[0] == '$') ? src.substr(1) : src;
                    if (const auto *v = bag.find(name)) mapped.set(kv.first, *v);
                }
                for (const auto &kv : bag.values())
                    if (!mapped.has(kv.first)) mapped.set(kv.first, kv.second);
                bag = mapped;
            }

            // Ключ кэша: отчёт + версия шаблонов + значения параметров (map уже отсортирован).
            // Версия в ключе делает публикацию шаблона инвалидацией.
            const std::uint64_t tplVersion = templatesVersion.load();
            std::string cacheKey = reportId;
            cacheKey += '|';
            cacheKey += std::to_string(tplVersion);
            for (const auto &kv : bag.values()) {
                cacheKey += '|';
                cacheKey += kv.first;
                cacheKey += '=';
                cacheKey += kv.second;
            }
            const std::string etag =
                "\"" + std::to_string(std::hash<std::string>{}(cacheKey + datasetSql)) + "\"";

            rep::ResultCache::Entry hit;
            if (resultCache.get(cacheKey, hit)) {
                if (req->getHeader("if-none-match") == hit.etag) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k304NotModified);
                    resp->addHeader("ETag", hit.etag);
                    resp->addHeader("X-Cache", "hit");
                    cb(resp);
                    return;
                }
                auto resp = HttpResponse::newHttpResponse();
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(hit.body);
                resp->addHeader("ETag", hit.etag);
                resp->addHeader("X-Cache", "hit");
                resp->addHeader("Cache-Control", "no-store");
                cb(resp);
                return;
            }

            // ':name' → $N, значения из bag; '::' остаётся кастом типа
            std::string compiled;
            std::vector<std::string> values;
            for (size_t i = 0; i < datasetSql.size();) {
                const char c = datasetSql[i];
                if (c == ':') {
                    if (i + 1 < datasetSql.size() && datasetSql[i + 1] == ':') {
                        compiled += "::";
                        i += 2;
                        continue;
                    }
                    size_t j = i + 1;
                    while (j < datasetSql.size() &&
                           (std::isalnum(static_cast<unsigned char>(datasetSql[j])) || datasetSql[j] == '_')) ++j;
                    const std::string name = datasetSql.substr(i + 1, j - i - 1);
                    if (name.empty()) {
                        compiled.push_back(c);
                        ++i;
                        continue;
                    }
                    const auto *v = bag.find(name);
                    if (v == nullptr) {
                        sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS",
                                  "не передан параметр '" + name + "'");
                        return;
                    }
                    // тип параметра берём из каста в SQL: без реестра это единственный источник.
                    const std::string tail = datasetSql.substr(std::min(j, datasetSql.size()));
                    const bool asDate = tail.rfind("::date", 0) == 0;
                    const bool asInt = tail.rfind("::int", 0) == 0;
                    if (asDate && !v->empty()) {
                        const std::string &s = *v;
                        const bool ok = (s.size() >= 10 && std::isdigit(static_cast<unsigned char>(s[0])) && s[4] == '-' && s[7] == '-');
                        if (!ok) {
                            sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS",
                                      "параметр '" + name + "' должен быть датой в формате ГГГГ-ММ-ДД");
                            return;
                        }
                    }
                    if (asInt && !v->empty()) {
                        const std::string &s = *v;
                        const bool ok = std::all_of(s.begin(), s.end(), [](char c) {
                            return std::isdigit(static_cast<unsigned char>(c)) || c == '-';
                        });
                        if (!ok) {
                            sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS",
                                      "параметр '" + name + "' должен быть целым числом");
                            return;
                        }
                    }
                    // пустая строка — законное значение («все», фильтр не задан)
                    values.push_back(*v);
                    compiled += "$" + std::to_string(values.size());
                    i = j;
                    continue;
                }
                compiled.push_back(c);
                ++i;
            }

            static const std::map<std::string, std::string> noDeclared;
            runQuery(
                db, compiled, values,
                [cb, maxRows, &resultCache, cacheKey, etag](const orm::Result &res) {
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
                        types.push_back(columnType(names.back(), noDeclared, res));
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

                    Json::StreamWriterBuilder wb;
                    wb["indentation"] = "";
                    std::string payload = Json::writeString(wb, out);
                    resultCache.put(cacheKey, payload, etag);

                    auto resp = HttpResponse::newHttpResponse();
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(payload);
                    resp->addHeader("ETag", etag);
                    resp->addHeader("X-Cache", "miss");
                    resp->addHeader("Cache-Control", "no-store");
                    cb(resp);
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
                    // типы параметров больше не описаны в реестре: ошибку приведения значения
                    // PostgreSQL отдаём как 422, чтобы контракт API не менялся
                    if (msg.find("invalid input syntax") != std::string::npos) {
                        sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", msg);
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

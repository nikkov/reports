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

#include "auth.h"
#include "cache.h"
#include "aggregate.h"
#include "csv_export.h"
#include "dataset.h"
#include "xlsx_export.h"
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

// --- Обновление по событию (ADR-42) -------------------------------------
// Клиент держит запрос /api/report/changes до 20 с; публикация или откат шаблона
// будит ждущих и сразу отдаёт changed=true — экран обновляется без опроса по таймеру.
namespace {

struct ChangeWaiter {
    std::function<void(const HttpResponsePtr &)> cb;
    trantor::TimerId timer;
};

std::mutex changesMutex;
std::map<std::string, std::vector<ChangeWaiter>> changeWaiters;

} // namespace

void notifyChange(const std::string &reportId) {
    std::vector<ChangeWaiter> waiters;
    {
        std::lock_guard<std::mutex> lock(changesMutex);
        const auto it = changeWaiters.find(reportId);
        if (it == changeWaiters.end()) return;
        waiters = std::move(it->second);
        changeWaiters.erase(it);
    }
    for (auto &w : waiters) {
        app().getLoop()->invalidateTimer(w.timer);
        Json::Value out;
        out["changed"] = true;
        out["reportId"] = reportId;
        w.cb(jsonResponse(out));
    }
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

struct DatasetInfo {
    std::string sql;
    std::map<std::string, std::string> params; // имя параметра запроса → "$param" шаблона
};

struct TemplateInfo {
    std::string id;
    std::string title;
    std::string datasetSql;                             // SQL первого набора (совместимость)
    std::map<std::string, std::string> datasetParams;
    std::map<std::string, DatasetInfo> datasets;        // все наборы шаблона (ADR-36)
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
    // SQL набора данных живёт в шаблоне (ADR-27); queryId из реестра больше не поддерживается
    out.datasets.clear();
    for (auto ds = datasets.begin(); ds != datasets.end(); ++ds) {
        if (ds->isMember("queryId")) return false;
        DatasetInfo info;
        info.sql = (*ds).get("sql", "").asString();
        if (info.sql.empty()) return false;
        const Json::Value &p = (*ds)["params"];
        if (p.isObject()) {
            for (auto it = p.begin(); it != p.end(); ++it) info.params[it.name()] = it->asString();
        }
        out.datasets[ds.name()] = info;
    }
    out.datasetSql = out.datasets.begin()->second.sql;
    out.datasetParams = out.datasets.begin()->second.params;
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

    // Чтение данных отчётов — под ролью только-чтения (ADR-33), запись шаблонов и админка
    // остаются на основной учётной записи. Если роль не настроена, работаем как раньше.
    const std::string roDsn = rep::readOnlyConnInfoFromEnv();
    orm::DbClientPtr dbRo;
    if (!roDsn.empty()) {
        dbRo = orm::DbClient::newPgClient(roDsn, 2);
        LOG_INFO << "данные отчётов читаются под ролью только-чтения ("
                 << rep::envOr("REP_RO_DB_USER", "") << ")";
    } else {
        LOG_WARN << "REP_RO_DB_USER/REP_RO_DB_PASSWORD не заданы: данные читаются основной ролью";
    }
    auto dataDb = dbRo ? dbRo : db;

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
    // Аутентификация (ADR-34): по умолчанию выключена, включает REP_AUTH=1
    const bool authEnabled = rep::envOr("REP_AUTH", "0") == "1";
    const int sessionTtlHours = std::stoi(rep::envOr("REP_SESSION_TTL_HOURS", "12"));
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
        notifyChange(id); // экраны с включённым live узнают об изменении сразу
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


    // --- Аутентификация и роли (ADR-34) -------------------------------------
    auto unauthorized = [](const std::string &msg) {
        auto resp = HttpResponse::newHttpResponse();
        Json::Value body;
        body["error"]["code"] = "UNAUTHORIZED";
        body["error"]["message"] = msg;
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        resp->setStatusCode(k401Unauthorized);
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        resp->setBody(Json::writeString(wb, body));
        return resp;
    };
    auto forbidden = [](const std::string &msg) {
        auto resp = HttpResponse::newHttpResponse();
        Json::Value body;
        body["error"]["code"] = "FORBIDDEN";
        body["error"]["message"] = msg;
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        resp->setStatusCode(k403Forbidden);
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        resp->setBody(Json::writeString(wb, body));
        return resp;
    };

    // Проверка сессии до маршрутизации: один раз на запрос, роль зависит от метода и пути.
    if (authEnabled) {
        app().registerPreRoutingAdvice(
            [db, &unauthorized, &forbidden](const HttpRequestPtr &req, AdviceCallback &&respond,
                                            AdviceChainCallback &&next) {
                const rep::Role need = rep::requiredRole(std::string(req->methodString()), req->path());
                if (need == rep::Role::Public) {
                    next();
                    return;
                }
                const std::string token = req->getCookie("rep_session");
                if (token.empty()) {
                    respond(unauthorized("нужна авторизация"));
                    return;
                }
                // callbacks копируем, а не перемещаем: они нужны обеим веткам (успех и ошибка)
                runQuery(
                    db,
                    "SELECT u.id, u.login, u.role FROM sessions s JOIN users u ON u.id = s.user_id "
                    "WHERE s.token = $1 AND s.expires_at > now()",
                    {token},
                    [req, need, &unauthorized, &forbidden, respond, next](const orm::Result &res) {
                        if (res.empty()) {
                            respond(unauthorized("сессия истекла или не найдена"));
                            return;
                        }
                        const std::string role = res[0]["role"].as<std::string>();
                        if (!rep::roleAtLeast(role, need)) {
                            respond(forbidden("недостаточно прав: нужна роль выше"));
                            return;
                        }
                        req->attributes()->insert("session_role", role);
                        req->attributes()->insert("session_login", res[0]["login"].as<std::string>());
                        next();
                    },
                    [&unauthorized, respond](const std::exception_ptr &) {
                        respond(unauthorized("не удалось проверить сессию"));
                    });
            });
    }

    app().registerHandler(
        "/api/auth/login",
        [db, authEnabled, sessionTtlHours](const HttpRequestPtr &req,
                                           std::function<void(const HttpResponsePtr &)> &&cb) {
            if (!authEnabled) {
                sendError(cb, k404NotFound, "AUTH_DISABLED", "аутентификация выключена (REP_AUTH=0)");
                return;
            }
            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                sendError(cb, k400BadRequest, "INVALID_PARAMS", "ожидался JSON {login, password}");
                return;
            }
            const std::string login = (*body).get("login", "").asString();
            const std::string password = (*body).get("password", "").asString();
            if (login.empty() || password.empty()) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", "нужны login и password");
                return;
            }
            runQuery(
                db, "SELECT id, login, password_salt, password_hash, role FROM users WHERE login = $1", {login},
                [db, cb, login, password, sessionTtlHours](const orm::Result &res) {
                    if (res.empty() ||
                        !rep::verifyPassword(password, res[0]["password_salt"].as<std::string>(),
                                             res[0]["password_hash"].as<std::string>())) {
                        sendError(cb, k401Unauthorized, "BAD_CREDENTIALS", "неверный логин или пароль");
                        return;
                    }
                    const long userId = res[0]["id"].as<long long>();
                    const std::string role = res[0]["role"].as<std::string>();
                    const std::string token = rep::randomHex(32);
                    runQuery(
                        db,
                        "INSERT INTO sessions (token, user_id, expires_at) "
                        "VALUES ($1, $2, now() + ($3 || ' hours')::interval)",
                        {token, std::to_string(userId), std::to_string(sessionTtlHours)},
                        [cb, token, login, role, sessionTtlHours](const orm::Result &) {
                            Cookie cookie("rep_session", token);
                            cookie.setPath("/");
                            cookie.setHttpOnly(true);
                            cookie.setSameSite(Cookie::SameSite::kLax);
                            cookie.setMaxAge(sessionTtlHours * 3600);
                            cookie.setSecure(false); // стенд локальный, https не используется

                            Json::Value out;
                            out["login"] = login;
                            out["role"] = role;
                            auto resp = jsonResponse(out);
                            resp->addCookie(cookie);
                            cb(resp);
                        },
                        [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "LOGIN_FAILED", describeError(e)); });
                },
                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "LOGIN_FAILED", describeError(e)); });
        },
        {Post});

    app().registerHandler(
        "/api/auth/logout",
        [db, authEnabled](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            const std::string token = req->getCookie("rep_session");
            auto finish = [cb]() {
                Cookie cookie("rep_session", "");
                cookie.setPath("/");
                cookie.setMaxAge(0);
                Json::Value out;
                out["logout"] = true;
                auto resp = jsonResponse(out);
                resp->addCookie(cookie);
                cb(resp);
            };
            if (!authEnabled || token.empty()) {
                finish();
                return;
            }
            runQuery(db, "DELETE FROM sessions WHERE token = $1", {token},
                     [finish](const orm::Result &) { finish(); },
                     [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "LOGOUT_FAILED", describeError(e)); });
        },
        {Post});

    app().registerHandler(
        "/api/auth/me",
        [db, authEnabled](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            if (!authEnabled) {
                Json::Value out;
                out["auth"] = false;
                cb(jsonResponse(out));
                return;
            }
            const std::string token = req->getCookie("rep_session");
            if (token.empty()) {
                sendError(cb, k401Unauthorized, "UNAUTHORIZED", "нужна авторизация");
                return;
            }
            runQuery(
                db,
                "SELECT u.login, u.role FROM sessions s JOIN users u ON u.id = s.user_id "
                "WHERE s.token = $1 AND s.expires_at > now()",
                {token},
                [cb](const orm::Result &res) {
                    if (res.empty()) {
                        sendError(cb, k401Unauthorized, "UNAUTHORIZED", "сессия истекла");
                        return;
                    }
                    Json::Value out;
                    out["auth"] = true;
                    out["login"] = res[0]["login"].as<std::string>();
                    out["role"] = res[0]["role"].as<std::string>();
                    cb(jsonResponse(out));
                },
                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "ME_FAILED", describeError(e)); });
        },
        {Get});


    // Серверная агрегация (ADR-37): GROUP BY поверх выбранного набора данных.
    // Клиент получает уже сгруппированные строки — на 2 746 строках это 5 вместо тысяч.
    app().registerHandler(
        "/api/report/aggregate",
        [dataDb, &templates, &templatesMutex, maxRows](const HttpRequestPtr &req,
                                                       std::function<void(const HttpResponsePtr &)> &&cb) {
            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                sendError(cb, k400BadRequest, "INVALID_PARAMS", "ожидался JSON-объект");
                return;
            }
            const std::string reportId = (*body).get("reportId", "").asString();
            if (reportId.empty()) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", "нужен reportId");
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
                const auto &all = it->second.datasets;
                if (all.empty()) {
                    datasetSql = it->second.datasetSql;
                    datasetParams = it->second.datasetParams;
                } else {
                    const std::string key = (*body).get("dataset", "").asString();
                    const auto ds = all.find(key.empty() ? all.begin()->first : key);
                    if (ds == all.end()) {
                        sendError(cb, k422UnprocessableEntity, "DATASET_NOT_FOUND",
                                  "в шаблоне нет набора '" + key + "'");
                        return;
                    }
                    datasetSql = ds->second.sql;
                    datasetParams = ds->second.params;
                }
            }

            rep::ParamBag bag;
            const Json::Value &rawParams = (*body)["params"];
            if (rawParams.isObject()) {
                for (auto it = rawParams.begin(); it != rawParams.end(); ++it) {
                    if (it->isString() || it->isInt64() || it->isUInt64() || it->isDouble() || it->isBool()) {
                        bag.set(it.name(), it->asString());
                    }
                }
            }

            std::vector<std::string> groupBy;
            for (const auto &g : (*body)["groupBy"]) {
                if (g.isString()) groupBy.push_back(g.asString());
            }
            std::vector<std::pair<std::string, std::string>> measures;
            for (const auto &m : (*body)["measures"]) {
                if (m.isObject() && m["field"].isString() && m["agg"].isString()) {
                    measures.emplace_back(m["field"].asString(), m["agg"].asString());
                }
            }

            rep::DatasetQuery query;
            std::string err;
            if (!rep::compileDataset(datasetSql, datasetParams, bag, query, err)) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", err);
                return;
            }
            std::string aggSql;
            if (!rep::buildAggregateSql(query.sql, groupBy, measures, aggSql, err)) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", err);
                return;
            }

            static const std::map<std::string, std::string> noDeclared;
            runQuery(
                dataDb, aggSql, query.values,
                [cb, maxRows, groupBy](const orm::Result &res) {
                    if (res.size() > maxRows) {
                        sendError(cb, k422UnprocessableEntity, "ROW_LIMIT", "результат больше лимита строк");
                        return;
                    }
                    Json::Value columns(Json::arrayValue);
                    const auto ncols = static_cast<size_t>(res.columns());
                    std::vector<std::string> names;
                    for (size_t c = 0; c < ncols; ++c) {
                        names.emplace_back(res.columnName(static_cast<unsigned long>(c)));
                        Json::Value col;
                        col["name"] = names.back();
                        col["type"] = columnType(names.back(), noDeclared, res);
                        columns.append(col);
                    }
                    Json::Value rows(Json::arrayValue);
                    for (const auto &row : res) {
                        Json::Value obj(Json::objectValue);
                        for (size_t c = 0; c < ncols; ++c) {
                            obj[names[c]] = fieldToJson(row[c], columnType(names[c], noDeclared, res));
                        }
                        rows.append(obj);
                    }
                    Json::Value out;
                    out["columns"] = columns;
                    out["rows"] = rows;
                    out["rowCount"] = static_cast<Json::UInt64>(res.size());
                    out["groupBy"] = Json::Value(Json::arrayValue);
                    for (const auto &g : groupBy) out["groupBy"].append(g);
                    cb(jsonResponse(out));
                },
                [cb](const std::exception_ptr &e) {
                    std::string msg = "aggregate failed";
                    try {
                        if (e) std::rethrow_exception(e);
                    } catch (const std::exception &ex) {
                        msg = ex.what();
                    }
                    // неизвестная колонка — ошибка запроса, а не сервера
                    sendError(cb, msg.find("does not exist") != std::string::npos ? k422UnprocessableEntity
                                                                                 : k500InternalServerError,
                              "AGGREGATE_FAILED", msg);
                });
        },
        {Post});


    // Серверный экспорт в .xlsx (ADR-38): шапка из шаблона, данные набора, лист параметров.
    // Нужен для рассылки и для объёмов, которые не помещаются в браузерный экспорт.
    app().registerHandler(
        "/api/report/export.xlsx",
        [dataDb, &templates, &templatesRaw, &templatesMutex](const HttpRequestPtr &req,
                                                             std::function<void(const HttpResponsePtr &)> &&cb) {
            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                sendError(cb, k400BadRequest, "INVALID_PARAMS", "ожидался JSON-объект");
                return;
            }
            const std::string reportId = (*body).get("reportId", "").asString();
            if (reportId.empty()) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", "нужен reportId");
                return;
            }

            std::string datasetSql;
            std::map<std::string, std::string> datasetParams;
            std::string datasetKey;
            std::string raw;
            {
                std::lock_guard<std::mutex> lock(templatesMutex);
                const auto it = templates.find(reportId);
                if (it == templates.end()) {
                    sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + reportId + "' не найден");
                    return;
                }
                const auto &all = it->second.datasets;
                if (all.empty()) {
                    datasetSql = it->second.datasetSql;
                    datasetParams = it->second.datasetParams;
                } else {
                    const std::string key = (*body).get("dataset", "").asString();
                    const auto ds = all.find(key.empty() ? all.begin()->first : key);
                    if (ds == all.end()) {
                        sendError(cb, k422UnprocessableEntity, "DATASET_NOT_FOUND",
                                  "в шаблоне нет набора '" + key + "'");
                        return;
                    }
                    datasetKey = ds->first;
                    datasetSql = ds->second.sql;
                    datasetParams = ds->second.params;
                }
                const auto rawIt = templatesRaw.find(reportId);
                if (rawIt != templatesRaw.end()) raw = rawIt->second;
            }

            rep::ParamBag bag;
            const Json::Value &rawParams = (*body)["params"];
            if (rawParams.isObject()) {
                for (auto it = rawParams.begin(); it != rawParams.end(); ++it) {
                    if (it->isString() || it->isInt64() || it->isUInt64() || it->isDouble() || it->isBool()) {
                        bag.set(it.name(), it->asString());
                    }
                }
            }

            rep::DatasetQuery query;
            std::string err;
            if (!rep::compileDataset(datasetSql, datasetParams, bag, query, err)) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", err);
                return;
            }

            // колонки и заголовок — из шаблона: блок с этим dataKey, иначе первый табличный
            Json::Value tpl;
            if (!raw.empty()) {
                Json::CharReaderBuilder builder;
                std::string errs;
                std::istringstream in(raw);
                Json::parseFromStream(builder, in, &tpl, &errs);
            }

            Json::Value block;
            for (const auto &b : tpl["blocks"]) {
                if (b.get("type", "").asString() != "table") continue;
                if (block.isNull() || (!datasetKey.empty() && b.get("dataKey", "").asString() == datasetKey)) {
                    block = b;
                }
            }

            struct Leaf {
                std::string field;
                std::string title;
                std::string numFmt;
                bool numeric;
            };
            std::vector<Leaf> leaves;
            std::function<void(const Json::Value &)> walk = [&](const Json::Value &col) {
                if (col["children"].isArray()) {
                    for (const auto &child : col["children"]) walk(child);
                    return;
                }
                Leaf leaf;
                leaf.field = col.get("field", "").asString();
                leaf.title = col.get("title", leaf.field).asString();
                const std::string fmt = col["format"].isString() ? col["format"].asString() : "";
                leaf.numeric = fmt == "number" || fmt == "integer" || fmt == "percent" ||
                               fmt == "currency" || col.isMember("aggregate");
                leaf.numFmt = fmt == "percent" ? "0.00%" : leaf.numeric ? "# ##0" : "";
                leaves.push_back(leaf);
            };
            for (const auto &col : block["columns"]) walk(col);

            const std::string title = tpl.get("title", reportId).asString();

            // Если блок агрегирует на сервере — файл должен содержать те же строки, что на экране
            std::string finalSql = query.sql;
            const Json::Value &agg = block["aggregate"];
            if (agg.isObject()) {
                std::vector<std::string> groupBy;
                for (const auto &g : agg["groupBy"]) {
                    if (g.isString()) groupBy.push_back(g.asString());
                }
                std::vector<std::pair<std::string, std::string>> measures;
                for (const auto &m : agg["measures"]) {
                    if (m.isObject() && m["field"].isString() && m["agg"].isString()) {
                        measures.emplace_back(m["field"].asString(), m["agg"].asString());
                    }
                }
                std::string aggErr;
                if (!rep::buildAggregateSql(finalSql, groupBy, measures, finalSql, aggErr)) {
                    sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", aggErr);
                    return;
                }
            }

            static const std::map<std::string, std::string> noDeclared;
            runQuery(
                dataDb, finalSql, query.values,
                [cb, leaves, title, bag, noDeclared, reportId](const orm::Result &res) {
                    const auto ncols = static_cast<size_t>(res.columns());
                    std::vector<std::string> names;
                    std::vector<std::string> types;
                    for (size_t c = 0; c < ncols; ++c) {
                        names.emplace_back(res.columnName(static_cast<unsigned long>(c)));
                        types.push_back(columnType(names.back(), noDeclared, res));
                    }

                    rep::ExportSheet sheet;
                    sheet.title = title;
                    for (const auto &leaf : leaves) {
                        sheet.columns.push_back({leaf.title, leaf.numFmt, leaf.numeric});
                    }
                    for (const auto &kv : bag.values()) sheet.params.emplace_back(kv.first, kv.second);

                    for (const auto &row : res) {
                        std::vector<std::string> values;
                        for (const auto &leaf : leaves) {
                            const auto pos = std::find(names.begin(), names.end(), leaf.field);
                            if (pos == names.end()) {
                                values.emplace_back();
                                continue;
                            }
                            const size_t idx = static_cast<size_t>(pos - names.begin());
                            const orm::Field &f = row[idx];
                            if (f.isNull()) {
                                values.emplace_back();
                            } else if (types[idx] == "int") {
                                values.push_back(std::to_string(f.as<long long>()));
                            } else if (types[idx] == "double") {
                                std::ostringstream os;
                                os << f.as<double>();
                                values.push_back(os.str());
                            } else {
                                values.push_back(f.as<std::string>());
                            }
                        }
                        sheet.rows.push_back(values);
                    }

                    const char *tmp = std::getenv("TMPDIR");
                    const std::string path = std::string(tmp ? tmp : "/tmp") + "/rep-export-" +
                                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                             ".xlsx";
                    std::string werr;
                    if (!rep::writeXlsx(path, sheet, werr)) {
                        sendError(cb, k500InternalServerError, "EXPORT_FAILED", werr);
                        return;
                    }
                    std::ifstream file(path, std::ios::binary);
                    std::stringstream ss;
                    ss << file.rdbuf();
                    file.close();
                    std::remove(path.c_str());

                    auto resp = HttpResponse::newHttpResponse();
                    resp->setContentTypeString(
                        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
                    // имя файла только ASCII: кириллица в заголовке недопустима по RFC 6266
                    resp->addHeader("Content-Disposition", "attachment; filename=\"" + reportId + ".xlsx\"");
                    resp->setBody(ss.str());
                    cb(resp);
                },
                [cb](const std::exception_ptr &e) {
                    sendError(cb, k500InternalServerError, "EXPORT_FAILED", describeError(e));
                });
        },
        {Post});

    // CSV-экспорт (ADR-40): тот же расчёт, что у .xlsx, но текстом; ';' и BOM — для Excel.
    app().registerHandler(
        "/api/report/export.csv",
        [dataDb, &templates, &templatesRaw, &templatesMutex](const HttpRequestPtr &req,
                                                             std::function<void(const HttpResponsePtr &)> &&cb) {
            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                sendError(cb, k400BadRequest, "INVALID_PARAMS", "ожидался JSON-объект");
                return;
            }
            const std::string reportId = (*body).get("reportId", "").asString();
            if (reportId.empty()) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", "нужен reportId");
                return;
            }

            std::string datasetSql;
            std::map<std::string, std::string> datasetParams;
            std::string datasetKey;
            std::string raw;
            {
                std::lock_guard<std::mutex> lock(templatesMutex);
                const auto it = templates.find(reportId);
                if (it == templates.end()) {
                    sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + reportId + "' не найден");
                    return;
                }
                const auto &all = it->second.datasets;
                if (all.empty()) {
                    datasetSql = it->second.datasetSql;
                    datasetParams = it->second.datasetParams;
                } else {
                    const std::string key = (*body).get("dataset", "").asString();
                    const auto ds = all.find(key.empty() ? all.begin()->first : key);
                    if (ds == all.end()) {
                        sendError(cb, k422UnprocessableEntity, "DATASET_NOT_FOUND",
                                  "в шаблоне нет набора '" + key + "'");
                        return;
                    }
                    datasetKey = ds->first;
                    datasetSql = ds->second.sql;
                    datasetParams = ds->second.params;
                }
                const auto rawIt = templatesRaw.find(reportId);
                if (rawIt != templatesRaw.end()) raw = rawIt->second;
            }

            rep::ParamBag bag;
            const Json::Value &rawParams = (*body)["params"];
            if (rawParams.isObject()) {
                for (auto it = rawParams.begin(); it != rawParams.end(); ++it) {
                    if (it->isString() || it->isInt64() || it->isUInt64() || it->isDouble() || it->isBool()) {
                        bag.set(it.name(), it->asString());
                    }
                }
            }

            rep::DatasetQuery query;
            std::string err;
            if (!rep::compileDataset(datasetSql, datasetParams, bag, query, err)) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", err);
                return;
            }

            Json::Value tpl;
            if (!raw.empty()) {
                Json::CharReaderBuilder builder;
                std::string errs;
                std::istringstream in(raw);
                Json::parseFromStream(builder, in, &tpl, &errs);
            }

            Json::Value block;
            for (const auto &b : tpl["blocks"]) {
                if (b.get("type", "").asString() != "table") continue;
                if (block.isNull() || (!datasetKey.empty() && b.get("dataKey", "").asString() == datasetKey)) {
                    block = b;
                }
            }

            struct Leaf {
                std::string field;
                std::string title;
                bool numeric;
            };
            std::vector<Leaf> leaves;
            std::function<void(const Json::Value &)> walk = [&](const Json::Value &col) {
                if (col["children"].isArray()) {
                    for (const auto &child : col["children"]) walk(child);
                    return;
                }
                const std::string fmt = col["format"].isString() ? col["format"].asString() : "";
                leaves.push_back({col.get("field", "").asString(), col.get("title", "").asString(),
                                  fmt == "number" || fmt == "integer" || fmt == "percent" ||
                                      fmt == "currency" || col.isMember("aggregate")});
            };
            for (const auto &col : block["columns"]) walk(col);

            // агрегация блока: CSV должен совпадать с тем, что видит пользователь
            std::string finalSql = query.sql;
            const Json::Value &agg = block["aggregate"];
            if (agg.isObject()) {
                std::vector<std::string> groupBy;
                for (const auto &g : agg["groupBy"]) {
                    if (g.isString()) groupBy.push_back(g.asString());
                }
                std::vector<std::pair<std::string, std::string>> measures;
                for (const auto &m : agg["measures"]) {
                    if (m.isObject() && m["field"].isString() && m["agg"].isString()) {
                        measures.emplace_back(m["field"].asString(), m["agg"].asString());
                    }
                }
                std::string aggErr;
                if (!rep::buildAggregateSql(finalSql, groupBy, measures, finalSql, aggErr)) {
                    sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", aggErr);
                    return;
                }
            }

            static const std::map<std::string, std::string> noDeclared;
            runQuery(
                dataDb, finalSql, query.values,
                [cb, leaves, bag, noDeclared, reportId](const orm::Result &res) {
                    const auto ncols = static_cast<size_t>(res.columns());
                    std::vector<std::string> names;
                    std::vector<std::string> types;
                    for (size_t c = 0; c < ncols; ++c) {
                        names.emplace_back(res.columnName(static_cast<unsigned long>(c)));
                        types.push_back(columnType(names.back(), noDeclared, res));
                    }
                    rep::ExportSheet sheet;
                    for (const auto &leaf : leaves) {
                        sheet.columns.push_back({leaf.title, "", leaf.numeric});
                    }
                    for (const auto &kv : bag.values()) sheet.params.emplace_back(kv.first, kv.second);

                    for (const auto &row : res) {
                        std::vector<std::string> values;
                        for (const auto &leaf : leaves) {
                            const auto pos = std::find(names.begin(), names.end(), leaf.field);
                            if (pos == names.end()) {
                                values.emplace_back();
                                continue;
                            }
                            const size_t idx = static_cast<size_t>(pos - names.begin());
                            const orm::Field &f = row[idx];
                            if (f.isNull()) values.emplace_back();
                            else if (types[idx] == "int") values.push_back(std::to_string(f.as<long long>()));
                            else if (types[idx] == "double") {
                                std::ostringstream os;
                                os << f.as<double>();
                                values.push_back(os.str());
                            } else values.push_back(f.as<std::string>());
                        }
                        sheet.rows.push_back(values);
                    }

                    auto resp = HttpResponse::newHttpResponse();
                    resp->setContentTypeString("text/csv; charset=utf-8");
                    resp->addHeader("Content-Disposition", "attachment; filename=\"" +
                                                               std::string(reportId) + ".csv\"");
                    resp->setBody(rep::toCsv(sheet));
                    cb(resp);
                },
                [cb](const std::exception_ptr &e) {
                    sendError(cb, k500InternalServerError, "EXPORT_FAILED", describeError(e));
                });
        },
        {Post});

    // Long-poll: сервер сам сообщает об изменении шаблона (ADR-42).
    app().registerHandler(
        "/api/report/changes",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            const std::string reportId = req->getParameter("report");
            if (reportId.empty()) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", "нужен параметр report");
                return;
            }
            auto done = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(cb));
            auto tid = std::make_shared<trantor::TimerId>(0);
            *tid = app().getLoop()->runAfter(20.0, [reportId, done, tid]() {
                {
                    std::lock_guard<std::mutex> lock(changesMutex);
                    const auto it = changeWaiters.find(reportId);
                    if (it != changeWaiters.end()) {
                        auto &list = it->second;
                        list.erase(std::remove_if(list.begin(), list.end(),
                                                  [&](const ChangeWaiter &w) { return w.timer == *tid; }),
                                   list.end());
                        if (list.empty()) changeWaiters.erase(it);
                    }
                }
                Json::Value out;
                out["changed"] = false;
                (*done)(jsonResponse(out));
            });
            {
                std::lock_guard<std::mutex> lock(changesMutex);
                changeWaiters[reportId].push_back({*done, *tid});
            }
        },
        {Get});

    app().registerHandler(
        "/api/health",
        [db, maxRows, &dbRo](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
            runQuery(
                db, "SELECT 1", {},
                [cb, maxRows, &dbRo](const orm::Result &res) {
                    Json::Value body;
                    body["status"] = "ok";
                    body["schemaVersion"] = 1;
                    body["db"] = !res.empty();
                    body["maxRows"] = static_cast<Json::UInt64>(maxRows);
                    body["readOnlyRole"] = static_cast<bool>(dbRo);
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
        [&templatesRaw, &templatesMutex](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
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

            // Серверный поиск и пагинация (ADR-41): без limit отдаём массив, как раньше,
            // с limit — {items, total}, чтобы клиент мог подгружать страницы.
            const std::string q = req->getParameter("q");
            const std::string folder = req->getParameter("folder");
            const std::string tag = req->getParameter("tag");
            std::string qLower = q;
            std::transform(qLower.begin(), qLower.end(), qLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            Json::Value filtered(Json::arrayValue);
            for (const auto &item : list) {
                if (!folder.empty()) {
                    const std::string itemFolder = item.get("folder", "").asString();
                    if (folder == "__none__") {
                        if (!itemFolder.empty()) continue;
                    } else if (itemFolder != folder) {
                        continue;
                    }
                }
                if (!tag.empty()) {
                    bool has = false;
                    for (const auto &t : item["tags"]) {
                        if (t.asString() == tag) has = true;
                    }
                    if (!has) continue;
                }
                if (!qLower.empty()) {
                    std::string haystack = item.get("title", "").asString() + " " + item.get("id", "").asString() +
                                           " " + item.get("description", "").asString();
                    for (const auto &t : item["tags"]) haystack += " " + t.asString();
                    std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (haystack.find(qLower) == std::string::npos) continue;
                }
                filtered.append(item);
            }

            const std::string limitParam = req->getParameter("limit");
            if (limitParam.empty()) {
                cb(jsonResponse(filtered));
                return;
            }

            const size_t limit = std::min<size_t>(200, std::max(1, std::stoi(limitParam)));
            const size_t offset = req->getParameter("offset").empty() ? 0 : std::stoul(req->getParameter("offset"));
            Json::Value items(Json::arrayValue);
            for (size_t i = offset; i < filtered.size() && items.size() < limit; ++i) items.append(filtered[static_cast<Json::ArrayIndex>(i)]);

            Json::Value out;
            out["items"] = items;
            out["total"] = static_cast<Json::UInt64>(filtered.size());
            out["limit"] = static_cast<Json::UInt64>(limit);
            out["offset"] = static_cast<Json::UInt64>(offset);
            cb(jsonResponse(out));
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
        [dataDb, &templatesRaw, &templatesMutex](const HttpRequestPtr &req,
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
                dataDb, compiled, values,
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
        [dataDb, &registry, &templates, &templatesMutex, maxRows, &resultCache, &templatesVersion](
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
            std::string datasetKey; // какой набор отдаём: '' — первый в шаблоне (ADR-36)
            {
                std::lock_guard<std::mutex> lock(templatesMutex);
                const auto it = templates.find(reportId);
                if (it == templates.end()) {
                    sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + reportId + "' не найден");
                    return;
                }
                const auto &all = it->second.datasets;
                if (all.empty()) {
                    datasetSql = it->second.datasetSql; // шаблон без данных: прежнее поведение
                    datasetParams = it->second.datasetParams;
                } else {
                    datasetKey = (*body).get("dataset", "").asString();
                    const auto ds = all.find(datasetKey.empty() ? all.begin()->first : datasetKey);
                    if (ds == all.end()) {
                        sendError(cb, k422UnprocessableEntity, "DATASET_NOT_FOUND",
                                  "в шаблоне нет набора '" + datasetKey + "'");
                        return;
                    }
                    datasetKey = ds->first;
                    datasetSql = ds->second.sql;
                    datasetParams = ds->second.params;
                }
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

            rep::DatasetQuery query;
            std::string compileErr;
            if (!rep::compileDataset(datasetSql, datasetParams, bag, query, compileErr)) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", compileErr);
                return;
            }

            // Ключ кэша: отчёт + версия шаблонов + значения параметров (map уже отсортирован).
            // Версия в ключе делает публикацию шаблона инвалидацией.
            const std::uint64_t tplVersion = templatesVersion.load();
            std::string cacheKey = reportId;
            cacheKey += '|';
            cacheKey += datasetKey; // набор данных — часть ключа: у блоков разные запросы
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
                dataDb, query.sql, query.values,
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

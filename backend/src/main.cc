#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Result.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <libpq-fe.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <future>
#include <mutex>
#include <thread>
#include <memory>
#include <string>
#include <vector>

#include "auth.h"
#include "cache.h"
#include "aggregate.h"
#include "csv_export.h"
#include "dataset.h"
#include "smtp.h"
#include "subscriptions.h"
#include "template_migrate.h"
#include "validate.h"
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

// --- Обновление по событию (ADR-44) -------------------------------------
// Одно SSE-соединение на зрителя: сервер сам пишет в потоке, когда шаблон
// опубликован или откатан, и раз в 15 с шлёт комментарий-пульс, чтобы прокси
// не закрывали простой. Клиент переподключается сам (EventSource).
namespace {

struct SseClient {
    std::shared_ptr<drogon::ResponseStream> stream;
};

std::mutex sseMutex;
std::map<std::string, std::map<uint64_t, std::shared_ptr<drogon::ResponseStream>>> sseClients;
std::atomic<uint64_t> sseNextId{1};
std::atomic<bool> sseHeartbeatStarted{false};

void sseWrite(const std::shared_ptr<drogon::ResponseStream> &stream, const std::string &payload) {
    if (!stream) return;
    stream->send(payload);
}

// пульс: держит соединение живым и заодно убирает отвалившихся клиентов
void sseHeartbeat() {
    std::lock_guard<std::mutex> lock(sseMutex);
    for (auto report = sseClients.begin(); report != sseClients.end();) {
        for (auto client = report->second.begin(); client != report->second.end();) {
            if (!client->second->send(": ping\n\n")) {
                client = report->second.erase(client);
            } else {
                ++client;
            }
        }
        if (report->second.empty()) {
            report = sseClients.erase(report);
        } else {
            ++report;
        }
    }
}

} // namespace

void notifyChange(const std::string &reportId) {
    std::vector<std::shared_ptr<drogon::ResponseStream>> targets;
    {
        std::lock_guard<std::mutex> lock(sseMutex);
        const auto it = sseClients.find(reportId);
        if (it == sseClients.end()) return;
        for (const auto &kv : it->second) targets.push_back(kv.second);
    }
    const std::string payload =
        "event: change\ndata: {\"reportId\":\"" + reportId + "\"}\n\n";
    for (const auto &stream : targets) sseWrite(stream, payload);
}


// --- Аудит действий (ADR-52) ---------------------------------------------
// Пишем отдельным коротким соединением: событий немного (админские действия),
// а блокирующая вставка не требует ни цикла событий, ни захвата колбэков.
void auditEvent(const std::string &actor, const std::string &action, const std::string &objectType,
                const std::string &objectId, const std::string &detailsJson, const std::string &ip) {
    PGconn *conn = PQconnectdb(rep::connInfoFromEnv().c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        LOG_WARN << "аудит: нет соединения с БД: " << PQerrorMessage(conn);
        PQfinish(conn);
        return;
    }
    const char *values[] = {actor.c_str(), action.c_str(), objectType.c_str(), objectId.c_str(),
                            detailsJson.c_str(), ip.c_str()};
    PGresult *res = PQexecParams(
        conn,
        "INSERT INTO audit_log (actor, action, object_type, object_id, details, ip)"
        " VALUES ($1, $2, $3, $4, $5::jsonb, NULLIF($6, ''))",
        6, nullptr, values, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) LOG_WARN << "аудит: " << PQerrorMessage(conn);
    PQclear(res);
    PQfinish(conn);
}

// Ретеншен журналов (ADR-59): удаляем записи старше N дней. Журналы только растут,
// поэтому глубина хранения — настройка, а не «когда-нибудь почистим руками».
struct PurgeResult {
    bool ok = false;
    long audit = 0;
    long runs = 0;
    std::string error;
};

PurgeResult purgeOldLogs(int days) {
    PurgeResult out;
    if (days <= 0) {
        out.ok = true;
        return out;
    }
    PGconn *conn = PQconnectdb(rep::connInfoFromEnv().c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        out.error = std::string(PQerrorMessage(conn));
        PQfinish(conn);
        return out;
    }
    const std::string daysText = std::to_string(days);
    const char *values[] = {daysText.c_str()};
    const auto run = [&](const char *sql) -> long {
        PGresult *res = PQexecParams(conn, sql, 1, nullptr, values, nullptr, nullptr, 0);
        long affected = -1;
        if (PQresultStatus(res) == PGRES_COMMAND_OK) {
            affected = std::strtol(PQcmdTuples(res), nullptr, 10);
        } else {
            out.error = std::string(PQerrorMessage(conn));
        }
        PQclear(res);
        return affected;
    };
    // журнал действий и журнал рассылки — обе таблицы с датой начала
    const long audit = run("DELETE FROM audit_log WHERE at < now() - ($1::int || ' days')::interval");
    const long runs = run("DELETE FROM subscription_runs WHERE started_at < now() - ($1::int || ' days')::interval");
    PQfinish(conn);
    if (audit < 0 || runs < 0) return out;
    out.ok = true;
    out.audit = audit;
    out.runs = runs;
    return out;
}

// Часовой пояс подписки (ADR-60). Локальное «наивное» время считаем в C++,
// а пересчёт местное ↔ UTC отдаём PostgreSQL: только база знает переходы на летнее
// время, самодельная арифметика на них ошибается дважды в год.
bool timezoneExists(PGconn *conn, const std::string &tz) {
    if (tz.empty()) return false;
    const char *values[] = {tz.c_str()};
    PGresult *res = PQexecParams(conn, "SELECT 1 FROM pg_timezone_names WHERE name = $1 LIMIT 1", 1, nullptr, values,
                                 nullptr, nullptr, 0);
    const bool ok = PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1;
    PQclear(res);
    return ok;
}

std::time_t nextRunInZone(PGconn *conn, const rep::Cron &cron, const std::string &tz, std::time_t afterEpoch) {
    const std::string epochText = std::to_string(static_cast<long long>(afterEpoch));
    const char *first[] = {epochText.c_str(), tz.c_str()};
    PGresult *res = PQexecParams(conn,
                                 "SELECT to_char(to_timestamp($1::bigint) AT TIME ZONE $2,"
                                 " 'YYYY-MM-DD\"T\"HH24:MI:SS')",
                                 2, nullptr, first, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1) {
        PQclear(res);
        return 0;
    }
    const std::string localText = PQgetvalue(res, 0, 0);
    PQclear(res);

    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(localText.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return 0;
    }
    std::tm local{};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    const std::time_t nextNaive = rep::nextRunNaive(cron, ::timegm(&local));
    if (nextNaive == 0) return 0;

    std::tm out = *::gmtime(&nextNaive);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:00", out.tm_year + 1900, out.tm_mon + 1,
                  out.tm_mday, out.tm_hour, out.tm_min);
    const char *toUtc[] = {buffer, tz.c_str()};
    res = PQexecParams(conn, "SELECT extract(epoch FROM ($1::timestamp AT TIME ZONE $2))::bigint", 2, nullptr, toUtc,
                       nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1) {
        PQclear(res);
        return 0;
    }
    const std::time_t nextUtc = static_cast<std::time_t>(std::stoll(PQgetvalue(res, 0, 0)));
    PQclear(res);
    return nextUtc;
}

std::string requestActor(const HttpRequestPtr &req) {
    if (!req) return "system";
    if (const auto attrs = req->attributes()) {
        const std::string login = attrs->get<std::string>("session_login");
        if (!login.empty()) return login;
    }
    return "anonymous";
}

std::string requestIp(const HttpRequestPtr &req) {
    if (!req) return "";
    return req->peerAddr().toIp();
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
    // Аутентификация: REP_AUTH=1 — сессии в cookie (ADR-34), REP_AUTH=proxy — личность
    // приходит от реверс-прокси в заголовке (ADR-54), иначе выключена.
    const rep::AuthMode authMode = rep::parseAuthMode(rep::envOr("REP_AUTH", "0"));
    const bool authEnabled = authMode != rep::AuthMode::Off;
    const bool proxyAuth = authMode == rep::AuthMode::Proxy;
    const std::string proxyHeader = rep::envOr("REP_AUTH_PROXY_HEADER", "X-Remote-User");
    // Кому верим: адрес, с которого пришёл запрос, должен быть в списке. Пустое
    // значение REP_AUTH_PROXY_TRUSTED означает «никому не верим» (никаких запросов).
    const auto proxyTrusted = rep::parseTrustedProxies(rep::envOr("REP_AUTH_PROXY_TRUSTED", "127.0.0.1,::1"));
    const bool proxyAutocreate = rep::envOr("REP_AUTH_PROXY_AUTOCREATE", "0") == "1";
    const std::string proxyDefaultRole = rep::envOr("REP_AUTH_PROXY_ROLE", "viewer");
    const int proxyCacheSeconds = std::stoi(rep::envOr("REP_AUTH_PROXY_CACHE_SECONDS", "60"));
    // Глубина хранения журналов (ADR-59): 0 — хранить бессрочно.
    const int logRetentionDays = std::stoi(rep::envOr("REP_LOG_RETENTION_DAYS", "90"));
    // Повторы неудачной отправки (ADR-61): сколько раз и с каким шагом (экспоненциально).
    const int smtpRetryAttempts = std::stoi(rep::envOr("REP_SMTP_RETRY_ATTEMPTS", "3"));
    const int smtpRetryDelaySeconds = std::stoi(rep::envOr("REP_SMTP_RETRY_DELAY_SECONDS", "60"));
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
    auto sqlGuard = [](const std::string &sql, std::string &err) { return rep::isReadOnlySql(sql, err); };

    // Полная проверка шаблона на сервере (ADR-45): те же правила, что в клиентском
    // validate.js, плюс SQL-гард — админский API не принимает шаблон, который
    // клиент просто не отправил на проверку.
    // Миграция до текущей версии + полная проверка (ADR-45, ADR-51).
    // normalized (если передан) получает шаблон в актуальной версии — его и сохраняем.
    auto prepareTemplate = [maxTemplateBytes](const std::string &raw, const std::string &urlId,
                                              std::string *normalized, std::vector<std::string> *steps) {
        rep::ValidationResult result;
        if (raw.size() > maxTemplateBytes) {
            result.errors.push_back({"(корень)", "шаблон больше " + std::to_string(maxTemplateBytes) + " байт"});
            return result;
        }
        Json::CharReaderBuilder rb;
        Json::Value root;
        std::string parseErr;
        std::istringstream in(raw);
        if (!Json::parseFromStream(rb, in, &root, &parseErr)) {
            result.errors.push_back({"(корень)", "шаблон не является JSON: " + parseErr});
            return result;
        }
        const auto migration = rep::migrateTemplate(root);
        if (!migration.ok) {
            result.errors.push_back({"version", migration.error});
            return result;
        }
        if (steps) *steps = migration.steps;
        if (normalized) {
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "  ";
            *normalized = Json::writeString(wb, root);
        }
        return rep::validateTemplateJson(root, urlId);
    };

    auto validateTemplateFull = [&prepareTemplate](const std::string &raw, const std::string &urlId) {
        return prepareTemplate(raw, urlId, nullptr, nullptr);
    };

    auto validateTemplate = [&validateTemplateFull](const std::string &raw, const std::string &urlId,
                                                    std::string &err) {
        const auto result = validateTemplateFull(raw, urlId);
        if (result.ok()) return true;
        const auto &first = result.errors.front();
        err = first.path == "(корень)" ? first.message : first.path + ": " + first.message;
        return false;
    };

    // Атомарная замена шаблона в памяти: сначала разбор, потом запись под мьютексом.
    auto applyTemplateInMemory = [&templates, &templatesRaw, &templatesMutex, &templatesVersion,
                                  &resultCache](const std::string &id, const std::string &raw,
                                                const std::string &title) {
        // тело могло быть сохранено в старой версии формата (файл, старый ряд в БД):
        // в память кладём уже мигрированное, чтобы клиент видел актуальный формат
        std::string body = raw;
        {
            Json::CharReaderBuilder rb;
            Json::Value doc;
            std::string parseErr;
            std::istringstream in(raw);
            if (Json::parseFromStream(rb, in, &doc, &parseErr)) {
                const auto migration = rep::migrateTemplate(doc);
                if (!migration.ok) return false;
                if (!migration.steps.empty()) {
                    Json::StreamWriterBuilder wb;
                    wb["indentation"] = "  ";
                    body = Json::writeString(wb, doc);
                }
            }
        }
        TemplateInfo info;
        if (!parseTemplate(body, id, info)) return false;
        info.id = id;
        info.title = title;
        {
            std::lock_guard<std::mutex> lock(templatesMutex);
            templates[id] = info;
            templatesRaw[id] = body;
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
        [db, adminEnabled, maxTemplateBytes, &validateTemplate, &prepareTemplate, &applyTemplateInMemory](
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
            std::string raw = Json::writeString(wb, tpl);
            std::string normalized;
            std::vector<std::string> steps;
            const auto prepared = prepareTemplate(raw, id, &normalized, &steps);
            if (!prepared.ok()) {
                const auto &first = prepared.errors.front();
                sendError(cb, k422UnprocessableEntity, "TEMPLATE_INVALID",
                          first.path == "(корень)" ? first.message : first.path + ": " + first.message);
                return;
            }
            raw = normalized; // сохраняем в актуальной версии формата
            const std::string title = tpl.get("title", id).asString();

            runQuery(
                db, "SELECT 1 FROM report_templates WHERE id = $1", {id},
                [cb, db, id, raw, title, &applyTemplateInMemory, req](const orm::Result &res) {
                    if (!res.empty()) {
                        sendError(cb, k409Conflict, "REPORT_EXISTS", "отчёт '" + id + "' уже существует");
                        return;
                    }
                    runQuery(
                        db, "INSERT INTO report_templates (id, title, body, updated_at) VALUES ($1, $2, $3::jsonb, now())",
                        {id, title, raw},
                        [cb, id, raw, title, &applyTemplateInMemory, req](const orm::Result &) {
                            if (!applyTemplateInMemory(id, raw, title)) {
                                sendError(cb, k422UnprocessableEntity, "TEMPLATE_INVALID", "шаблон не разбирается");
                                return;
                            }
                            auditEvent(requestActor(req), "template.create", "report", id, "{}", requestIp(req));
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
        [db, adminEnabled, &validateTemplate, &prepareTemplate, &applyTemplateInMemory](
            const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb, const std::string &id) {
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
            std::string normalized;
            std::vector<std::string> steps;
            const auto prepared = prepareTemplate(Json::writeString(wb, tpl), id, &normalized, &steps);
            if (!prepared.ok()) {
                const auto &first = prepared.errors.front();
                sendError(cb, k422UnprocessableEntity, "TEMPLATE_INVALID",
                          first.path == "(корень)" ? first.message : first.path + ": " + first.message);
                return;
            }
            const std::string raw = normalized;
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
                [cb, db, id, raw, title, &applyTemplateInMemory, req](const orm::Result &) {
                    runQuery(
                        db, "UPDATE report_templates SET title = $2, body = $3::jsonb, updated_at = now() WHERE id = $1",
                        {id, title, raw},
                        [cb, id, raw, title, &applyTemplateInMemory, req](const orm::Result &updated) {
                            if (updated.affectedRows() == 0) {
                                sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + id + "' не найден");
                                return;
                            }
                            if (!applyTemplateInMemory(id, raw, title)) {
                                sendError(cb, k422UnprocessableEntity, "TEMPLATE_INVALID", "шаблон не разбирается");
                                return;
                            }
                            auditEvent(requestActor(req), "template.update", "report", id, "{}", requestIp(req));
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
                [cb, db, id, &dropTemplateFromMemory, req](const orm::Result &) {
                    runQuery(
                        db, "DELETE FROM report_templates WHERE id = $1", {id},
                        [cb, id, &dropTemplateFromMemory, req](const orm::Result &del) {
                            dropTemplateFromMemory(id);
                            auditEvent(requestActor(req), "template.delete", "report", id, "{}", requestIp(req));
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
        [adminEnabled, &validateTemplateFull](const HttpRequestPtr &req,
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
            const auto result = validateTemplateFull(raw, id);
            Json::Value errors(Json::arrayValue);
            for (const auto &e : result.errors) {
                Json::Value item;
                item["path"] = e.path;
                item["message"] = e.message;
                errors.append(item);
            }
            Json::Value warnings(Json::arrayValue);
            for (const auto &w : result.warnings) {
                Json::Value item;
                item["path"] = w.path;
                item["message"] = w.message;
                warnings.append(item);
            }
            Json::Value out;
            out["ok"] = result.ok();
            out["errors"] = errors;
            out["warnings"] = warnings;
            if (!result.ok()) {
                const auto &first = result.errors.front();
                out["error"] = first.path == "(корень)" ? first.message : first.path + ": " + first.message;
            }
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
        [db, adminEnabled, &applyTemplateInMemory](const HttpRequestPtr &req,
                                                   std::function<void(const HttpResponsePtr &)> &&cb,
                                                   const std::string &id, const std::string &snapshot) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            runQuery(
                db, "SELECT title, body::text AS body FROM report_templates_history WHERE id = $1 AND report_id = $2",
                {snapshot, id},
                [cb, db, id, &applyTemplateInMemory, req, snapshot](const orm::Result &res) {
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
                        [cb, db, id, raw, title, &applyTemplateInMemory, req, snapshot](const orm::Result &) {
                            runQuery(
                                db, "UPDATE report_templates SET title = $2, body = $3::jsonb, updated_at = now() WHERE id = $1",
                                {id, title, raw},
                                [cb, id, raw, title, &applyTemplateInMemory, req, snapshot](const orm::Result &) {
                                    applyTemplateInMemory(id, raw, title);
                                    Json::Value out;
                                    out["id"] = id;
                                    auditEvent(requestActor(req), "template.rollback", "report", id,
                                               "{\"snapshot\":\"" + snapshot + "\"}", requestIp(req));
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
        [db, adminEnabled, &applyTemplateInMemory, &prepareTemplate](
            const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb, const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            runQuery(
                db, "SELECT draft::text AS draft FROM report_templates WHERE id = $1", {id},
                [cb, db, id, &applyTemplateInMemory, &prepareTemplate, req](const orm::Result &res) {
                    if (res.empty()) {
                        sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + id + "' не найден");
                        return;
                    }
                    if (res[0]["draft"].isNull()) {
                        sendError(cb, k409Conflict, "NO_DRAFT", "у отчёта нет черновика");
                        return;
                    }
                    std::string normalized;
                    std::vector<std::string> steps;
                    const auto prepared = prepareTemplate(res[0]["draft"].as<std::string>(), id, &normalized, &steps);
                    if (!prepared.ok()) {
                        const auto &first = prepared.errors.front();
                        sendError(cb, k422UnprocessableEntity, "TEMPLATE_INVALID",
                                  first.path == "(корень)" ? first.message : first.path + ": " + first.message);
                        return;
                    }
                    const std::string raw = normalized;
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
                        [cb, db, id, raw, title, &applyTemplateInMemory, req](const orm::Result &) {
                            runQuery(
                                db,
                                "UPDATE report_templates SET title = $2, body = $3::jsonb, updated_at = now(), "
                                "draft = NULL, draft_updated_at = NULL WHERE id = $1",
                                {id, title, raw},
                                [cb, id, raw, title, &applyTemplateInMemory, req](const orm::Result &) {
                                    applyTemplateInMemory(id, raw, title);
                                    Json::Value out;
                                    out["id"] = id;
                                    auditEvent(requestActor(req), "template.publish", "report", id, "{}", requestIp(req));
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

    // Режим прокси (ADR-54): личность приходит в заголовке от доверенного прокси.
    // Роль ищется в users; положительный ответ кэшируется на REP_AUTH_PROXY_CACHE_SECONDS,
    // чтобы не ходить в базу на каждый запрос (отключение пользователя видно не дольше TTL).
    struct ProxyRoleEntry {
        std::string role;
        std::chrono::steady_clock::time_point until;
    };
    auto proxyRoles = std::make_shared<std::map<std::string, ProxyRoleEntry>>();
    auto proxyRolesMutex = std::make_shared<std::mutex>();
    auto resolveProxyUser = [db, proxyAutocreate, proxyDefaultRole, proxyRoles, proxyRolesMutex, proxyCacheSeconds](
                                const std::string &login,
                                std::function<void(const std::string &role, const std::string &err)> done) {
        auto remember = [proxyRoles, proxyRolesMutex, proxyCacheSeconds, login](const std::string &role) {
            if (proxyCacheSeconds <= 0) return;
            std::lock_guard<std::mutex> lock(*proxyRolesMutex);
            (*proxyRoles)[login] = {role, std::chrono::steady_clock::now() + std::chrono::seconds(proxyCacheSeconds)};
        };
        if (proxyCacheSeconds > 0) {
            std::lock_guard<std::mutex> lock(*proxyRolesMutex);
            const auto it = proxyRoles->find(login);
            if (it != proxyRoles->end() && it->second.until > std::chrono::steady_clock::now()) {
                done(it->second.role, "");
                return;
            }
        }
        runQuery(
            db, "SELECT role FROM users WHERE login = $1 AND enabled", {login},
            [db, login, done, remember, proxyAutocreate, proxyDefaultRole](const orm::Result &res) {
                if (!res.empty()) {
                    const std::string role = res[0]["role"].as<std::string>();
                    remember(role);
                    done(role, "");
                    return;
                }
                if (!proxyAutocreate) {
                    done("", "пользователь прокси не найден в справочнике или отключён");
                    return;
                }
                // Автосоздание: пароль пустой, поэтому по паролю такой пользователь войти не может.
                runQuery(
                    db,
                    "INSERT INTO users (login, password_salt, password_hash, role) VALUES ($1, '', '', $2) "
                    "ON CONFLICT (login) DO UPDATE SET login = EXCLUDED.login RETURNING role, enabled",
                    {login, proxyDefaultRole},
                    [login, done, remember](const orm::Result &created) {
                        if (created.empty() || !created[0]["enabled"].as<bool>()) {
                            done("", "пользователь прокси отключён администратором");
                            return;
                        }
                        const std::string role = created[0]["role"].as<std::string>();
                        auditEvent("system", "user.autocreate", "user", login,
                                   "{\"role\":\"" + role + "\",\"source\":\"proxy\"}", "");
                        remember(role);
                        done(role, "");
                    },
                    [done](const std::exception_ptr &e) { done("", describeError(e)); });
            },
            [done](const std::exception_ptr &e) { done("", describeError(e)); });
    };

    // Проверка сессии до маршрутизации: один раз на запрос, роль зависит от метода и пути.
    if (authEnabled) {
        app().registerPreRoutingAdvice(
            [db, authMode, proxyAuth, proxyHeader, proxyTrusted, resolveProxyUser, &unauthorized, &forbidden](
                const HttpRequestPtr &req, AdviceCallback &&respond, AdviceChainCallback &&next) {
                const rep::Role need = rep::requiredRole(std::string(req->methodString()), req->path());
                if (need == rep::Role::Public) {
                    next();
                    return;
                }
                if (proxyAuth) {
                    const std::string peer = req->peerAddr().toIp();
                    if (!rep::isTrustedPeer(peer, proxyTrusted)) {
                        respond(forbidden("запрос не от доверенного прокси (" + peer + ")"));
                        return;
                    }
                    std::string login = req->getHeader(proxyHeader);
                    while (!login.empty() && (login.front() == ' ' || login.front() == '\t')) login.erase(login.begin());
                    while (!login.empty() && (login.back() == ' ' || login.back() == '\t')) login.pop_back();
                    if (login.empty()) {
                        respond(unauthorized("прокси не передал имя пользователя в заголовке " + proxyHeader));
                        return;
                    }
                    if (!rep::isValidProxyLogin(login)) {
                        respond(unauthorized("некорректное имя пользователя от прокси"));
                        return;
                    }
                    resolveProxyUser(
                        login,
                        [req, need, login, &forbidden, respond, next](const std::string &role,
                                                                     const std::string &err) {
                            if (!err.empty()) {
                                respond(forbidden(err));
                                return;
                            }
                            if (!rep::roleAtLeast(role, need)) {
                                respond(forbidden("недостаточно прав: роль " + role + " не подходит для действия"));
                                return;
                            }
                            req->attributes()->insert("session_role", role);
                            req->attributes()->insert("session_login", login);
                            req->attributes()->insert("session_source", std::string("proxy"));
                            next();
                        });
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
                    "WHERE s.token = $1 AND s.expires_at > now() AND u.enabled",
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
        [db, authMode, sessionTtlHours](const HttpRequestPtr &req,
                                        std::function<void(const HttpResponsePtr &)> &&cb) {
            if (authMode == rep::AuthMode::Off) {
                sendError(cb, k404NotFound, "AUTH_DISABLED", "аутентификация выключена (REP_AUTH=0)");
                return;
            }
            if (authMode == rep::AuthMode::Proxy) {
                sendError(cb, k400BadRequest, "AUTH_ON_PROXY",
                          "вход выполняет реверс-прокси: приложение получает имя пользователя в заголовке");
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
                db, "SELECT id, login, password_salt, password_hash, role, enabled FROM users WHERE login = $1",
                {login},
                [db, cb, login, password, sessionTtlHours, req](const orm::Result &res) {
                    if (res.empty() ||
                        !rep::verifyPassword(password, res[0]["password_salt"].as<std::string>(),
                                             res[0]["password_hash"].as<std::string>())) {
                        auditEvent(login, "auth.login_failed", "user", login, "{}", requestIp(req));
                        sendError(cb, k401Unauthorized, "BAD_CREDENTIALS", "неверный логин или пароль");
                        return;
                    }
                    if (!res[0]["enabled"].as<bool>()) {
                        sendError(cb, k403Forbidden, "USER_DISABLED", "пользователь отключён администратором");
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
                        [cb, token, login, role, sessionTtlHours, req](const orm::Result &) {
                            Cookie cookie("rep_session", token);
                            cookie.setPath("/");
                            cookie.setHttpOnly(true);
                            cookie.setSameSite(Cookie::SameSite::kLax);
                            cookie.setMaxAge(sessionTtlHours * 3600);
                            cookie.setSecure(false); // стенд локальный, https не используется

                            auditEvent(login, "auth.login", "user", login, "{}", requestIp(req));
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
        [db, authMode](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            if (authMode == rep::AuthMode::Proxy) {
                sendError(cb, k400BadRequest, "AUTH_ON_PROXY", "выход выполняет реверс-прокси");
                return;
            }
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
            if (authMode == rep::AuthMode::Off || token.empty()) {
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
        [db, authMode, proxyHeader](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            if (authMode == rep::AuthMode::Off) {
                Json::Value out;
                out["auth"] = false;
                cb(jsonResponse(out));
                return;
            }
            if (authMode == rep::AuthMode::Proxy) {
                // Путь /api/auth/** публичный (нужен странице входа), поэтому личность
                // здесь берём из заголовка напрямую — прокси проверил её сам.
                std::string login = req->getHeader(proxyHeader);
                while (!login.empty() && (login.front() == ' ' || login.front() == '\t')) login.erase(login.begin());
                while (!login.empty() && (login.back() == ' ' || login.back() == '\t')) login.pop_back();
                if (!rep::isValidProxyLogin(login)) {
                    sendError(cb, k401Unauthorized, "PROXY_USER_MISSING",
                              "прокси не передал корректное имя пользователя в заголовке " + proxyHeader);
                    return;
                }
                runQuery(
                    db, "SELECT role FROM users WHERE login = $1 AND enabled", {login},
                    [cb, login](const orm::Result &res) {
                        if (res.empty()) {
                            sendError(cb, k403Forbidden, "PROXY_USER_UNKNOWN",
                                      "пользователь прокси не найден в справочнике или отключён");
                            return;
                        }
                        Json::Value out;
                        out["auth"] = true;
                        out["mode"] = "proxy";
                        out["login"] = login;
                        out["role"] = res[0]["role"].as<std::string>();
                        cb(jsonResponse(out));
                    },
                    [cb](const std::exception_ptr &e) {
                        sendError(cb, k500InternalServerError, "AUTH_FAILED", describeError(e));
                    });
                return;
            }
            const std::string token = req->getCookie("rep_session");
            if (token.empty()) {
                sendError(cb, k401Unauthorized, "UNAUTHORIZED", "нужна авторизация");
                return;
            }
            runQuery(
                db,
                "SELECT u.login, u.role FROM sessions s JOIN users u ON u.id = s.user_id AND u.enabled "
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
            std::vector<std::size_t> groupColumns;
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
                if (col["group"].asBool()) groupColumns.push_back(leaves.size());
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
                [cb, leaves, title, bag, noDeclared, reportId, groupColumns](const orm::Result &res) {
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
                    rep::applyGrouping(sheet, groupColumns);
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
    // Общая сборка листа отчёта: один расчёт для CSV, XLSX и рассылки (ADR-46, ADR-48).
    // status 200 — sheet заполнен; иначе message содержит причину, code — машинный код.
    auto buildSheet = [dataDb, &templates, &templatesRaw, &templatesMutex](
                          const std::string &reportId, const std::string &requestedDataset,
                          const Json::Value &rawParams,
                          std::function<void(int status, const std::string &code,
                                             const rep::ExportSheet &sheet, const std::string &message)> done) {
        std::string datasetSql;
        std::map<std::string, std::string> datasetParams;
        std::string datasetKey;
        std::string raw;
        {
            std::lock_guard<std::mutex> lock(templatesMutex);
            const auto it = templates.find(reportId);
            if (it == templates.end()) {
                done(404, "REPORT_NOT_FOUND", rep::ExportSheet{}, "отчёт '" + reportId + "' не найден");
                return;
            }
            const auto &all = it->second.datasets;
            if (all.empty()) {
                datasetSql = it->second.datasetSql;
                datasetParams = it->second.datasetParams;
            } else {
                const auto ds = all.find(requestedDataset.empty() ? all.begin()->first : requestedDataset);
                if (ds == all.end()) {
                    done(422, "DATASET_NOT_FOUND", rep::ExportSheet{}, "в шаблоне нет набора '" + requestedDataset + "'");
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
            done(422, "INVALID_PARAMS", rep::ExportSheet{}, err);
            return;
        }

        Json::Value tpl;
        if (!raw.empty()) {
            Json::CharReaderBuilder reader;
            std::string errs;
            std::istringstream in(raw);
            Json::parseFromStream(reader, in, &tpl, &errs);
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
            std::string numFmt;
        };
        std::vector<Leaf> leaves;
        std::vector<std::size_t> groupColumns;
        std::function<void(const Json::Value &)> walk = [&](const Json::Value &col) {
            if (col["children"].isArray()) {
                for (const auto &child : col["children"]) walk(child);
                return;
            }
            const std::string fmt = col["format"].isString() ? col["format"].asString() : "";
            const bool numeric = fmt == "number" || fmt == "integer" || fmt == "percent" ||
                                 fmt == "currency" || col.isMember("aggregate");
            const std::string numFmt = fmt == "percent" ? "0.00%" : numeric ? "# ##0" : "";
            if (col["group"].asBool()) groupColumns.push_back(leaves.size());
            leaves.push_back({col.get("field", "").asString(), col.get("title", "").asString(), numeric, numFmt});
        };
        for (const auto &col : block["columns"]) walk(col);

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
                done(422, "INVALID_PARAMS", rep::ExportSheet{}, aggErr);
                return;
            }
        }

        const std::string sheetTitle = tpl.get("title", "").asString();
        static const std::map<std::string, std::string> noDeclared;
        runQuery(
            dataDb, finalSql, query.values,
            [done, leaves, bag, noDeclared, sheetTitle, groupColumns](const orm::Result &res) {
                const auto ncols = static_cast<size_t>(res.columns());
                std::vector<std::string> names;
                std::vector<std::string> types;
                for (size_t c = 0; c < ncols; ++c) {
                    names.emplace_back(res.columnName(static_cast<unsigned long>(c)));
                    types.push_back(columnType(names.back(), noDeclared, res));
                }
                rep::ExportSheet sheet;
                sheet.title = sheetTitle;
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
                rep::applyGrouping(sheet, groupColumns);
                done(200, "", sheet, "");
            },
            [done](const std::exception_ptr &e) { done(500, "EXPORT_FAILED", rep::ExportSheet{}, describeError(e)); });
    };

    // CSV и XLSX строятся из одного листа: письмо и скачивание не расходятся.
    auto buildCsv = [buildSheet](const std::string &reportId, const std::string &requestedDataset,
                                 const Json::Value &rawParams,
                                 std::function<void(int, const std::string &, const std::string &, int)> done) {
        buildSheet(reportId, requestedDataset, rawParams,
                   [done](int status, const std::string &code, const rep::ExportSheet &sheet,
                          const std::string &message) {
                       if (status != 200) {
                           done(status, code, message, 0);
                           return;
                       }
                       done(200, "", rep::toCsv(sheet), static_cast<int>(sheet.rows.size()));
                   });
    };

    auto buildXlsx = [buildSheet](const std::string &reportId, const std::string &requestedDataset,
                                  const Json::Value &rawParams,
                                  std::function<void(int, const std::string &, const std::string &, int)> done) {
        buildSheet(reportId, requestedDataset, rawParams,
                   [done, reportId](int status, const std::string &code, const rep::ExportSheet &sheet,
                                    const std::string &message) {
                       if (status != 200) {
                           done(status, code, message, 0);
                           return;
                       }
                       const std::string dir = rep::envOr("TMPDIR", "/tmp");
                       const std::string path = dir + "/rep-mail-" + reportId + "-" +
                                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                                ".xlsx";
                       std::string werr;
                       if (!rep::writeXlsx(path, sheet, werr)) {
                           done(500, "EXPORT_FAILED", werr, 0);
                           return;
                       }
                       std::ifstream file(path, std::ios::binary);
                       std::stringstream buffer;
                       buffer << file.rdbuf();
                       file.close();
                       std::remove(path.c_str());
                       done(200, "", buffer.str(), static_cast<int>(sheet.rows.size()));
                   });
    };

    app().registerHandler(
        "/api/report/export.csv",
        [&buildCsv](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
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
            buildCsv(reportId, (*body).get("dataset", "").asString(), (*body)["params"],
                     [cb, reportId](int status, const std::string &code, const std::string &payload, int) {
                         if (status != 200) {
                             sendError(cb, static_cast<HttpStatusCode>(status), code, payload);
                             return;
                         }
                         auto resp = HttpResponse::newHttpResponse();
                         resp->setContentTypeString("text/csv; charset=utf-8");
                         resp->addHeader("Content-Disposition", "attachment; filename=\"" + reportId + ".csv\"");
                         resp->setBody(payload);
                         cb(resp);
                     });
        },
        {Post});



    // --- Пользователи и роли (ADR-50) ---------------------------------------
    // Пароли хэшируются тем же PBKDF2, что и при входе (auth.cc). Отключение и смена
    // пароля гасят сессии пользователя; последнего включённого администратора нельзя
    // ни отключить, ни понизить, ни удалить — иначе сервер останется без управления.
    auto userJson = [](PGresult *res) {
        Json::Value list(Json::arrayValue);
        const int count = PQntuples(res);
        for (int i = 0; i < count; ++i) {
            Json::Value item;
            item["id"] = static_cast<Json::Int64>(std::stoll(PQgetvalue(res, i, 0)));
            item["login"] = PQgetvalue(res, i, 1);
            item["role"] = PQgetvalue(res, i, 2);
            item["enabled"] = std::string(PQgetvalue(res, i, 3)) == "t";
            item["createdAt"] = PQgetisnull(res, i, 4) ? "" : PQgetvalue(res, i, 4);
            item["updatedAt"] = PQgetisnull(res, i, 5) ? "" : PQgetvalue(res, i, 5);
            list.append(item);
        }
        return list;
    };

    app().registerHandler(
        "/api/admin/users",
        [adminEnabled, userJson](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            PGconn *conn = PQconnectdb(rep::connInfoFromEnv().c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                const std::string msg = PQerrorMessage(conn);
                PQfinish(conn);
                sendError(cb, k503ServiceUnavailable, "DB_UNAVAILABLE", msg);
                return;
            }
            if (req->method() == Get) {
                PGresult *res = PQexec(conn,
                                       "SELECT id, login, role, enabled,"
                                       " to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'),"
                                       " to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')"
                                       " FROM users ORDER BY id");
                if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                    const std::string msg = PQerrorMessage(conn);
                    PQclear(res);
                    PQfinish(conn);
                    sendError(cb, k500InternalServerError, "QUERY_FAILED", msg);
                    return;
                }
                Json::Value out;
                out["items"] = userJson(res);
                PQclear(res);
                PQfinish(conn);
                cb(jsonResponse(out));
                return;
            }

            // создание пользователя
            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                PQfinish(conn);
                sendError(cb, k400BadRequest, "INVALID_PARAMS", "ожидался JSON-объект");
                return;
            }
            const std::string login = (*body).get("login", "").asString();
            const std::string role = (*body).get("role", "viewer").asString();
            const std::string password = (*body).get("password", "").asString();
            std::string err;
            if (!rep::validateUserInput(login, role, password, true, err)) {
                PQfinish(conn);
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", err);
                return;
            }
            const std::string salt = rep::randomHex(16);
            const std::string hash = rep::hashPassword(password, salt);
            const std::string enabled = (*body).get("enabled", true).asBool() ? "true" : "false";
            const char *values[] = {login.c_str(), salt.c_str(), hash.c_str(), role.c_str(), enabled.c_str()};
            PGresult *res = PQexecParams(
                conn,
                "INSERT INTO users (login, password_salt, password_hash, role, enabled)"
                " VALUES ($1, $2, $3, $4, $5::boolean) RETURNING id",
                5, nullptr, values, nullptr, nullptr, 0);
            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                const std::string msg = PQerrorMessage(conn);
                PQclear(res);
                PQfinish(conn);
                const bool duplicate = msg.find("users_login_key") != std::string::npos;
                sendError(cb, duplicate ? k409Conflict : k422UnprocessableEntity,
                          duplicate ? "LOGIN_TAKEN" : "INVALID_PARAMS",
                          duplicate ? "логин «" + login + "» уже занят" : msg);
                return;
            }
            const std::string newId = PQgetvalue(res, 0, 0);
            auditEvent(requestActor(req), "user.create", "user", newId,
                       "{\"login\":\"" + login + "\",\"role\":\"" + role + "\",\"enabled\":" +
                           (enabled == "true" ? "true" : "false") + "}",
                       requestIp(req));
            Json::Value out;
            out["id"] = static_cast<Json::Int64>(std::stoll(newId));
            PQclear(res);
            PQfinish(conn);
            cb(jsonResponse(out));
        },
        {Get, Post});

    app().registerHandler(
        "/api/admin/users/{id}",
        [adminEnabled](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb,
                       const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            PGconn *conn = PQconnectdb(rep::connInfoFromEnv().c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                const std::string msg = PQerrorMessage(conn);
                PQfinish(conn);
                sendError(cb, k503ServiceUnavailable, "DB_UNAVAILABLE", msg);
                return;
            }
            const auto finish = [conn, &cb](HttpStatusCode status, const std::string &code, const std::string &msg) {
                PQfinish(conn);
                sendError(cb, status, code, msg);
            };

            // кто выполняет действие: себя удалять/отключать нельзя
            std::string actor;
            if (const auto attrs = req->attributes()) actor = attrs->get<std::string>("session_login");

            if (req->method() == Delete) {
                const char *values[] = {id.c_str()};
                PGresult *res = PQexecParams(
                    conn,
                    "SELECT login, role, enabled FROM users WHERE id = $1", 1, nullptr, values, nullptr, nullptr, 0);
                if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1) {
                    PQclear(res);
                    finish(k404NotFound, "USER_NOT_FOUND", "пользователь не найден");
                    return;
                }
                const std::string login = PQgetvalue(res, 0, 0);
                const std::string role = PQgetvalue(res, 0, 1);
                const bool enabled = std::string(PQgetvalue(res, 0, 2)) == "t";
                PQclear(res);
                if (!actor.empty() && login == actor) {
                    finish(k409Conflict, "SELF_PROTECTED", "нельзя удалить собственную учётную запись");
                    return;
                }
                if (role == "admin" && enabled) {
                    PGresult *admins = PQexec(conn, "SELECT count(*) FROM users WHERE role = 'admin' AND enabled");
                    const long count = std::stol(PQgetvalue(admins, 0, 0));
                    PQclear(admins);
                    if (count <= 1) {
                        finish(k409Conflict, "LAST_ADMIN", "нельзя удалить последнего администратора");
                        return;
                    }
                }
                PGresult *del = PQexecParams(conn, "DELETE FROM users WHERE id = $1", 1, nullptr, values, nullptr,
                                             nullptr, 0);
                const bool ok = PQresultStatus(del) == PGRES_COMMAND_OK;
                const std::string msg = ok ? "" : PQerrorMessage(conn);
                PQclear(del);
                PQfinish(conn);
                if (!ok) {
                    sendError(cb, k500InternalServerError, "DELETE_FAILED", msg);
                    return;
                }
                auditEvent(requestActor(req), "user.delete", "user", id,
                           "{\"login\":\"" + login + "\",\"role\":\"" + role + "\"}", requestIp(req));
                Json::Value out;
                out["deleted"] = true;
                cb(jsonResponse(out));
                return;
            }

            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                finish(k400BadRequest, "INVALID_PARAMS", "ожидался JSON-объект");
                return;
            }
            const std::string role = (*body).get("role", "viewer").asString();
            const std::string password = (*body).get("password", "").asString();
            const bool enabled = (*body).get("enabled", true).asBool();

            // текущее состояние: логин для проверок и защиты последнего администратора
            const char *values[] = {id.c_str()};
            PGresult *cur = PQexecParams(conn, "SELECT login, role, enabled FROM users WHERE id = $1", 1, nullptr,
                                         values, nullptr, nullptr, 0);
            if (PQresultStatus(cur) != PGRES_TUPLES_OK || PQntuples(cur) != 1) {
                PQclear(cur);
                finish(k404NotFound, "USER_NOT_FOUND", "пользователь не найден");
                return;
            }
            const std::string login = PQgetvalue(cur, 0, 0);
            const std::string oldRole = PQgetvalue(cur, 0, 1);
            const bool oldEnabled = std::string(PQgetvalue(cur, 0, 2)) == "t";
            PQclear(cur);

            std::string err;
            if (!rep::validateUserInput(login, role, password, false, err)) {
                finish(k422UnprocessableEntity, "INVALID_PARAMS", err);
                return;
            }
            const bool losesAdmin = oldRole == "admin" && oldEnabled && (role != "admin" || !enabled);
            if (losesAdmin) {
                PGresult *admins = PQexec(conn, "SELECT count(*) FROM users WHERE role = 'admin' AND enabled");
                const long count = std::stol(PQgetvalue(admins, 0, 0));
                PQclear(admins);
                if (count <= 1) {
                    finish(k409Conflict, "LAST_ADMIN", "нельзя оставить сервер без администратора");
                    return;
                }
            }
            if (!actor.empty() && login == actor && (!enabled || role != oldRole)) {
                finish(k409Conflict, "SELF_PROTECTED", "нельзя отключить себя или сменить себе роль");
                return;
            }

            // пароль меняем только если передан: пустая строка — «не трогать»
            if (!password.empty()) {
                const std::string salt = rep::randomHex(16);
                const std::string hash = rep::hashPassword(password, salt);
                const char *upd[] = {id.c_str(), salt.c_str(), hash.c_str(), role.c_str(),
                                     enabled ? "true" : "false"};
                PGresult *res = PQexecParams(
                    conn,
                    "UPDATE users SET password_salt = $2, password_hash = $3, role = $4, enabled = $5::boolean,"
                    " updated_at = now() WHERE id = $1",
                    5, nullptr, upd, nullptr, nullptr, 0);
                const bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
                const std::string msg = ok ? "" : PQerrorMessage(conn);
                PQclear(res);
                if (!ok) {
                    finish(k500InternalServerError, "UPDATE_FAILED", msg);
                    return;
                }
                // смена пароля гасит старые сессии
                const char *del[] = {id.c_str()};
                PGresult *sessions = PQexecParams(conn, "DELETE FROM sessions WHERE user_id = $1::bigint", 1, nullptr,
                                                  del, nullptr, nullptr, 0);
                PQclear(sessions);
            } else {
                const char *upd[] = {id.c_str(), role.c_str(), enabled ? "true" : "false"};
                PGresult *res = PQexecParams(
                    conn,
                    "UPDATE users SET role = $2, enabled = $3::boolean, updated_at = now() WHERE id = $1",
                    3, nullptr, upd, nullptr, nullptr, 0);
                const bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
                const std::string msg = ok ? "" : PQerrorMessage(conn);
                PQclear(res);
                if (!ok) {
                    finish(k500InternalServerError, "UPDATE_FAILED", msg);
                    return;
                }
                if (!enabled) {
                    const char *del[] = {id.c_str()};
                    PGresult *sessions = PQexecParams(conn, "DELETE FROM sessions WHERE user_id = $1::bigint", 1,
                                                      nullptr, del, nullptr, nullptr, 0);
                    PQclear(sessions);
                }
            }
            auditEvent(requestActor(req), "user.update", "user", id,
                       "{\"login\":\"" + login + "\",\"role\":\"" + role + "\",\"enabled\":" +
                           (enabled ? "true" : "false") +
                           ",\"passwordChanged\":" + (password.empty() ? "false" : "true") + "}",
                       requestIp(req));
            PQfinish(conn);
            Json::Value out;
            out["updated"] = true;
            cb(jsonResponse(out));
        },
        {Put, Delete});

    // --- Рассылка отчётов (ADR-46) ------------------------------------------
    // Подписки хранятся в report_subscriptions; планировщик раз в 30 с выбирает
    // просроченные и отправляет CSV письмом. Соединение с БД — своё (libpq),
    // потому что отправка блокирующая и не должна занимать цикл событий.
    const std::string smtpHost = rep::envOr("REP_SMTP_HOST", "");
    const int smtpPort = std::stoi(rep::envOr("REP_SMTP_PORT", "25"));
    const std::string smtpFrom = rep::envOr("REP_SMTP_FROM", "reports@localhost");
    const bool smtpStartTls = rep::envOr("REP_SMTP_TLS", "1") == "1";
    const bool schedulerEnabled = rep::envOr("REP_SCHEDULER", "1") == "1";

    auto smtpConfig = [smtpHost, smtpPort, smtpFrom, smtpStartTls]() {
        rep::MailConfig config;
        config.host = smtpHost;
        config.port = smtpPort;
        config.from = smtpFrom;
        config.user = rep::envOr("REP_SMTP_USER", "");
        config.password = rep::envOr("REP_SMTP_PASSWORD", "");
        config.starttls = smtpStartTls;
        return config;
    };

    // «{a@b,c@d}» → список адресов
    auto parseTextArray = [](const std::string &value) {
        std::vector<std::string> out;
        if (value.size() < 2) return out;
        std::string current;
        bool quoted = false;
        for (std::size_t i = 1; i + 1 < value.size(); ++i) {
            const char c = value[i];
            if (c == '"') {
                quoted = !quoted;
                continue;
            }
            if (c == ',' && !quoted) {
                if (!current.empty()) out.push_back(current);
                current.clear();
                continue;
            }
            if (c == '\\' && quoted && i + 2 < value.size()) {
                current.push_back(value[++i]);
                continue;
            }
            current.push_back(c);
        }
        if (!current.empty()) out.push_back(current);
        return out;
    };

    auto jsonSubscriptions = [parseTextArray](PGresult *res) {
        Json::Value list(Json::arrayValue);
        const int rows = PQntuples(res);
        for (int i = 0; i < rows; ++i) {
            Json::Value item;
            item["id"] = static_cast<Json::Int64>(std::stoll(PQgetvalue(res, i, 0)));
            item["reportId"] = PQgetvalue(res, i, 1);
            item["title"] = PQgetvalue(res, i, 2);
            item["cron"] = PQgetvalue(res, i, 3);
            Json::Value params;
            Json::CharReaderBuilder reader;
            std::string errs;
            std::istringstream in(PQgetvalue(res, i, 4));
            Json::parseFromStream(reader, in, &params, &errs);
            item["params"] = params;
            item["format"] = PQgetvalue(res, i, 5);
            Json::Value recipients(Json::arrayValue);
            for (const auto &mail : parseTextArray(PQgetvalue(res, i, 6))) recipients.append(mail);
            item["recipients"] = recipients;
            item["enabled"] = std::string(PQgetvalue(res, i, 7)) == "t";
            item["nextRunAt"] = PQgetisnull(res, i, 8) ? "" : PQgetvalue(res, i, 8);
            item["lastRunAt"] = PQgetisnull(res, i, 9) ? "" : PQgetvalue(res, i, 9);
            item["lastOk"] = PQgetisnull(res, i, 10) ? Json::Value() : Json::Value(std::string(PQgetvalue(res, i, 10)) == "t");
            item["lastError"] = PQgetisnull(res, i, 11) ? "" : PQgetvalue(res, i, 11);
            item["timezone"] = PQgetvalue(res, i, 12);
            item["retryAt"] = PQgetvalue(res, i, 13);
            item["retryCount"] = std::atoi(PQgetvalue(res, i, 14));
            list.append(item);
        }
        return list;
    };

    const std::string subscriptionSelect =
        "SELECT s.id, s.report_id, s.title, s.cron, s.params::text, s.format, s.recipients::text, s.enabled,"
        " to_char(s.next_run_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'),"
        " to_char(s.last_run_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'),"
        " r.ok, r.error, s.timezone,"
        " coalesce(to_char(s.retry_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), ''),"
        " s.retry_count"
        " FROM report_subscriptions s"
        " LEFT JOIN LATERAL (SELECT ok, error FROM subscription_runs WHERE subscription_id = s.id"
        "                    ORDER BY started_at DESC LIMIT 1) r ON true";

    // проверка полей подписки: cron, получатели, существование отчёта, часовой пояс
    auto validateSubscription = [&templates, &templatesMutex](PGconn *conn, const Json::Value &body, std::string &err,
                                                              rep::Cron &cron) {
        const std::string cronExpr = body.get("cron", "").asString();
        if (!rep::parseCron(cronExpr, cron, err)) return false;
        const Json::Value &recipients = body["recipients"];
        if (!recipients.isArray() || recipients.empty()) {
            err = "нужен непустой список recipients";
            return false;
        }
        for (const auto &mail : recipients) {
            const std::string address = mail.asString();
            const auto at = address.find('@');
            if (!mail.isString() || at == std::string::npos || at == 0 || at + 1 >= address.size() ||
                address.find(' ') != std::string::npos) {
                err = "некорректный адрес получателя: «" + address + "»";
                return false;
            }
        }
        const std::string format = body.get("format", "csv").asString();
        if (format != "csv" && format != "xlsx") {
            err = "format должен быть csv или xlsx";
            return false;
        }
        const std::string reportId = body.get("reportId", "").asString();
        if (reportId.empty()) {
            err = "нужен reportId";
            return false;
        }
        const std::string timezone = body.get("timezone", "UTC").asString();
        if (!timezoneExists(conn, timezone)) {
            err = "неизвестный часовой пояс «" + timezone + "»: нужен IANA-идентификатор (Europe/Moscow)";
            return false;
        }
        std::lock_guard<std::mutex> lock(templatesMutex);
        if (!templates.count(reportId)) {
            err = "отчёт '" + reportId + "' не найден";
            return false;
        }
        return true;
    };

    app().registerHandler(
        "/api/admin/subscriptions",
        [adminEnabled, &templates, &templatesMutex, subscriptionSelect, jsonSubscriptions,
         validateSubscription](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            PGconn *conn = PQconnectdb(rep::connInfoFromEnv().c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                const std::string msg = PQerrorMessage(conn);
                PQfinish(conn);
                sendError(cb, k503ServiceUnavailable, "DB_UNAVAILABLE", msg);
                return;
            }
            if (req->method() == Get) {
                PGresult *res = PQexec(conn, (subscriptionSelect + " ORDER BY s.id").c_str());
                if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                    const std::string msg = PQerrorMessage(conn);
                    PQclear(res);
                    PQfinish(conn);
                    sendError(cb, k500InternalServerError, "QUERY_FAILED", msg);
                    return;
                }
                Json::Value out;
                out["items"] = jsonSubscriptions(res);
                PQclear(res);
                PQfinish(conn);
                cb(jsonResponse(out));
                return;
            }

            // создание: POST /api/admin/subscriptions
            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                PQfinish(conn);
                sendError(cb, k400BadRequest, "INVALID_PARAMS", "ожидался JSON-объект");
                return;
            }
            std::string err;
            rep::Cron cron;
            if (!validateSubscription(conn, *body, err, cron)) {
                PQfinish(conn);
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", err);
                return;
            }
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "";
            const std::string paramsJson = Json::writeString(wb, (*body)["params"]);
            Json::StreamWriterBuilder rb;
            const std::string recipientsJson = Json::writeString(rb, (*body)["recipients"]);
            const std::string timezone = (*body).get("timezone", "UTC").asString();
            const std::time_t next = nextRunInZone(conn, cron, timezone, std::time(nullptr));
            const std::string nextText = std::to_string(next);
            const std::string format = (*body).get("format", "csv").asString();
            const bool enabled = (*body).get("enabled", true).asBool();

            const char *sql =
                "INSERT INTO report_subscriptions (report_id, title, cron, params, format, recipients, enabled,"
                " next_run_at, timezone)"
                " VALUES ($1, $2, $3, $4::jsonb, $5, (SELECT array_agg(value::text) FROM json_array_elements_text($6::json)),"
                "         $7, to_timestamp($8), $9) RETURNING id";
            const std::string values[] = {(*body).get("reportId", "").asString(),
                                          (*body).get("title", (*body).get("reportId", "")).asString(),
                                          (*body).get("cron", "").asString(), paramsJson, format,
                                          recipientsJson, enabled ? "true" : "false", nextText, timezone};
            const char *params[9];
            for (int i = 0; i < 9; ++i) params[i] = values[i].c_str();
            PGresult *res = PQexecParams(conn, sql, 9, nullptr, params, nullptr, nullptr, 0);
            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                const std::string msg = PQerrorMessage(conn);
                PQclear(res);
                PQfinish(conn);
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", msg);
                return;
            }
            auditEvent(requestActor(req), "subscription.create", "subscription", PQgetvalue(res, 0, 0),
                       "{\"report\":\"" + (*body)["reportId"].asString() + "\",\"cron\":\"" + (*body)["cron"].asString() +
                           "\",\"format\":\"" + format + "\",\"timezone\":\"" + timezone + "\"}",
                       requestIp(req));
            Json::Value out;
            out["id"] = static_cast<Json::Int64>(std::stoll(PQgetvalue(res, 0, 0)));
            out["nextRunAt"] = nextText;
            out["timezone"] = timezone;
            PQclear(res);
            PQfinish(conn);
            cb(jsonResponse(out));
        },
        {Get, Post});

    app().registerHandler(
        "/api/admin/subscriptions/{id}",
        [adminEnabled, &templates, &templatesMutex, validateSubscription](
            const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb, const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            PGconn *conn = PQconnectdb(rep::connInfoFromEnv().c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                const std::string msg = PQerrorMessage(conn);
                PQfinish(conn);
                sendError(cb, k503ServiceUnavailable, "DB_UNAVAILABLE", msg);
                return;
            }
            if (req->method() == Delete) {
                const char *values[] = {id.c_str()};
                PGresult *res = PQexecParams(conn, "DELETE FROM report_subscriptions WHERE id = $1", 1, nullptr,
                                             values, nullptr, nullptr, 0);
                const bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
                const std::string msg = ok ? "" : PQerrorMessage(conn);
                PQclear(res);
                PQfinish(conn);
                if (!ok) {
                    sendError(cb, k500InternalServerError, "DELETE_FAILED", msg);
                    return;
                }
                auditEvent(requestActor(req), "subscription.delete", "subscription", id, "{}", requestIp(req));
                Json::Value out;
                out["deleted"] = true;
                cb(jsonResponse(out));
                return;
            }

            const auto body = req->getJsonObject();
            if (!body || !body->isObject()) {
                PQfinish(conn);
                sendError(cb, k400BadRequest, "INVALID_PARAMS", "ожидался JSON-объект");
                return;
            }
            std::string err;
            rep::Cron cron;
            if (!validateSubscription(conn, *body, err, cron)) {
                PQfinish(conn);
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", err);
                return;
            }
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "";
            const std::string paramsJson = Json::writeString(wb, (*body)["params"]);
            const std::string recipientsJson = Json::writeString(wb, (*body)["recipients"]);
            const std::string timezone = (*body).get("timezone", "UTC").asString();
            const std::time_t next = nextRunInZone(conn, cron, timezone, std::time(nullptr));
            const std::string nextText = std::to_string(next);
            const std::string values[] = {id,
                                          (*body).get("reportId", "").asString(),
                                          (*body).get("title", (*body).get("reportId", "")).asString(),
                                          (*body).get("cron", "").asString(),
                                          paramsJson,
                                          (*body).get("format", "csv").asString(),
                                          recipientsJson,
                                          (*body).get("enabled", true).asBool() ? "true" : "false",
                                          nextText,
                                          timezone};
            const char *params[10];
            for (int i = 0; i < 10; ++i) params[i] = values[i].c_str();
            const char *sql =
                "UPDATE report_subscriptions SET report_id = $2, title = $3, cron = $4, params = $5::jsonb,"
                " format = $6, recipients = (SELECT array_agg(value::text) FROM json_array_elements_text($7::json)),"
                " enabled = $8::boolean, next_run_at = to_timestamp($9), timezone = $10,"
                " retry_count = 0, retry_at = NULL, updated_at = now()"
                " WHERE id = $1 RETURNING id";
            PGresult *res = PQexecParams(conn, sql, 10, nullptr, params, nullptr, nullptr, 0);
            const bool ok = PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1;
            const std::string msg = ok ? "" : PQerrorMessage(conn);
            PQclear(res);
            PQfinish(conn);
            if (!ok) {
                sendError(cb, k404NotFound, "SUBSCRIPTION_NOT_FOUND", msg.empty() ? "подписка не найдена" : msg);
                return;
            }
            auditEvent(requestActor(req), "subscription.update", "subscription", id,
                       "{\"report\":\"" + (*body)["reportId"].asString() + "\",\"cron\":\"" +
                           (*body)["cron"].asString() + "\"}",
                       requestIp(req));
            Json::Value out;
            out["updated"] = true;
            out["nextRunAt"] = nextText;
            cb(jsonResponse(out));
        },
        {Put, Delete});


    // --- Журнал аудита (ADR-52) ---------------------------------------------
    // Только чтение: записи создаются в местах действий, править их через API нельзя.
    app().registerHandler(
        "/api/admin/audit",
        [adminEnabled](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            PGconn *conn = PQconnectdb(rep::connInfoFromEnv().c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                const std::string msg = PQerrorMessage(conn);
                PQfinish(conn);
                sendError(cb, k503ServiceUnavailable, "DB_UNAVAILABLE", msg);
                return;
            }
            const std::string limitText = req->getParameter("limit").empty() ? "50" : req->getParameter("limit");
            const std::string offsetText = req->getParameter("offset").empty() ? "0" : req->getParameter("offset");
            const std::string action = req->getParameter("action");
            const std::string actor = req->getParameter("actor");

            const char *values[] = {limitText.c_str(), offsetText.c_str(), action.c_str(), actor.c_str()};
            PGresult *res = PQexecParams(
                conn,
                "SELECT id, to_char(at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), actor, action,"
                " object_type, object_id, details::text, coalesce(ip, ''), count(*) OVER ()"
                " FROM audit_log"
                " WHERE ($3 = '' OR action = $3) AND ($4 = '' OR actor = $4)"
                " ORDER BY at DESC, id DESC LIMIT LEAST($1::int, 200) OFFSET $2::int",
                4, nullptr, values, nullptr, nullptr, 0);
            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                const std::string msg = PQerrorMessage(conn);
                PQclear(res);
                PQfinish(conn);
                sendError(cb, k500InternalServerError, "QUERY_FAILED", msg);
                return;
            }
            Json::Value items(Json::arrayValue);
            long total = 0;
            const int count = PQntuples(res);
            for (int i = 0; i < count; ++i) {
                Json::Value item;
                item["id"] = static_cast<Json::Int64>(std::stoll(PQgetvalue(res, i, 0)));
                item["at"] = PQgetvalue(res, i, 1);
                item["actor"] = PQgetvalue(res, i, 2);
                item["action"] = PQgetvalue(res, i, 3);
                item["objectType"] = PQgetvalue(res, i, 4);
                item["objectId"] = PQgetvalue(res, i, 5);
                Json::Value details;
                Json::CharReaderBuilder reader;
                std::string errs;
                std::istringstream in(PQgetvalue(res, i, 6));
                Json::parseFromStream(reader, in, &details, &errs);
                item["details"] = details;
                item["ip"] = PQgetvalue(res, i, 7);
                total = std::stol(PQgetvalue(res, i, 8));
                items.append(item);
            }
            PQclear(res);
            PQfinish(conn);
            Json::Value out;
            out["items"] = items;
            out["total"] = static_cast<Json::Int64>(total);
            cb(jsonResponse(out));
        },
        {Get});

    // SSE: сервер сам сообщает об изменении шаблона (ADR-44).
    app().registerHandler(
        "/api/report/events",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            const std::string reportId = req->getParameter("report");
            if (reportId.empty()) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", "нужен параметр report");
                return;
            }

            const uint64_t clientId = sseNextId++;
            auto resp = HttpResponse::newAsyncStreamResponse(
                [reportId, clientId](ResponseStreamPtr stream) {
                    auto shared = std::shared_ptr<drogon::ResponseStream>(std::move(stream));
                    sseWrite(shared, ": connected\n\n");
                    {
                        std::lock_guard<std::mutex> lock(sseMutex);
                        sseClients[reportId][clientId] = shared;
                    }
                    if (!sseHeartbeatStarted.exchange(true)) {
                        app().getLoop()->runEvery(15.0, sseHeartbeat);
                    }
                },
                true);
            resp->setContentTypeString("text/event-stream; charset=utf-8");
            resp->addHeader("Cache-Control", "no-cache");
            resp->addHeader("Connection", "keep-alive");
            resp->addHeader("X-Accel-Buffering", "no");
            cb(resp);
        },
        {Get});

    app().registerHandler(
        "/api/health",
        [db, maxRows, authMode, logRetentionDays, &dbRo](const HttpRequestPtr &,
                                                         std::function<void(const HttpResponsePtr &)> &&cb) {
            runQuery(
                db, "SELECT 1", {},
                [cb, maxRows, authMode, logRetentionDays, &dbRo](const orm::Result &res) {
                    Json::Value body;
                    body["status"] = "ok";
                    body["schemaVersion"] = rep::kCurrentTemplateVersion;
                    body["db"] = !res.empty();
                    body["maxRows"] = static_cast<Json::UInt64>(maxRows);
                    body["readOnlyRole"] = static_cast<bool>(dbRo);
                    body["authMode"] = authMode == rep::AuthMode::Proxy ? "proxy"
                                       : authMode == rep::AuthMode::Session ? "session" : "off";
                    // версия приложения из файла VERSION (единая точка, см. scripts/version.sh)
                    body["version"] = REP_VERSION;
                    body["logRetentionDays"] = logRetentionDays;
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


    // --- Планировщик и отправка рассылки (ADR-46) ----------------------------
    // Одна подписка: собрать CSV через buildCsv (в цикле событий), отправить письма,
    // записать результат в журнал и пересчитать next_run_at. Ошибки не бросают
    // исключений — попадают в журнал и в lastError.
    auto runSubscription = [buildCsv, buildXlsx, smtpConfig, parseTextArray, smtpRetryAttempts,
                            smtpRetryDelaySeconds](long long id, bool manual, const std::string &actor, bool retry) {
        PGconn *conn = PQconnectdb(rep::connInfoFromEnv().c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            LOG_ERROR << "рассылка: нет соединения с БД: " << PQerrorMessage(conn);
            PQfinish(conn);
            return;
        }
        const std::string idText = std::to_string(id);
        const char *values[] = {idText.c_str()};
        PGresult *res = PQexecParams(
            conn,
            "SELECT report_id, title, cron, params::text, recipients::text, format, timezone, retry_count"
            " FROM report_subscriptions WHERE id = $1",
            1, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1) {
            PQclear(res);
            PQfinish(conn);
            LOG_WARN << "рассылка: подписка " << id << " не найдена";
            return;
        }
        const std::string reportId = PQgetvalue(res, 0, 0);
        const std::string title = PQgetvalue(res, 0, 1);
        const std::string cronExpr = PQgetvalue(res, 0, 2);
        const std::string paramsText = PQgetvalue(res, 0, 3);
        const auto recipients = parseTextArray(PQgetvalue(res, 0, 4));
        const std::string format = PQgetvalue(res, 0, 5);
        const std::string timezone = PQgetvalue(res, 0, 6);
        const int retryCount = std::atoi(PQgetvalue(res, 0, 7));
        PQclear(res);
        const int attempt = retry ? retryCount + 1 : 1;

        Json::Value params;
        {
            Json::CharReaderBuilder reader;
            std::string errs;
            std::istringstream in(paramsText);
            Json::parseFromStream(reader, in, &params, &errs);
        }

        const auto started = std::chrono::steady_clock::now();
        bool ok = false;
        std::string error;
        int rows = 0;
        int sent = 0;

        // сборка файла идёт в цикле событий: buildCsv работает с пулом данных
        auto promise = std::make_shared<std::promise<std::tuple<int, std::string, std::string, int>>>();
        auto future = promise->get_future();
        const bool wantXlsx = format == "xlsx";
        app().getLoop()->queueInLoop([buildCsv, buildXlsx, reportId, params, wantXlsx, promise]() {
            auto emit = [promise](int status, const std::string &code, const std::string &payload, int count) {
                promise->set_value({status, code, payload, count});
            };
            if (wantXlsx) {
                buildXlsx(reportId, "", params, emit);
            } else {
                buildCsv(reportId, "", params, emit);
            }
        });

        if (future.wait_for(std::chrono::seconds(120)) != std::future_status::ready) {
            error = "сборка файла не завершилась за 120 с";
        } else {
            auto [status, code, payload, count] = future.get();
            if (status != 200) {
                error = code + ": " + payload;
            } else {
                rows = count;
                rep::MailConfig config = smtpConfig();
                const bool xlsx = format == "xlsx";
                const std::string attachmentName = reportId + (xlsx ? ".xlsx" : ".csv");
                const std::string attachmentType =
                    xlsx ? "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" : "text/csv";
                std::string failures;
                for (const auto &address : recipients) {
                    rep::MailMessage message;
                    message.to = address;
                    message.subject = "Отчёт: " + title;
                    message.text = "Отчёт «" + title + "» во вложении.\nСформирован автоматически сервером отчётов.";
                    message.attachmentName = attachmentName;
                    message.attachmentType = attachmentType;
                    message.attachmentData = payload;
                    std::string mailErr;
                    if (rep::sendMail(config, message, mailErr)) {
                        ++sent;
                    } else {
                        failures += (failures.empty() ? "" : "; ") + address + ": " + mailErr;
                    }
                }
                ok = failures.empty();
                error = failures;
            }
        }

        const auto elapsed = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());

        // журнал отправки
        const std::string okText = ok ? "true" : "false";
        const std::string rowsText = std::to_string(rows);
        const std::string sentText = std::to_string(sent);
        const std::string msText = std::to_string(elapsed);
        const std::string attemptText = std::to_string(attempt);
        const char *runValues[] = {idText.c_str(), okText.c_str(), error.c_str(), rowsText.c_str(),
                                   sentText.c_str(), msText.c_str(), attemptText.c_str()};
        PGresult *run = PQexecParams(
            conn,
            "INSERT INTO subscription_runs (subscription_id, finished_at, ok, error, rows, recipients, duration_ms,"
            " attempt) VALUES ($1, now(), $2::boolean, NULLIF($3, ''), $4::int, $5::int, $6::int, $7::int)",
            7, nullptr, runValues, nullptr, nullptr, 0);
        PQclear(run);

        // Следующий запуск считается в часовом поясе подписки (ADR-60).
        // Повтор неудачной отправки расписание не сдвигает: иначе сбой SMTP
        // «съедал» бы плановое время, и отчёт за период не уходил вовсе.
        rep::Cron cron;
        std::string cronErr;
        std::string nextEpoch = "0";
        if (rep::parseCron(cronExpr, cron, cronErr)) {
            const std::time_t next = nextRunInZone(conn, cron, timezone, std::time(nullptr));
            if (next > 0) nextEpoch = std::to_string(next);
        }

        // Состояние повторов (ADR-61): успех сбрасывает счётчик, неудача планирует
        // следующий повтор с удвоением паузы, исчерпание попыток — отказ и запись в журнал.
        std::string retryAtSql = "NULL";
        std::string retryCountText = "0";
        bool gaveUp = false;
        if (ok) {
            retryCountText = "0";
        } else if (!manual && smtpRetryAttempts > 0 && attempt < smtpRetryAttempts) {
            const int delay = smtpRetryDelaySeconds * (1 << (attempt - 1)); // 60, 120, 240…
            retryAtSql = "now() + (" + std::to_string(delay) + " || ' seconds')::interval";
            retryCountText = std::to_string(attempt);
        } else if (!manual && smtpRetryAttempts > 0) {
            gaveUp = true;
        }

        const char *markValues[] = {idText.c_str(), nextEpoch.c_str(), retryCountText.c_str()};
        PGresult *mark = PQexecParams(
            conn,
            ("UPDATE report_subscriptions SET last_run_at = now(), retry_count = $3::int,"
             " retry_at = " + retryAtSql +
             ", next_run_at = CASE WHEN $2::bigint > 0 AND NOT " + std::string(retry ? "true" : "false") +
             " THEN to_timestamp($2::bigint) ELSE next_run_at END WHERE id = $1")
                .c_str(),
            3, nullptr, markValues, nullptr, nullptr, 0);
        PQclear(mark);
        if (gaveUp) {
            auditEvent(actor, "subscription.given_up", "subscription", idText,
                       "{\"attempts\":" + std::to_string(smtpRetryAttempts) + ",\"error\":\"" + error + "\"}", "");
        }
        PQfinish(conn);

        LOG_INFO << "рассылка: подписка " << id << " («" << title << "», " << reportId << "), попытка " << attempt
                 << (retry ? " (повтор)" : "") << ", строк " << rows << ", писем " << sent << "/" << recipients.size()
                 << ", зона " << timezone << ", " << (ok ? "ок" : ("ошибка: " + error))
                 << (manual ? " (вручную)" : "");
        auditEvent(actor, "subscription.run", "subscription", idText,
                   std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"rows\":" + std::to_string(rows) +
                       ",\"sent\":" + std::to_string(sent) + ",\"manual\":" + (manual ? "true" : "false") +
                       ",\"attempt\":" + std::to_string(attempt) +
                       ",\"retry\":" + (retry ? "true" : "false") + "}",
                   "");
    };

    // «Отправить сейчас»: не ждём тика планировщика
    app().registerHandler(
        "/api/admin/subscriptions/{id}/run",
        [adminEnabled, runSubscription](const HttpRequestPtr &req,
                                        std::function<void(const HttpResponsePtr &)> &&cb, const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            const auto numeric = std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isdigit(c); });
            if (id.empty() || !numeric) {
                sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", "некорректный id подписки");
                return;
            }
            const long long subscriptionId = std::stoll(id);
            std::string actor = requestActor(req);
            std::thread([runSubscription, subscriptionId, actor]() {
                // ручной запуск: повторов не планируем, ошибку видит человек
                runSubscription(subscriptionId, true, actor, false);
            }).detach();
            Json::Value out;
            out["scheduled"] = true;
            cb(jsonResponse(out));
        },
        {Post});

    // Журнал отправок подписки: что именно ушло, сколько строк и писем, когда и с какой ошибкой.
    app().registerHandler(
        "/api/admin/subscriptions/{id}/runs",
        [adminEnabled](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb,
                       const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            PGconn *conn = PQconnectdb(rep::connInfoFromEnv().c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                const std::string msg = PQerrorMessage(conn);
                PQfinish(conn);
                sendError(cb, k503ServiceUnavailable, "DB_UNAVAILABLE", msg);
                return;
            }
            const std::string limitText = req->getParameter("limit").empty() ? "20" : req->getParameter("limit");
            const char *values[] = {id.c_str(), limitText.c_str()};
            PGresult *res = PQexecParams(
                conn,
                "SELECT to_char(started_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'),"
                " to_char(finished_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'),"
                " ok, coalesce(error, ''), coalesce(rows, 0), coalesce(recipients, 0), coalesce(duration_ms, 0)"
                " FROM subscription_runs WHERE subscription_id = $1::bigint"
                " ORDER BY started_at DESC LIMIT LEAST($2::int, 100)",
                2, nullptr, values, nullptr, nullptr, 0);
            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                const std::string msg = PQerrorMessage(conn);
                PQclear(res);
                PQfinish(conn);
                sendError(cb, k500InternalServerError, "QUERY_FAILED", msg);
                return;
            }
            Json::Value items(Json::arrayValue);
            const int count = PQntuples(res);
            for (int i = 0; i < count; ++i) {
                Json::Value run;
                run["startedAt"] = PQgetvalue(res, i, 0);
                run["finishedAt"] = PQgetisnull(res, i, 1) ? "" : PQgetvalue(res, i, 1);
                run["ok"] = std::string(PQgetvalue(res, i, 2)) == "t";
                run["error"] = PQgetvalue(res, i, 3);
                run["rows"] = std::stoi(PQgetvalue(res, i, 4));
                run["recipients"] = std::stoi(PQgetvalue(res, i, 5));
                run["durationMs"] = std::stoi(PQgetvalue(res, i, 6));
                items.append(run);
            }
            PQclear(res);
            PQfinish(conn);
            Json::Value out;
            out["items"] = items;
            cb(jsonResponse(out));
        },
        {Get});

    // Чистка журналов: сразу при старте и раз в 6 часов, только если глубина задана.
    auto startLogRetention = [logRetentionDays]() {
        if (logRetentionDays <= 0) {
            LOG_INFO << "ретеншен журналов выключен (REP_LOG_RETENTION_DAYS=0)";
            return;
        }
        std::thread([logRetentionDays]() {
            while (true) {
                const PurgeResult purged = purgeOldLogs(logRetentionDays);
                if (purged.ok) {
                    LOG_INFO << "ретеншен журналов: " << logRetentionDays << " дн., удалено audit_log "
                             << purged.audit << ", subscription_runs " << purged.runs;
                    if (purged.audit > 0 || purged.runs > 0) {
                        auditEvent("system", "logs.purge", "journal", "audit_log",
                                   "{\"days\":" + std::to_string(logRetentionDays) +
                                       ",\"deleted\":" + std::to_string(purged.audit) +
                                       ",\"runs\":" + std::to_string(purged.runs) + "}",
                                   "");
                    }
                } else {
                    LOG_WARN << "ретеншен журналов: ошибка: " << purged.error;
                }
                std::this_thread::sleep_for(std::chrono::hours(6));
            }
        }).detach();
    };

    auto startScheduler = [schedulerEnabled, runSubscription, smtpHost, smtpPort, smtpFrom]() {
        if (!schedulerEnabled) return;
        std::thread([runSubscription]() {
            PGconn *conn = nullptr;
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(30));
                if (!conn || PQstatus(conn) != CONNECTION_OK) {
                    if (conn) PQfinish(conn);
                    conn = PQconnectdb(rep::connInfoFromEnv().c_str());
                    if (PQstatus(conn) != CONNECTION_OK) continue;
                }
                // Подписка «к отправке», если подошло плановое время ИЛИ наступил
                // срок повтора после сбоя SMTP (ADR-61). is_retry отличает второй случай:
                // повтор не должен сдвигать расписание.
                // «Заём» на время отправки: подписка не должна достаться второму экземпляру,
                // пока первый ещё собирает файл. next_run_at уезжает вперёд на 2 минуты и
                // перезаписывается настоящим расписанием в конце отправки; retry_at снимается,
                // его заново выставит runSubscription при повторном сбое (ADR-61).
                PGresult *res = PQexec(
                    conn,
                    "WITH due AS ("
                    "  SELECT id, (retry_at IS NOT NULL AND retry_at <= now()"
                    "              AND (next_run_at IS NULL OR next_run_at > now())) AS is_retry"
                    "    FROM report_subscriptions"
                    "   WHERE enabled AND ("
                    "     (next_run_at IS NOT NULL AND next_run_at <= now())"
                    "     OR (retry_at IS NOT NULL AND retry_at <= now())"
                    "   )"
                    "   ORDER BY coalesce(retry_at, next_run_at) NULLS FIRST LIMIT 10 FOR UPDATE SKIP LOCKED"
                    ") "
                    "UPDATE report_subscriptions s SET"
                    "  last_run_at = now(),"
                    "  next_run_at = CASE WHEN d.is_retry THEN s.next_run_at"
                    "                    ELSE now() + interval '2 minutes' END,"
                    "  retry_at = NULL"
                    " FROM due d WHERE s.id = d.id"
                    " RETURNING s.id, d.is_retry");
                if (PQresultStatus(res) == PGRES_TUPLES_OK) {
                    const int count = PQntuples(res);
                    std::vector<std::pair<long long, bool>> due;
                    for (int i = 0; i < count; ++i) {
                        due.emplace_back(std::stoll(PQgetvalue(res, i, 0)), std::string(PQgetvalue(res, i, 1)) == "t");
                    }
                    PQclear(res);
                    for (const auto &[subscriptionId, isRetry] : due) {
                        runSubscription(subscriptionId, false, "system", isRetry);
                    }
                } else {
                    PQclear(res);
                }
            }
        }).detach();
        LOG_INFO << "планировщик рассылки запущен (тик 30 с, SMTP "
                 << (smtpHost.empty() ? std::string("не настроен") : smtpHost + ":" + std::to_string(smtpPort)) << ")";
    };

    LOG_INFO << "listening on " << host << ":" << port;
    // Порт проверяем сами: drogon при занятом порте уходит в abort(), а на аварийном
    // выходе с работающими потоками (планировщик) память уже рушилась — вместо
    // понятного сообщения получали SIGABRT и «corrupted double-linked list».
    {
        const int probe = ::socket(AF_INET, SOCK_STREAM, 0);
        if (probe >= 0) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<uint16_t>(port));
            addr.sin_addr.s_addr = INADDR_ANY;
            const bool busy = ::bind(probe, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0;
            ::close(probe);
            if (busy) {
                // _Exit: разрушать статику drogon на фатальном выходе нельзя — падало в SIGSEGV
                LOG_ERROR << "порт " << port << " уже занят — сервер не запущен";
                std::_Exit(1);
            }
        }
    }

    startScheduler();
    startLogRetention();

    try {
        app().addListener(host, port).setThreadNum(threads).run();
    } catch (const std::exception &e) {
        // Планировщик и SMTP-поток живут отдельно от drogon: при фатальной ошибке
        // (например, порт занят) разрушать статику под работающими потоками нельзя —
        // это уже приводило к «corrupted double-linked list» вместо внятного выхода.
        LOG_ERROR << "сервер остановлен: " << e.what();
        std::_Exit(1);
    }
    return 0;
}

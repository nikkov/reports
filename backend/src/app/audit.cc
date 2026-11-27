#include "app/audit.h"

#include <libpq-fe.h>

#include <cstdlib>
#include <sstream>

#include <drogon/drogon.h>

#include "app/db.h"
#include "env.h"
#include "logging.h"

namespace rep {
namespace app {

using namespace drogon;

// Совместимость со старыми вызовами: админские действия пишутся через единую функцию
// логирования (ADR-109), а не отдельным механизмом. Строка «template.create» превращается
// в enum-действие, ip и объект уходят в контекст.
namespace {

rep::log::Level legacyLevel(const std::string &action) {
    if (action == "auth.login_failed" || action == "subscription.given_up") {
        return rep::log::Level::Warning;
    }
    return rep::log::Level::Info;
}

rep::log::Action legacyAction(const std::string &action) {
    if (action.rfind("template.", 0) == 0) {
        return action == "template.publish" ? rep::log::Action::PublishTemplate
                                            : rep::log::Action::ChangeTemplate;
    }
    if (action == "auth.login" || action == "auth.login_failed") return rep::log::Action::Login;
    if (action == "user.create" || action == "user.autocreate") return rep::log::Action::CreateUser;
    if (action == "user.delete") return rep::log::Action::DeleteUser;
    if (action == "user.update") return rep::log::Action::ChangeUser;
    if (action.rfind("role.", 0) == 0) return rep::log::Action::ChangeRoles;
    if (action.rfind("subscription.", 0) == 0) {
        return action == "subscription.run" || action == "subscription.given_up"
                   ? rep::log::Action::RunSubscription
                   : rep::log::Action::ChangeSubscription;
    }
    return rep::log::Action::Other;
}

}  // namespace

void auditEvent(const std::string &actor, const std::string &action, const std::string &objectType,
                const std::string &objectId, const std::string &detailsJson, const std::string &ip) {
    Json::Value ctx(Json::objectValue);
    // в журнале остаётся прежняя строка действия (template.update) — её видят админка
    // и фильтры; enum используется в stdout JSON
    ctx["legacy_action"] = action;
    ctx["object_type"] = objectType;
    ctx["object_id"] = objectId;
    if (!ip.empty()) ctx["ip"] = ip;
    if (!detailsJson.empty() && detailsJson != "{}") {
        Json::CharReaderBuilder builder;
        Json::Value details;
        std::string errs;
        std::istringstream in(detailsJson);
        if (Json::parseFromStream(builder, in, &details, &errs)) ctx["details"] = details;
    }
    rep::log::event(legacyLevel(action), legacyAction(action), actor == "system" ? "" : actor,
                    action, ctx);
}

// --- Аудит действий (ADR-52) ---------------------------------------------
// Пишем отдельным коротким соединением: событий немного (админские действия),
// а блокирующая вставка не требует ни цикла событий, ни захвата колбэков.
// Логин текущего пользователя (пусто — системное/анонимное событие).
std::string requestActor(const HttpRequestPtr &req) {
    if (!req) return "system";
    if (const auto attrs = req->attributes()) {
        const std::string login = attrs->get<std::string>("session_login");
        if (!login.empty()) return login;
    }
    return "anonymous";
}

std::string actorOf(const HttpRequestPtr &req) {
    if (req && req->attributes()->find("session_login")) {
        return req->attributes()->get<std::string>("session_login");
    }
    return std::string();
}

// Непосредственная запись строки журнала — используется приёмником логов (ADR-109).
void writeAuditRow(const std::string &actor, const std::string &action, const std::string &objectType,
                   const std::string &objectId, const std::string &detailsJson, const std::string &ip) {
    PGconn *conn = PQconnectdb(rep::connInfoFromEnv().c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        REP_LOG_WARN(AuditFailed, "") << "аудит: нет соединения с БД: " << PQerrorMessage(conn);
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

}  // namespace app
}  // namespace rep

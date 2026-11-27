#include "app/system_routes.h"

#include <drogon/drogon.h>

#include "app/context.h"
#include "app/db.h"
#include "app/http.h"
#include "auth.h"
#include "connections.h"
#include "template_migrate.h"

namespace rep {
namespace routes {

using namespace drogon;
using rep::app::describeError;
using rep::app::runQuery;
using rep::http::jsonResponse;
using rep::http::sendError;

void registerSystemRoutes(rep::app::AppContext &ctx) {
    // Локальные копии: тело обработчика перенесено из main.cc без правок (ADR-193)
    const auto db = ctx.db;
    const std::size_t maxRows = ctx.maxRows;
    const rep::AuthMode authMode = ctx.authMode;
    const int logRetentionDays = ctx.logRetentionDays;
    const bool dbRo = ctx.readOnlyRole;
    rep::ConnRegistry &connRegistry = *ctx.conn;

    drogon::app().registerHandler(
        "/api/health",
        [db, maxRows, authMode, logRetentionDays, &dbRo, &connRegistry](
            const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
            runQuery(
                db,
                "SELECT current_user AS role, "
                "(SELECT rolsuper FROM pg_roles WHERE rolname = current_user) AS is_super", {},
                [cb, maxRows, authMode, logRetentionDays, &dbRo, &connRegistry](const orm::Result &res) {
                    Json::Value body;
                    body["status"] = "ok";
                    // сколько внешних источников сейчас в карантине (ADR-132): дежурному
                    // не нужно читать журнал, чтобы заметить сбой
                    body["datasourcesInQuarantine"] =
                        static_cast<Json::UInt64>(connRegistry.failedCount());
                    body["schemaVersion"] = rep::kCurrentTemplateVersion;
                    body["db"] = !res.empty();
                    // под какой ролью работает сервер: в эксплуатации это reporting_srv,
                    // суперпользователь допустим только на стенде (ADR-75)
                    if (!res.empty()) {
                        body["dbUser"] = res[0]["role"].c_str();
                        body["superuser"] = res[0]["is_super"].as<bool>();
                    }
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
}

}  // namespace routes
}  // namespace rep

#include "app/admin_reports.h"

#include <drogon/drogon.h>

#include "app/context.h"
#include "app/db.h"
#include "app/http.h"
#include "app/audit.h"
#include "env.h"

namespace rep {
namespace routes {

using namespace drogon;
using rep::app::actorOf;
using rep::app::describeError;
using rep::app::runQuery;
using rep::http::jsonResponse;
using rep::http::sendError;
using rep::http::TracedMutex;

void registerAdminReportReadRoutes(rep::app::AppContext &ctx) {
    // Локальные копии: тела обработчиков перенесены из main.cc без правок (ADR-193)
    const bool adminEnabled = ctx.adminEnabled;
    const auto db = ctx.db;
    auto &templatesRaw = *ctx.templatesRaw;
    rep::http::TracedMutex &templatesMutex = *ctx.templatesMutex;

    drogon::app().registerHandler(
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
                std::lock_guard<TracedMutex> lock(templatesMutex);
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

    drogon::app().registerHandler(
        "/api/admin/reports/deleted",
        [db, adminEnabled](const HttpRequestPtr &req,
                           std::function<void(const HttpResponsePtr &)> &&cb) {
            rep::http::setActor(actorOf(req));
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            // Пагинация как в каталоге (ADR-41, ADR-163): без limit — всё, с limit — страница и total
            const std::string limitParam = req->getParameter("limit");
            const std::string offsetParam = req->getParameter("offset");
            int limit = 0;
            int offset = 0;
            if (!limitParam.empty()) {
                try {
                    limit = std::stoi(limitParam);
                } catch (...) {
                    sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", "limit должен быть числом");
                    return;
                }
                if (limit < 1 || limit > 200) {
                    sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", "limit: 1…200");
                    return;
                }
            }
            if (!offsetParam.empty()) {
                try {
                    offset = std::stoi(offsetParam);
                } catch (...) {
                    sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", "offset должен быть числом");
                    return;
                }
                if (offset < 0) {
                    sendError(cb, k422UnprocessableEntity, "INVALID_PARAMS", "offset ≥ 0");
                    return;
                }
            }
            // count(*) OVER () даёт общее число строк в том же запросе — второй запрос не нужен
            std::string sql = "SELECT id, title, updated_at, count(*) OVER () AS total FROM report_templates "
                              "WHERE is_delete = true ORDER BY id";
            std::vector<std::string> params;
            if (limit > 0) {
                sql += " LIMIT $1 OFFSET $2";
                params.push_back(std::to_string(limit));
                params.push_back(std::to_string(offset));
            }
            runQuery(
                db, sql, params,
                [cb, limit, offset](const orm::Result &res) {
                    Json::Value items(Json::arrayValue);
                    long long total = 0;
                    for (const auto &row : res) {
                        Json::Value item;
                        item["id"] = row["id"].as<std::string>();
                        item["title"] = row["title"].as<std::string>();
                        item["updatedAt"] = row["updated_at"].as<std::string>();
                        items.append(item);
                        total = row["total"].as<long long>();
                    }
                    Json::Value out;
                    out["items"] = items;
                    // total нужен и при пустой странице: считаем его отдельно, если строк не было
                    if (limit > 0 && res.empty()) {
                        out["total"] = 0;
                    } else {
                        out["total"] = Json::Int64(total);
                    }
                    if (limit > 0) {
                        out["limit"] = limit;
                        out["offset"] = offset;
                        out["hasMore"] = offset + static_cast<int>(res.size()) < total;
                    }
                    cb(jsonResponse(out));
                },
                [cb](const std::exception_ptr &e) {
                    sendError(cb, k500InternalServerError, "QUERY_FAILED", describeError(e));
                });
        },
        {Get});
}

}  // namespace routes
}  // namespace rep

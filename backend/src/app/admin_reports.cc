#include "app/admin_reports.h"

#include <drogon/drogon.h>

#include "app/context.h"
#include "app/db.h"
#include "app/http.h"
#include "app/template_parse.h"
#include "app/template_types.h"
#include "app/audit.h"
#include "env.h"

namespace rep {
namespace routes {

using namespace drogon;
using ::rep::app::actorOf;
using ::rep::app::describeError;
using ::rep::app::parseTemplate;
using ::rep::app::requestActor;
using ::rep::app::TemplateInfo;
using ::rep::app::runQuery;
using ::rep::http::jsonResponse;
using ::rep::http::sendError;
using ::rep::http::TracedMutex;

void registerAdminReportReadRoutes(::rep::app::AppContext &ctx) {
    // Локальные копии: тела обработчиков перенесены из main.cc без правок (ADR-193)
    const bool adminEnabled = ctx.adminEnabled;
    const auto db = ctx.db;
    auto &templatesRaw = *ctx.templatesRaw;
    ::rep::http::TracedMutex &templatesMutex = *ctx.templatesMutex;

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
            ::rep::http::setActor(actorOf(req));
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


// Остаток админских маршрутов шаблонов: восстановление, сохранение, история, откат,
// черновики и публикация (ADR-193). Зависимости приходят параметрами: applyTemplate и
// prepareTemplate живут в main() как обёртки над app/template_store и app/template_parse.

#include <functional>
#include <string>
#include <vector>

#include <drogon/drogon.h>

#include "app/audit.h"
#include "app/context.h"
#include "app/db.h"
#include "app/http.h"
#include "app/template_parse.h"
#include "app/template_types.h"
#include "auth.h"
#include "env.h"
#include "validate.h"


using namespace drogon;
using ::rep::app::actorOf;
using ::rep::app::auditEvent;
using ::rep::app::describeError;
using ::rep::app::requestIp;
using ::rep::app::parseTemplate;
using ::rep::app::requestActor;
using ::rep::app::TemplateInfo;
using ::rep::app::runQuery;
using ::rep::http::jsonResponse;
using ::rep::http::sendError;
using ::rep::http::TracedMutex;

void registerAdminReportWriteRoutes(
    ::rep::app::AppContext &ctx,
    std::function<bool(const std::string &, const std::string &, const std::string &)> applyTemplateInMemory,
    std::function<rep::ValidationResult(const std::string &, const std::string &, std::string *,
                                        std::vector<std::string> *)> prepareTemplate,
    std::function<rep::ValidationResult(const std::string &, const std::string &)> validateTemplateFull) {
    // Локальные копии: тела обработчиков перенесены из main.cc без правок (ADR-193)
    const bool adminEnabled = ctx.adminEnabled;
    const auto db = ctx.db;
    auto &templatesMutex = *ctx.templatesMutex;
    auto &draftsParsed = *ctx.draftsParsed;
    auto &draftsRaw = *ctx.draftsRaw;
    auto &draftsErrors = *ctx.draftsErrors;

    drogon::app().registerHandler(
        "/api/admin/reports/{id}/restore",
        [db, adminEnabled, applyTemplateInMemory, prepareTemplate](const HttpRequestPtr &req,
                                                                    std::function<void(const HttpResponsePtr &)> &&cb,
                                                                    const std::string &id) {
            ::rep::http::setActor(actorOf(req));
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            const std::string restoreActor = requestActor(req);
            const std::string restoreIp = requestIp(req);
            runQuery(
                db, "SELECT title, body::text AS body FROM report_templates WHERE id = $1 AND is_delete = true",
                {id},
                [cb, db, id, applyTemplateInMemory, prepareTemplate, restoreActor, restoreIp](
                    const orm::Result &res) {
                    if (res.empty()) {
                        sendError(cb, k404NotFound, "NOT_DELETED", "отчёт '" + id + "' не найден среди удалённых");
                        return;
                    }
                    const std::string title = res[0]["title"].as<std::string>();
                    const std::string body = res[0]["body"].as<std::string>();
                    // тело могло быть испорчено правкой в БД: в каталог битый шаблон не пускаем,
                    // признак удаления при этом НЕ снимаем (ADR-154)
                    const auto restored = prepareTemplate(body, id, nullptr, nullptr);
                    if (!restored.ok()) {
                        sendError(cb, k422UnprocessableEntity, "TEMPLATE_INVALID",
                                  "удалённый шаблон содержит ошибки и не может быть восстановлен");
                        return;
                    }
                    runQuery(
                        db, "UPDATE report_templates SET is_delete = false, updated_at = now() WHERE id = $1",
                        {id},
                        [cb, id, title, body, applyTemplateInMemory, restoreActor, restoreIp](
                            const orm::Result &) {
                            applyTemplateInMemory(id, body, title);
                            auditEvent(restoreActor, "template.restore", "report", id, "{}", restoreIp);
                            Json::Value out;
                            out["id"] = id;
                            out["restored"] = true;
                            cb(jsonResponse(out));
                        },
                        [cb](const std::exception_ptr &e) {
                            sendError(cb, k500InternalServerError, "RESTORE_FAILED", describeError(e));
                        });
                },
                [cb](const std::exception_ptr &e) {
                    sendError(cb, k500InternalServerError, "RESTORE_FAILED", describeError(e));
                });
        },
        {Post});
    drogon::app().registerHandler(
        "/api/admin/reports",
        [db, adminEnabled](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена: запустите сервис с REP_ADMIN=1");
                return;
            }
            runQuery(
                db,
                "SELECT id, title, char_length(body::text) AS size, to_char(updated_at, 'YYYY-MM-DD HH24:MI') AS updated, "
                "(draft IS NOT NULL) AS has_draft, to_char(draft_updated_at, 'YYYY-MM-DD HH24:MI') AS draft_at, "
                // роли доступа нужны вкладке «Роли»: видно, какие отчёты закрыты ключом (ADR-171)
                "COALESCE(body->'accessRoles', '[]'::jsonb)::text AS access_roles "
                "FROM report_templates WHERE is_delete = false ORDER BY id",
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
                        if (!row["access_roles"].isNull()) {
                            Json::CharReaderBuilder reader;
                            Json::Value roles;
                            std::string errs;
                            const std::string raw = row["access_roles"].as<std::string>();
                            std::istringstream in(raw);
                            if (Json::parseFromStream(reader, in, &roles, &errs) && roles.isArray()) {
                                item["accessRoles"] = roles;
                            }
                        }
                        list.append(item);
                    }
                    cb(jsonResponse(list));
                },
                [cb](const std::exception_ptr &e) {
                    sendError(cb, k500InternalServerError, "ADMIN_LIST_FAILED", describeError(e));
                });
        },
        {Get});
    drogon::app().registerHandler(
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
    drogon::app().registerHandler(
        "/api/admin/reports/{id}/rollback/{snapshot}",
        [db, adminEnabled, applyTemplateInMemory](const HttpRequestPtr &req,
                                                   std::function<void(const HttpResponsePtr &)> &&cb,
                                                   const std::string &id, const std::string &snapshot) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            // логин и ip до асинхронной задачи: в колбэке req уже уничтожен (ADR-153)
            const std::string auditActor = requestActor(req);
            const std::string auditIp = requestIp(req);
            runQuery(
                db, "SELECT title, body::text AS body FROM report_templates_history WHERE id = $1 AND report_id = $2",
                {snapshot, id},
                [cb, db, id, applyTemplateInMemory, auditActor, auditIp, snapshot](
                    const orm::Result &res) {
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
                        [cb, db, id, raw, title, applyTemplateInMemory, auditActor, auditIp, snapshot](
                            const orm::Result &) {
                            runQuery(
                                db, "UPDATE report_templates SET title = $2, body = $3::jsonb, updated_at = now() WHERE id = $1",
                                {id, title, raw},
                                [cb, id, raw, title, applyTemplateInMemory, auditActor, auditIp, snapshot](
                                    const orm::Result &) {
                                    applyTemplateInMemory(id, raw, title);
                                    Json::Value out;
                                    out["id"] = id;
                                    auditEvent(auditActor, "template.rollback", "report", id,
                                               "{\"snapshot\":\"" + snapshot + "\"}", auditIp);
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
    drogon::app().registerHandler(
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
    drogon::app().registerHandler(
        "/api/admin/reports/{id}/draft",
        [db, adminEnabled, validateTemplateFull, &draftsParsed, &draftsRaw, &draftsErrors,
         &templatesMutex](
            const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb,
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
            // Черновик сохраняется всегда, даже с ошибками (ADR-131): автор может оставить
            // работу на середине. Ошибки возвращаются в ответе, а исполнение запрещено.
            // Полный список ошибок: автор видит все сразу, а не правит по одной (ADR-132)
            const auto checked = validateTemplateFull(raw, id);
            const bool valid = checked.ok();
            Json::Value errorList(Json::arrayValue);
            std::string firstError;
            for (const auto &e : checked.errors) {
                Json::Value item;
                item["path"] = e.path;
                item["message"] = e.message;
                errorList.append(item);
                if (firstError.empty()) {
                    firstError = e.path == "(корень)" ? e.message : e.path + ": " + e.message;
                }
            }
            runQuery(
                db, "UPDATE report_templates SET draft = $2::jsonb, draft_updated_at = now() WHERE id = $1",
                {id, raw},
                [cb, id, raw, valid, firstError, errorList, &draftsParsed, &draftsRaw, &draftsErrors,
                 &templatesMutex](
                    const orm::Result &updated) {
                    if (updated.affectedRows() == 0) {
                        sendError(cb, k404NotFound, "REPORT_NOT_FOUND", "отчёт '" + id + "' не найден");
                        return;
                    }
                    {
                        std::lock_guard<TracedMutex> lock(templatesMutex);
                        draftsRaw[id] = raw;
                        draftsErrors[id] = valid ? std::string() : firstError;
                        if (valid) {
                            TemplateInfo info;
                            if (parseTemplate(raw, id, info)) draftsParsed[id] = info;
                        } else {
                            // исполнять нечего: данные и публикация вернут 422 (ADR-131)
                            draftsParsed.erase(id);
                        }
                    }
                    Json::Value out;
                    out["id"] = id;
                    out["draft"] = true;
                    out["valid"] = valid;
                    if (!valid) out["errors"] = errorList;
                    cb(jsonResponse(out));
                },
                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "DRAFT_FAILED", describeError(e)); });
        },
        {Put});

    // DELETE /api/admin/reports/{id}/draft — отменить черновик
    drogon::app().registerHandler(
        "/api/admin/reports/{id}/draft",
        [db, adminEnabled, &draftsParsed, &draftsRaw, &templatesMutex](
            const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb,
                           const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            runQuery(
                db, "UPDATE report_templates SET draft = NULL, draft_updated_at = NULL WHERE id = $1", {id},
                [cb, id, &draftsParsed, &draftsRaw, &templatesMutex](const orm::Result &) {
                    {
                        std::lock_guard<TracedMutex> lock(templatesMutex);
                        draftsParsed.erase(id);
                        draftsRaw.erase(id);
                    }
                    Json::Value out;
                    out["id"] = id;
                    out["discarded"] = true;
                    cb(jsonResponse(out));
                },
                [cb](const std::exception_ptr &e) { sendError(cb, k500InternalServerError, "DRAFT_FAILED", describeError(e)); });
        },
        {Delete});
    drogon::app().registerHandler(
        "/api/admin/reports/{id}/publish",
        [db, adminEnabled, applyTemplateInMemory, prepareTemplate, &draftsParsed, &draftsRaw, &templatesMutex](
            const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb, const std::string &id) {
            if (!adminEnabled) {
                sendError(cb, k404NotFound, "ADMIN_DISABLED", "админка выключена");
                return;
            }
            const std::string auditActor = requestActor(req);
            const std::string auditIp = requestIp(req);
            runQuery(
                db, "SELECT draft::text AS draft FROM report_templates WHERE id = $1", {id},
                [cb, db, id, applyTemplateInMemory, prepareTemplate, &draftsParsed, &draftsRaw,
                 &templatesMutex, auditActor, auditIp](const orm::Result &res) {
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
                        [cb, db, id, raw, title, applyTemplateInMemory, &draftsParsed, &draftsRaw,
                         &templatesMutex, auditActor, auditIp](const orm::Result &) {
                            runQuery(
                                db,
                                "UPDATE report_templates SET title = $2, body = $3::jsonb, updated_at = now(), "
                                "draft = NULL, draft_updated_at = NULL WHERE id = $1",
                                {id, title, raw},
                                [cb, id, raw, title, applyTemplateInMemory, &draftsParsed, &draftsRaw,
                                 &templatesMutex, auditActor, auditIp](const orm::Result &) {
                                    applyTemplateInMemory(id, raw, title);
                                    {
                                        std::lock_guard<TracedMutex> lock(templatesMutex);
                                        draftsParsed.erase(id);
                                        draftsRaw.erase(id);
                                    }
                                    Json::Value out;
                                    out["id"] = id;
                                    auditEvent(auditActor, "template.publish", "report", id, "{}", auditIp);
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
}

}  // namespace routes
}  // namespace rep

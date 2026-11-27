#include "app/auth_routes.h"

#include <drogon/drogon.h>

#include "app/audit.h"
#include "app/context.h"
#include "app/db.h"
#include "app/http.h"
#include "auth.h"
#include "env.h"

namespace rep {
namespace routes {

using namespace drogon;
using rep::app::actorOf;
using rep::app::auditEvent;
using rep::app::describeError;
using rep::app::requestIp;
using rep::app::runQuery;
using rep::http::jsonResponse;
using rep::http::sendError;

void registerAuthRoutes(rep::app::AppContext &ctx) {
    // Локальные копии: тела обработчиков перенесены из main.cc без правок (ADR-193)
    const auto db = ctx.db;
    const rep::AuthMode authMode = ctx.authMode;
    const int sessionTtlHours = ctx.sessionTtlHours;
    const std::string proxyHeader = ctx.proxyHeader;

    drogon::app().registerHandler(
        "/api/auth/login",
        [db, authMode, sessionTtlHours](const HttpRequestPtr &req,
                                        std::function<void(const HttpResponsePtr &)> &&cb) {
            rep::http::setActor(actorOf(req)); // логин в записях об ошибках (ADR-128)
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
                db, "SELECT id, login, password_salt, password_hash, role, enabled FROM users WHERE lower(login) = lower($1)",
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

    drogon::app().registerHandler(
        "/api/auth/logout",
        [db, authMode](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            rep::http::setActor(actorOf(req)); // логин в записях об ошибках (ADR-128)
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

    drogon::app().registerHandler(
        "/api/auth/me",
        [db, authMode, proxyHeader](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            rep::http::setActor(actorOf(req)); // логин в записях об ошибках (ADR-128)
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
                    db, "SELECT role FROM users WHERE lower(login) = lower($1) AND enabled", {login},
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
}

}  // namespace routes
}  // namespace rep

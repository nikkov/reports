#include "app/http.h"

#include <cstdio>
#include <cstdlib>

#if defined(__linux__)
#include <execinfo.h>
#endif

#include "env.h"
#include "logging.h"

namespace rep {
namespace http {

bool TracedMutex::enabled() {
    static const bool on = rep::envOr("REP_DEBUG_MUTEX", "0") == "1";
    return on;
}

std::size_t TracedMutex::tid() {
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

void TracedMutex::dumpStack(const char *tag, void *self, std::size_t threadId) {
#if defined(__linux__)
    void *frames[8];
    const int n = ::backtrace(frames, 8);
    char **syms = ::backtrace_symbols(frames, n);
    std::fprintf(stderr, "MUTEX %s %p tid %zu", tag, self, threadId);
    for (int i = 0; syms && i < n; ++i) std::fprintf(stderr, " | %s", syms[i]);
    std::fprintf(stderr, "\n");
    if (syms) std::free(syms);
#else
    std::fprintf(stderr, "MUTEX %s %p tid %zu\n", tag, self, threadId);
#endif
}

bool &TracedMutex::held() {
    thread_local bool holds = false;
    return holds;
}

void TracedMutex::lock() {
    if (enabled() && held()) {
        std::fprintf(stderr, "MUTEX REENTER %p tid %zu — повторный захват, будет дедлок\n",
                     static_cast<void *>(this), tid());
        dumpStack("reenter", static_cast<void *>(this), tid());
    }
    if (enabled()) dumpStack("wait", static_cast<void *>(this), tid());
    m.lock();
    held() = true;
    if (enabled()) dumpStack("lock", static_cast<void *>(this), tid());
}

void TracedMutex::unlock() {
    if (enabled()) std::fprintf(stderr, "MUTEX free %p tid %zu\n", static_cast<void *>(this), tid());
    held() = false;
    m.unlock();
}

namespace {
thread_local std::string currentActor;
}  // namespace

std::string &actorRef() {
    return currentActor;
}

void setActor(const std::string &login) {
    currentActor = login;
}

Json::Value errorBody(const std::string &code, const std::string &message) {
    Json::Value body;
    body["error"]["code"] = code;
    body["error"]["message"] = message;
    return body;
}

drogon::HttpResponsePtr jsonResponse(const Json::Value &body, drogon::HttpStatusCode status) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(status);
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(Json::FastWriter().write(body));
    resp->addHeader("Cache-Control", "no-store");
    return resp;
}

void sendError(const std::function<void(const drogon::HttpResponsePtr &)> &cb, drogon::HttpStatusCode status,
               const std::string &code, const std::string &message) {
    // Каждая ошибка API попадает в журнал (ADR-127): 5xx — error, остальные — warning,
    // 401 — debug (нет сессии — обычное дело при первом открытии страницы)
    const int numeric = static_cast<int>(status);
    rep::log::Level level = rep::log::Level::Warning;
    if (numeric >= 500) {
        level = rep::log::Level::Error;
    } else if (numeric == 401) {
        level = rep::log::Level::Debug;
    }
    rep::log::event(level, rep::log::Action::Other, currentActor,
                    "ошибка API " + std::to_string(numeric) + " " + code + ": " + message);
    cb(jsonResponse(errorBody(code, message), status));
}

}  // namespace http
}  // namespace rep

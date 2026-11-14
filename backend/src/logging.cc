#include "logging.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace rep {
namespace log {

namespace {

std::mutex &sinkMutex() {
    static std::mutex m;
    return m;
}

Sink &sinkRef() {
    static Sink sink;
    return sink;
}

std::mutex &writeMutex() {
    static std::mutex m;
    return m;
}

} // namespace

const char *levelName(Level level) {
    switch (level) {
        case Level::Trace: return "trace";
        case Level::Debug: return "debug";
        case Level::Info: return "info";
        case Level::Warning: return "warning";
        case Level::Error: return "error";
    }
    return "info";
}

const char *actionName(Action action) {
    switch (action) {
        case Action::OpenReport: return "open_report";
        case Action::ExportReport: return "export_report";
        case Action::PrintReport: return "print_report";
        case Action::Login: return "login";
        case Action::Logout: return "logout";
        case Action::CreateUser: return "create_user";
        case Action::ChangeUser: return "change_user";
        case Action::DeleteUser: return "delete_user";
        case Action::ChangeRoles: return "change_roles";
        case Action::ChangeTemplate: return "change_template";
        case Action::PublishTemplate: return "publish_template";
        case Action::RunSubscription: return "run_subscription";
        case Action::AccessDenied: return "access_denied";
        case Action::ServerStart: return "server_start";
        case Action::ServerStop: return "server_stop";
        case Action::SqlSlow: return "sql_slow";
        case Action::SqlError: return "sql_error";
        case Action::ConnectionError: return "connection_error";
        case Action::RestError: return "rest_error";
        case Action::AuditFailed: return "audit_failed";
        case Action::Other: return "other";
    }
    return "unknown";
}

std::string timestampNow() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t t = system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", utc.tm_year + 1900,
                  utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec,
                  static_cast<int>(ms.count()));
    return buf;
}

std::string formatJson(const Event &event, const std::string &ts) {
    Json::Value root(Json::objectValue);
    root["ts"] = ts;
    root["level"] = levelName(event.level);
    root["action"] = actionName(event.action);
    root["user"] = event.user; // пустая строка — системное/анонимное событие
    root["message"] = event.message;
    root["context"] = event.context.isNull() ? Json::Value(Json::objectValue) : event.context;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    return Json::writeString(builder, root);
}

void setSink(Sink newSink) {
    std::lock_guard<std::mutex> lock(sinkMutex());
    sinkRef() = std::move(newSink);
}

Sink sink() {
    std::lock_guard<std::mutex> lock(sinkMutex());
    return sinkRef();
}

void event(Level level, Action action, const std::string &user, const std::string &message,
           const Json::Value &context) {
    Event e;
    e.level = level;
    e.action = action;
    e.user = user;
    e.message = message;
    e.context = context;
    const std::string ts = timestampNow();
    const std::string json = formatJson(e, ts);
    {
        std::lock_guard<std::mutex> lock(writeMutex());
        std::fprintf(stdout, "%s\n", json.c_str());
        std::fflush(stdout);
    }
    // Приёмник читаем под замком, вызываем без него: sink может ходить в БД,
    // и держать мьютекс логгера на время сети нельзя.
    const Sink target = sink();
    if (target) target(e, ts);
}

} // namespace log
} // namespace rep

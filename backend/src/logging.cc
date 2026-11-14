#include "logging.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>

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

// --- файловый приёмник -----------------------------------------------------
std::mutex &fileMutex() {
    static std::mutex m;
    return m;
}
std::ofstream &fileRef() {
    static std::ofstream file;
    return file;
}
std::string &filePathRef() {
    static std::string path;
    return path;
}
Level &fileLevelRef() {
    static Level level = Level::Debug;
    return level;
}
std::size_t &fileMaxRef() {
    static std::size_t max = 10u * 1024 * 1024;
    return max;
}
std::size_t &fileBytesRef() {
    static std::size_t bytes = 0;
    return bytes;
}
bool &fileOnRef() {
    static bool on = false;
    return on;
}

void writeToFile(const std::string &line) {
    std::lock_guard<std::mutex> lock(fileMutex());
    if (!fileOnRef()) return;
    if (fileBytesRef() + line.size() + 1 > fileMaxRef()) {
        // ротация: текущий файл уходит в <path>.1, запись начинается заново
        fileRef().close();
        const std::string path = filePathRef();
        std::rename(path.c_str(), (path + ".1").c_str());
        fileRef().open(path, std::ios::app);
        fileBytesRef() = 0;
    }
    fileRef() << line << '\n';
    fileRef().flush();
    fileBytesRef() += line.size() + 1;
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
        case Action::ChangeSubscription: return "change_subscription";
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

Level parseLevel(const std::string &name) {
    if (name == "trace") return Level::Trace;
    if (name == "debug") return Level::Debug;
    if (name == "warning" || name == "warn") return Level::Warning;
    if (name == "error") return Level::Error;
    return Level::Info;
}

namespace {
Level &minLevelRef() {
    static Level level = Level::Info;
    return level;
}
std::mutex &minLevelMutex() {
    static std::mutex m;
    return m;
}
} // namespace

void setFile(const std::string &path, Level level, std::size_t maxBytes) {
    std::lock_guard<std::mutex> lock(fileMutex());
    if (fileRef().is_open()) fileRef().close();
    fileOnRef() = false;
    if (path.empty()) return;
    fileRef().open(path, std::ios::app);
    if (!fileRef().is_open()) {
        std::fprintf(stderr, "logging: не удалось открыть файл журнала %s\n", path.c_str());
        return;
    }
    filePathRef() = path;
    fileLevelRef() = level;
    fileMaxRef() = maxBytes;
    fileRef().seekp(0, std::ios::end);
    const auto size = static_cast<std::size_t>(fileRef().tellp());
    fileBytesRef() = size < 0 ? 0 : size;
    fileOnRef() = true;
}

void closeFile() {
    std::lock_guard<std::mutex> lock(fileMutex());
    if (fileRef().is_open()) fileRef().close();
    fileOnRef() = false;
}

void setMinLevel(Level level) {
    std::lock_guard<std::mutex> lock(minLevelMutex());
    minLevelRef() = level;
}

Level minLevel() {
    std::lock_guard<std::mutex> lock(minLevelMutex());
    return minLevelRef();
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
    if (static_cast<int>(level) < static_cast<int>(minLevel())) return; // фильтр REP_LOG_LEVEL
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
    const bool toFile = [level] {
        std::lock_guard<std::mutex> lock(fileMutex());
        return fileOnRef() && static_cast<int>(level) >= static_cast<int>(fileLevelRef());
    }();
    if (toFile) writeToFile(json);
    // Приёмник читаем под замком, вызываем без него: sink может ходить в БД,
    // и держать мьютекс логгера на время сети нельзя.
    const Sink target = sink();
    if (target) target(e, ts);
}

} // namespace log
} // namespace rep

#include "logging.h"

#include <trantor/utils/Logger.h>

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
int &fileKeepRef() {
    static int keep = 5;
    return keep;
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
        // ротация (ADR-119): <файл> → .1, .1 → .2, …, самый старый удаляется.
        // keep = 0 — архивов не держим, файл просто начинается заново.
        fileRef().close();
        const std::string path = filePathRef();
        const int keep = fileKeepRef();
        if (keep <= 0) {
            std::remove(path.c_str());
        } else {
            std::remove((path + "." + std::to_string(keep)).c_str());
            for (int i = keep - 1; i >= 1; --i) {
                const std::string from = path + "." + std::to_string(i);
                const std::string to = path + "." + std::to_string(i + 1);
                std::rename(from.c_str(), to.c_str());
            }
            std::rename(path.c_str(), (path + ".1").c_str());
        }
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
        case Action::UploadAsset: return "asset_upload";
        case Action::DeleteAsset: return "asset_delete";
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

void setFile(const std::string &path, Level level, std::size_t maxBytes, int keep) {
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
    fileKeepRef() = keep < 0 ? 0 : keep;
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

namespace rep {
namespace log {

namespace {

// Строка drogon: «20261117 05:30:05.548295 UTC 14093 ERROR !!!Pg connection failed: ...».
Level trantorLevel(const std::string &line) {
    if (line.find(" FATAL ") != std::string::npos || line.find(" ERROR ") != std::string::npos)
        return Level::Error;
    if (line.find(" WARN ") != std::string::npos) return Level::Warning;
    if (line.find(" DEBUG ") != std::string::npos) return Level::Debug;
    if (line.find(" TRACE ") != std::string::npos) return Level::Trace;
    return Level::Info;
}

std::string trantorText(const std::string &line) {
    // отрезаем дату, время, PID и уровень: в нашем JSON они уже есть отдельно
    static const char *levels[] = {" FATAL ", " ERROR ", " WARN ", " INFO ", " DEBUG ", " TRACE "};
    size_t best = std::string::npos;
    for (const char *l : levels) {
        const size_t p = line.find(l);
        if (p != std::string::npos && (best == std::string::npos || p < best)) best = p;
    }
    if (best == std::string::npos) return line;
    const size_t start = line.find(' ', best + 1);
    return start == std::string::npos ? line : line.substr(start + 1);
}

// Приёмник drogon может быть вызван из потока, где мы сами пишем в лог (sink аудита
// ходит в PostgreSQL): защищаемся от рекурсии.
thread_local bool insideFrameworkSink = false;

// drogon при недоступной базе пишет одну и ту же строку десятки раз в секунду.
// Склеиваем повторы: пишем не чаще раза в 5 секунд и указываем, сколько подавлено (ADR-125).
// Состояние общее для потоков: drogon пишет из разных, и при thread_local повторы
// не склеивались бы (ADR-125)
std::mutex frameworkMutex;
std::string lastFrameworkText;
std::chrono::steady_clock::time_point lastFrameworkAt;
int suppressedFrameworkLines = 0;

}  // namespace

void installFrameworkSink(Level minLevel) {
    switch (minLevel) {
        case Level::Trace: trantor::Logger::setLogLevel(trantor::Logger::kTrace); break;
        case Level::Debug: trantor::Logger::setLogLevel(trantor::Logger::kDebug); break;
        case Level::Info: trantor::Logger::setLogLevel(trantor::Logger::kInfo); break;
        case Level::Warning: trantor::Logger::setLogLevel(trantor::Logger::kWarn); break;
        case Level::Error: trantor::Logger::setLogLevel(trantor::Logger::kError); break;
    }
    trantor::Logger::setOutputFunction(
        [](const char *msg, const uint64_t len) {
            if (insideFrameworkSink) return;
            insideFrameworkSink = true;
            std::string line(msg ? std::string(msg, len) : std::string());
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            if (!line.empty()) {
                const std::string text = trantorText(line);
                const auto now = std::chrono::steady_clock::now();
                std::string message;
                bool write = false;
                {
                    std::lock_guard<std::mutex> lock(frameworkMutex);
                    if (text == lastFrameworkText &&
                        now - lastFrameworkAt < std::chrono::seconds(5)) {
                        ++suppressedFrameworkLines;
                    } else {
                        message = "drogon: " + text;
                        if (suppressedFrameworkLines > 0) {
                            message += " (повторов подавлено: " +
                                       std::to_string(suppressedFrameworkLines) + ")";
                            suppressedFrameworkLines = 0;
                        }
                        lastFrameworkText = text;
                        lastFrameworkAt = now;
                        write = true;
                    }
                }
                if (write) event(trantorLevel(line), Action::Other, "", message);
            }
            insideFrameworkSink = false;
        },
        []() { std::cout.flush(); });
}

}  // namespace log
}  // namespace rep

// Единая точка логирования (ADR-109).
//
// Все события сервера — и действия пользователей, и технические — проходят через
// rep::log::event(). Сейчас событие уходит JSON-строкой в stdout и в приёмник
// (sink), который пишет журнал аудита; в дальнейшем сюда же добавляются файл,
// SIEM и другие приёмники без изменения мест вызова.
#pragma once

#include <json/json.h>

#include <functional>
#include <sstream>
#include <string>

namespace rep {
namespace log {

// Уровень события. trace/debug — диагностика, info — обычные действия,
// warning — подозрительное, но не сломанное, error — отказ операции.
enum class Level { Trace, Debug, Info, Warning, Error };

// Тип действия: значения enum, а не свободные строки — так их можно фильтровать,
// сопоставлять с политиками безопасности и проверять компилятором.
enum class Action {
    // действия пользователей и системы
    OpenReport,
    ExportReport,
    PrintReport,
    Login,
    Logout,
    CreateUser,
    ChangeUser,
    DeleteUser,
    ChangeRoles,
    ChangeTemplate,
    PublishTemplate,
    ChangeSubscription,
    RunSubscription,
    AccessDenied,
    // технические события сервера
    ServerStart,
    ServerStop,
    SqlSlow,
    SqlError,
    ConnectionError,
    RestError,
    AuditFailed,
    Other // прочие технические сообщения (задел на будущее)
};

const char *levelName(Level level);   // "trace" | "debug" | "info" | "warning" | "error"
// Разбор уровня из настройки (REP_LOG_LEVEL); неизвестное значение — info.
Level parseLevel(const std::string &name);

// Файловый приёмник (REP_LOG_FILE): JSON-строки, дописывание, ротация по размеру.
// Пустой путь выключает файл. Уровень файла задаётся отдельно (REP_LOG_FILE_LEVEL).
void setFile(const std::string &path, Level level, std::size_t maxBytes = 10u * 1024 * 1024);
void closeFile();

// Минимальный уровень для записи (REP_LOG_LEVEL, по умолчанию info).
void setMinLevel(Level level);
Level minLevel();
const char *actionName(Action action); // "open_report", "change_user", ...

struct Event {
    Level level = Level::Info;
    Action action = Action::OpenReport;
    std::string user;                                  // пусто — системное/анонимное
    std::string message;                               // текст для человека
    Json::Value context = Json::Value(Json::objectValue); // произвольные поля (reportId, ip, ms)
};

// Время события в ISO-8601 UTC с миллисекундами: 2026-11-14T10:00:00.123Z.
std::string timestampNow();

// Одна строка JSON: {"ts","level","action","user","message","context"}.
std::string formatJson(const Event &event, const std::string &ts);

// Приёмник (БД, файл, SIEM). Вызывается после записи в stdout; может быть пустым.
using Sink = std::function<void(const Event &, const std::string &ts)>;
void setSink(Sink sink);
Sink sink();

// Записать событие: JSON в stdout и передача приёмнику. Потокобезопасно.
void event(Level level, Action action, const std::string &user, const std::string &message,
           const Json::Value &context = Json::Value(Json::objectValue));

// Потоковый адаптер для привычной записи: REP_LOG_INFO(ServerStart, "") << "порт " << port;
// Текст собирается через operator<< и уходит в event() при разрушении объекта.
class Stream {
public:
    Stream(Level level, Action action, std::string user)
        : level_(level), action_(action), user_(std::move(user)) {}
    ~Stream() { event(level_, action_, user_, stream_.str()); }

    template <typename T>
    Stream &operator<<(const T &value) {
        stream_ << value;
        return *this;
    }

private:
    Level level_;
    Action action_;
    std::string user_;
    std::ostringstream stream_;
};

#define REP_LOG_TRACE(action, user) rep::log::Stream(rep::log::Level::Trace, rep::log::Action::action, user)
#define REP_LOG_DEBUG(action, user) rep::log::Stream(rep::log::Level::Debug, rep::log::Action::action, user)
#define REP_LOG_INFO(action, user) rep::log::Stream(rep::log::Level::Info, rep::log::Action::action, user)
#define REP_LOG_WARN(action, user) rep::log::Stream(rep::log::Level::Warning, rep::log::Action::action, user)
#define REP_LOG_ERROR(action, user) rep::log::Stream(rep::log::Level::Error, rep::log::Action::action, user)

} // namespace log
} // namespace rep

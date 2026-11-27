// Журнал действий (ADR-52, ADR-109): запись события, ретеншен и разбор реквизитов запроса.
// Вынесено из main.cc (ADR-193).
#pragma once

#include <string>

#include <drogon/HttpRequest.h>

namespace rep {
namespace app {

// Ретеншен журналов (ADR-59): сколько записей удалено за прогон.
struct PurgeResult {
    bool ok = false;
    long audit = 0;
    long runs = 0;
    std::string error;
};

// Логин текущего пользователя (пусто — системное/анонимное событие).
std::string actorOf(const drogon::HttpRequestPtr &req);

// Совместимость со старыми вызовами: строка «template.create» превращается в enum-действие,
// объект и ip уходят в контекст события (ADR-109).
void auditEvent(const std::string &actor, const std::string &action, const std::string &objectType,
                const std::string &objectId, const std::string &detailsJson, const std::string &ip);

// Непосредственная запись строки журнала — используется приёмником логов (ADR-109).
void writeAuditRow(const std::string &actor, const std::string &action, const std::string &objectType,
                   const std::string &objectId, const std::string &detailsJson, const std::string &ip);

// Удаляет записи журналов старше N дней (0 — хранение бессрочное).
PurgeResult purgeOldLogs(int days);

}  // namespace app
}  // namespace rep

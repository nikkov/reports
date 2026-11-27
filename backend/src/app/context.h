// Общее состояние сервера одним объектом (ADR-193). Раньше каждый обработчик захватывал
// в лямбду десяток переменных (пул, шаблоны, мьютекс, кэш REST, реестр соединений) — при
// разделении main.cc на модули эти захваты пришлось бы тащить в каждый файл. Теперь модуль
// получает AppContext и берёт из него нужное.
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <drogon/orm/DbClient.h>

#include "app/http.h"
#include "app/template_types.h"
#include "auth.h"
#include "cache.h"
#include "connections.h"
#include "rest_client.h"

namespace rep {
namespace app {

// Все поля — указатели/ссылки на объекты, которыми владеет main(): контекст не управляет
// временем жизни, он только связывает части сервера. Создаётся один раз после настройки.
struct AppContext {
    std::shared_ptr<drogon::orm::DbClient> db;      // основная учётная запись (запись шаблонов)
    std::shared_ptr<drogon::orm::DbClient> dataDb;  // чтение данных отчётов (может быть read-only)
    rep::ConnRegistry *conn = nullptr;              // источники данных шаблонов (ADR-76)
    rep::RestCache *rest = nullptr;                 // кэш ответов REST (ADR-104)

    // Шаблоны и черновики: один мьютекс на обе карты (ADR-153)
    rep::http::TracedMutex *templatesMutex = nullptr;
    std::map<std::string, TemplateInfo> *templates = nullptr;
    std::map<std::string, std::string> *templatesRaw = nullptr;
    std::map<std::string, TemplateInfo> *draftsParsed = nullptr;
    std::map<std::string, std::string> *draftsRaw = nullptr;
    std::map<std::string, std::string> *draftsErrors = nullptr; // ошибки разбора черновика

    rep::ResultCache *resultCache = nullptr;        // кэш результатов наборов (REP_CACHE_TTL_SECONDS)
    std::atomic<std::uint64_t> *templatesVersion = nullptr; // версия шаблонов: инвалидация кэшей

    std::size_t maxRows = 50000;   // потолок строк на клиенте (REP_MAX_ROWS)
    bool authEnabled = true;       // REP_AUTH=0 — работа без аутентификации
    rep::AuthMode authMode = rep::AuthMode::Off; // режим: off / session / proxy
    int sessionTtlHours = 12;      // время жизни сессии (REP_SESSION_TTL_HOURS)
    std::string proxyHeader;       // заголовок с логином от доверенного прокси (ADR-54)
    int logRetentionDays = 90;     // глубина хранения журналов (REP_LOG_RETENTION_DAYS)
    bool readOnlyRole = false;     // данные читаются под отдельной ролью (ADR-33)
    bool adminEnabled = false;     // REP_ADMIN=1 — включена админка

    // Снимок шаблона под мьютексом: обработчики не должны держать замок на всё время работы.
    std::shared_ptr<TemplateInfo> templateSnapshot(const std::string &id) const {
        if (!templates || !templatesMutex) return nullptr;
        std::lock_guard<rep::http::TracedMutex> lock(*templatesMutex);
        const auto it = templates->find(id);
        if (it == templates->end()) return nullptr;
        return std::make_shared<TemplateInfo>(it->second);
    }

    std::string templateRaw(const std::string &id) const {
        if (!templatesRaw || !templatesMutex) return std::string();
        std::lock_guard<rep::http::TracedMutex> lock(*templatesMutex);
        const auto it = templatesRaw->find(id);
        return it == templatesRaw->end() ? std::string() : it->second;
    }
};

}  // namespace app
}  // namespace rep

// REST-источник шаблона (ADR-104): GET, JSON, выборка массива по `rowsPath`,
// явные колонки с типами. Секреты — только через ${ENV} в заголовках; хосты
// ограничены allowlist'ом REP_REST_ALLOW_HOSTS (защита от SSRF).
#pragma once

#include <json/json.h>

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "connections.h"
#include "dataset_rows.h"

namespace rep {

struct RestColumn {
    std::string field;
    std::string type = "text"; // text | int | float | date | datetime | bool
    std::string title;
    std::string as; // имя в результате; пусто — field
};

struct RestSpec {
    std::string path;                     // /api/regions
    std::map<std::string, std::string> query;
    std::string rowsPath;                 // items | data.rows | пусто — корень
    std::vector<RestColumn> columns;
};

// Разбор datasets.<key>.rest. false + текст ошибки (семантику дополнительно проверяет валидатор).
bool parseRestSpec(const Json::Value &rest, RestSpec &out, std::string &error);

// Проверка хоста из baseUrl по списку разрешённых (host или host:port). Пустой список — запрещено всё.
bool restHostAllowed(const std::string &baseUrl, const std::vector<std::string> &allowed,
                     std::string &host, std::string &error);

std::vector<std::string> parseAllowHosts(const std::string &value);

// Разбор тела ответа в строки объявленных колонок. Типы приводятся к объявленным.
bool parseRestRows(const std::string &body, const RestSpec &spec, DatasetRows &out, std::string &error);

// Кеш ответов REST (короткий TTL): ключ — адрес, путь, query и хеш заголовков.
class RestCache {
public:
    RestCache(size_t maxEntries, int defaultTtlSeconds);

    bool get(const std::string &key, std::string &body);
    void put(const std::string &key, const std::string &body, int ttlSeconds);

    size_t size();

private:
    struct Entry {
        std::string body;
        std::chrono::steady_clock::time_point expires;
    };
    std::mutex mutex_;
    std::map<std::string, Entry> entries_;
    size_t maxEntries_;
    int defaultTtl_;
};

// Асинхронный GET. Колбэки вызываются в потоке drogon.
void restFetch(const ConnSpec &conn, const RestSpec &spec, RestCache &cache,
               std::function<void(const DatasetRows &)> onOk,
               std::function<void(const std::string &code, const std::string &message)> onErr);

} // namespace rep

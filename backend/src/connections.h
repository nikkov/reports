#pragma once

// Источники данных шаблона (ADR-76).
//
// Все параметры, которые касаются запросов к БД, живут в блоке `connections` шаблона:
// адрес сервера, порт, база, пользователь, схема, режим SSL и имя переменной окружения
// с паролем. Пароль в шаблоне не хранится никогда — только `passwordEnv` (решение PO).
// Наборы данных и справочники параметров ссылаются на соединение по ключу:
//   datasets.<key>.connection = "finance"
//   params[].source.connection = "finance"
// Ключ `default` (а также отсутствие поля) означает подключение сервера из окружения
// (PGHOST… и пул только-чтения), поэтому старые шаблоны работают без правок.
//
// Блок спроектирован с запасом на другие источники: `kind` — это дискриминатор, сейчас
// поддержан `postgres`, а `rest` зарезервирован (валидатор его принимает, исполнение
// отвечает понятной ошибкой «ещё не поддерживается»).

#include <json/json.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <drogon/orm/DbClient.h>

namespace rep {

namespace orm = drogon::orm;

struct ConnSpec {
    std::string key;
    std::string kind = "postgres";
    // postgres
    std::string host;
    std::string port = "5432";
    std::string database;
    std::string user;
    std::string passwordEnv;
    std::string schema;      // search_path сессии; пусто — как настроено в роли
    std::string sslMode;     // disable | prefer | require | verify-ca | verify-full
    int connectTimeout = 5;  // секунды
    int statementTimeoutMs = 0;
    // rest (ADR-104)
    std::string baseUrl;
    std::map<std::string, std::string> headers; // значения могут быть ${ENV} (секреты — только так)
    int timeoutMs = 5000;
    int cacheSeconds = 0;
};

// Подстановка ${NAME} в строковом поле источника (ADR-77). Незаданная переменная — ошибка.
std::string expandEnvValue(const std::string &value, const std::string &key, std::string &error);

// Разбор блока `connections`. Возвращает false, если блок не объект или содержит
// элемент не-объект; семантику полей проверяет валидатор шаблона (клиент и сервер).
bool parseConnections(const Json::Value &connections, std::map<std::string, ConnSpec> &out,
                      std::string &error);

// Клиенты внешних источников: создаются лениво и переиспользуются по строке подключения.
// `default` и пустой ключ отдают fallback (пул только-чтения сервера), поэтому поведение
// шаблонов без блока connections не меняется.
class ConnRegistry {
public:
    // Возвращает nullptr и заполняет error, если источник недоступен: неизвестный ключ,
    // зарезервированный REST, отсутствующая переменная окружения с паролем, сбой клиента.
    orm::DbClientPtr resolve(const std::map<std::string, ConnSpec> &connections,
                             const std::string &key, const orm::DbClientPtr &fallback,
                             std::string &error);

    // DSN для проверки прав и диагностики: пароль в него попадает, наружу не отдаётся.
    static std::string dsnFor(const ConnSpec &spec, std::string &error);

    size_t size();

private:
    std::mutex mutex_;
    std::map<std::string, orm::DbClientPtr> cache_;
};

}  // namespace rep

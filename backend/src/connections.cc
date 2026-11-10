#include "connections.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "env.h"

namespace rep {

namespace {

// Значение для libpq-строки подключения: оборачиваем в одинарные кавычки и экранируем
// обратную косую черту и кавычку, иначе пробел в пути схемы ломает разбор DSN.
std::string quote(const std::string &value) {
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\\' || ch == '\'') out += '\\';
        out += ch;
    }
    out += '\'';
    return out;
}

// Значение может ссылаться на окружение: "${PGHOST}", "${REP_RO_DB_USER}".
// Так блок connections остаётся явным (в шаблоне видно, что и откуда берётся),
// но переносимым: адрес и база не зашиты на конкретный стенд.
bool isEnvRef(const std::string &value, std::string &name) {
    if (value.size() < 4 || value.rfind("${", 0) != 0 || value.back() != '}') return false;
    name = value.substr(2, value.size() - 3);
    if (name.empty()) return false;
    const auto first = static_cast<unsigned char>(name[0]);
    if (!std::isalpha(first) && first != '_') return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_';
    });
}

bool isIdentifier(const std::string &value) {
    if (value.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(value[0])) || value[0] == '_')) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_';
    });
}

}  // namespace

bool parseConnections(const Json::Value &connections, std::map<std::string, ConnSpec> &out,
                      std::string &error) {
    out.clear();
    if (connections.isNull()) return true;
    if (!connections.isObject()) {
        error = "connections должен быть объектом";
        return false;
    }
    for (auto it = connections.begin(); it != connections.end(); ++it) {
        if (!it->isObject()) {
            error = "connections." + it.name() + " должен быть объектом";
            return false;
        }
        ConnSpec spec;
        spec.key = it.name();
        spec.kind = it->get("kind", "postgres").asString();
        spec.host = it->get("host", "").asString();
        spec.port = it->get("port", "5432").asString();
        spec.database = it->get("database", "").asString();
        spec.user = it->get("user", "").asString();
        spec.passwordEnv = it->get("passwordEnv", "").asString();
        spec.schema = it->get("schema", "").asString();
        spec.sslMode = it->get("sslmode", it->get("sslMode", "")).asString();
        if (it->isMember("connectTimeout")) spec.connectTimeout = (*it)["connectTimeout"].asInt();
        if (it->isMember("statementTimeoutMs")) {
            spec.statementTimeoutMs = (*it)["statementTimeoutMs"].asInt();
        }
        spec.baseUrl = it->get("baseUrl", "").asString();
        out[spec.key] = spec;
    }
    return true;
}

namespace {

// Подстановка ${NAME} в строковых полях источника. Значение берётся из окружения
// процесса; незаданная переменная — ошибка, а не пустая строка: иначе подключение
// молча уйдёт на другой сервер (localhost вместо нужного).
std::string expandEnv(const std::string &value, const std::string &key, std::string &error) {
    if (value.find("${") == std::string::npos) return value;
    std::string out;
    size_t i = 0;
    while (i < value.size()) {
        if (value[i] == '$' && i + 1 < value.size() && value[i + 1] == '{') {
            const size_t close = value.find('}', i + 2);
            if (close == std::string::npos) {
                error = "источник '" + key + "': незакрытая подстановка ${…}";
                return {};
            }
            const std::string name = value.substr(i + 2, close - i - 2);
            const auto first = name.empty() ? 0 : static_cast<unsigned char>(name[0]);
            const bool valid = !name.empty() && (std::isalpha(first) || first == '_') &&
                               std::all_of(name.begin(), name.end(), [](unsigned char ch) {
                                   return std::isalnum(ch) || ch == '_';
                               });
            if (!valid) {
                error = "источник '" + key + "': в подстановке ${…} ожидается имя переменной окружения";
                return {};
            }
            const std::string resolved = envOr(name, "");
            if (resolved.empty()) {
                error = "источник '" + key + "': не задана переменная окружения " + name;
                return {};
            }
            out += resolved;
            i = close + 1;
            continue;
        }
        out += value[i];
        ++i;
    }
    return out;
}

}  // namespace

std::string ConnRegistry::dsnFor(const ConnSpec &spec, std::string &error) {
    error.clear();
    if (spec.kind != "postgres") {
        if (spec.kind == "rest") {
            error = "источник '" + spec.key + "' типа rest зарезервирован, но ещё не поддерживается";
        } else {
            error = "источник '" + spec.key + "': неизвестный тип '" + spec.kind + "'";
        }
        return {};
    }
    // адрес и база могут ссылаться на окружение: ${PGHOST}, ${REP_RO_DB_USER} (ADR-77)
    const std::string host = expandEnv(spec.host, spec.key, error);
    if (!error.empty()) return {};
    const std::string port = spec.port.empty() ? "5432" : expandEnv(spec.port, spec.key, error);
    if (!error.empty()) return {};
    const std::string database = expandEnv(spec.database, spec.key, error);
    if (!error.empty()) return {};
    const std::string user = expandEnv(spec.user, spec.key, error);
    if (!error.empty()) return {};
    const std::string schema = expandEnv(spec.schema, spec.key, error);
    if (!error.empty()) return {};
    const std::string ssl = expandEnv(spec.sslMode, spec.key, error);
    if (!error.empty()) return {};

    if (host.empty()) { error = "источник '" + spec.key + "': не задан host"; return {}; }
    if (database.empty()) { error = "источник '" + spec.key + "': не задана database"; return {}; }
    if (user.empty()) { error = "источник '" + spec.key + "': не задан user"; return {}; }
    if (spec.passwordEnv.empty()) {
        error = "источник '" + spec.key + "': секреты в шаблоне не хранятся — укажите passwordEnv";
        return {};
    }
    if (!isIdentifier(spec.passwordEnv)) {
        error = "источник '" + spec.key + "': passwordEnv должен быть именем переменной окружения";
        return {};
    }
    const std::string password = envOr(spec.passwordEnv, "");
    if (password.empty()) {
        error = "источник '" + spec.key + "': не задана переменная окружения " + spec.passwordEnv;
        return {};
    }

    std::string options = "-c default_transaction_read_only=on";
    if (!schema.empty()) {
        if (!isIdentifier(schema)) {
            error = "источник '" + spec.key + "': schema должен быть идентификатором";
            return {};
        }
        options += " -c search_path=" + schema;
    }
    if (spec.statementTimeoutMs > 0) {
        options += " -c statement_timeout=" + std::to_string(spec.statementTimeoutMs);
    }

    std::string dsn = "host=" + quote(host) + " port=" + quote(port) +
                      " dbname=" + quote(database) + " user=" + quote(user) +
                      " password=" + quote(password) + " options=" + quote(options) +
                      " connect_timeout=" + std::to_string(spec.connectTimeout > 0 ? spec.connectTimeout : 5);
    if (!ssl.empty()) dsn += " sslmode=" + quote(ssl);
    return dsn;
}

orm::DbClientPtr ConnRegistry::resolve(const std::map<std::string, ConnSpec> &connections,
                                       const std::string &key, const orm::DbClientPtr &fallback,
                                       std::string &error) {
    error.clear();
    if (key.empty() || key == "default") return fallback;

    const auto it = connections.find(key);
    if (it == connections.end()) {
        error = "шаблон не объявляет соединение '" + key + "' в блоке connections";
        return nullptr;
    }
    std::string dsnError;
    const std::string dsn = dsnFor(it->second, dsnError);
    if (dsn.empty()) {
        error = dsnError;
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto cached = cache_.find(dsn);
    if (cached != cache_.end()) return cached->second;
    if (cache_.size() >= 16) {
        error = "слишком много источников данных: не больше 16 на процесс";
        return nullptr;
    }
    auto client = orm::DbClient::newPgClient(dsn, 2);
    if (!client) {
        error = "не удалось создать клиент источника '" + key + "'";
        return nullptr;
    }
    cache_[dsn] = client;
    return client;
}

size_t ConnRegistry::size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

}  // namespace rep

#include "connections.h"

#include <algorithm>
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

std::string ConnRegistry::dsnFor(const ConnSpec &spec, std::string &error) {
    if (spec.kind != "postgres") {
        if (spec.kind == "rest") {
            error = "источник '" + spec.key + "' типа rest зарезервирован, но ещё не поддерживается";
        } else {
            error = "источник '" + spec.key + "': неизвестный тип '" + spec.kind + "'";
        }
        return {};
    }
    if (spec.host.empty()) { error = "источник '" + spec.key + "': не задан host"; return {}; }
    if (spec.database.empty()) { error = "источник '" + spec.key + "': не задана database"; return {}; }
    if (spec.user.empty()) { error = "источник '" + spec.key + "': не задан user"; return {}; }
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
    if (!spec.schema.empty()) {
        if (!isIdentifier(spec.schema)) {
            error = "источник '" + spec.key + "': schema должен быть идентификатором";
            return {};
        }
        options += " -c search_path=" + spec.schema;
    }
    if (spec.statementTimeoutMs > 0) {
        options += " -c statement_timeout=" + std::to_string(spec.statementTimeoutMs);
    }

    std::string dsn = "host=" + quote(spec.host) + " port=" + quote(spec.port.empty() ? "5432" : spec.port) +
                      " dbname=" + quote(spec.database) + " user=" + quote(spec.user) +
                      " password=" + quote(password) + " options=" + quote(options) +
                      " connect_timeout=" + std::to_string(spec.connectTimeout > 0 ? spec.connectTimeout : 5);
    if (!spec.sslMode.empty()) dsn += " sslmode=" + quote(spec.sslMode);
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

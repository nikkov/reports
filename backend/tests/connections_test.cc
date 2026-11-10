// Источники данных шаблона (ADR-76): разбор блока connections и сборка строки подключения.
//
// Проверяется то, что важно для эксплуатации: пароль берётся только из окружения,
// сессия открывается только на чтение, REST-источник отвечает понятной ошибкой,
// а неизвестный ключ не превращается в тихий запрос к основному серверу.
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

#include "connections.h"

namespace {

Json::Value parse(const std::string &text) {
    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream in(text);
    Json::parseFromStream(b, in, &root, &errs);
    return root;
}

const char *kEnvName = "REP_TEST_CONN_PASSWORD";

struct EnvGuard {
    explicit EnvGuard(const char *value) { setenv(kEnvName, value, 1); }
    ~EnvGuard() { unsetenv(kEnvName); }
};

}  // namespace

TEST_CASE("connections: блок читается в спецификации") {
    const auto root = parse(R"json({
        "default": {"kind": "postgres"},
        "finance": {"kind": "postgres", "host": "10.0.0.5", "port": "5433",
                    "database": "finance", "user": "reader",
                    "passwordEnv": "REP_TEST_CONN_PASSWORD", "schema": "public",
                    "sslmode": "require", "connectTimeout": 7, "statementTimeoutMs": 1500},
        "crm": {"kind": "rest", "baseUrl": "https://crm.example/api"}
    })json");
    std::map<std::string, rep::ConnSpec> conns;
    std::string err;
    REQUIRE(rep::parseConnections(root, conns, err));
    REQUIRE(conns.size() == 3);
    REQUIRE(conns["finance"].host == "10.0.0.5");
    REQUIRE(conns["finance"].port == "5433");
    REQUIRE(conns["finance"].passwordEnv == kEnvName);
    REQUIRE(conns["finance"].connectTimeout == 7);
    REQUIRE(conns["finance"].statementTimeoutMs == 1500);
    REQUIRE(conns["crm"].kind == "rest");
    REQUIRE(conns["crm"].baseUrl == "https://crm.example/api");
}

TEST_CASE("connections: не-объект отвергается") {
    std::map<std::string, rep::ConnSpec> conns;
    std::string err;
    REQUIRE_FALSE(rep::parseConnections(parse(R"json({"a": 1})json"), conns, err));
    REQUIRE(err == "connections.a должен быть объектом");
}

TEST_CASE("connections: DSN читает пароль из окружения и закрывает сессию на чтение") {
    EnvGuard guard("s3cret");
    rep::ConnSpec spec;
    spec.key = "finance";
    spec.host = "10.0.0.5";
    spec.port = "5433";
    spec.database = "finance";
    spec.user = "reader";
    spec.passwordEnv = kEnvName;
    spec.schema = "public";
    spec.statementTimeoutMs = 2000;

    std::string err;
    const std::string dsn = rep::ConnRegistry::dsnFor(spec, err);
    REQUIRE(err.empty());
    REQUIRE(dsn.find("host='10.0.0.5'") != std::string::npos);
    REQUIRE(dsn.find("port='5433'") != std::string::npos);
    REQUIRE(dsn.find("dbname='finance'") != std::string::npos);
    REQUIRE(dsn.find("password='s3cret'") != std::string::npos);
    // запись запрещена на уровне сессии: шаблон не может менять данные источника
    REQUIRE(dsn.find("default_transaction_read_only=on") != std::string::npos);
    REQUIRE(dsn.find("search_path=public") != std::string::npos);
    REQUIRE(dsn.find("statement_timeout=2000") != std::string::npos);
}

TEST_CASE("connections: незаданная переменная окружения — понятная ошибка") {
    unsetenv(kEnvName);
    rep::ConnSpec spec;
    spec.key = "finance";
    spec.host = "h";
    spec.database = "d";
    spec.user = "u";
    spec.passwordEnv = kEnvName;

    std::string err;
    REQUIRE(rep::ConnRegistry::dsnFor(spec, err).empty());
    REQUIRE(err.find(kEnvName) != std::string::npos);
}

TEST_CASE("connections: пароль в шаблоне не принимается") {
    rep::ConnSpec spec;
    spec.key = "finance";
    spec.host = "h";
    spec.database = "d";
    spec.user = "u";  // passwordEnv не задан
    std::string err;
    REQUIRE(rep::ConnRegistry::dsnFor(spec, err).empty());
    REQUIRE(err.find("passwordEnv") != std::string::npos);
}

TEST_CASE("connections: REST зарезервирован и отвечает ошибкой") {
    rep::ConnSpec spec;
    spec.key = "crm";
    spec.kind = "rest";
    spec.baseUrl = "https://crm.example/api";
    std::string err;
    REQUIRE(rep::ConnRegistry::dsnFor(spec, err).empty());
    REQUIRE(err.find("ещё не поддерживается") != std::string::npos);
}

TEST_CASE("connections: адрес и база подставляются из окружения") {
    EnvGuard pw("s3cret");
    setenv("REP_TEST_CONN_HOST", "10.1.2.3", 1);
    setenv("REP_TEST_CONN_DB", "warehouse", 1);
    setenv("REP_TEST_CONN_USER", "reader", 1);
    rep::ConnSpec spec;
    spec.key = "main";
    spec.host = "${REP_TEST_CONN_HOST}";
    spec.port = "${REP_TEST_CONN_PORT_UNSET_FALLBACK}";
    spec.database = "${REP_TEST_CONN_DB}";
    spec.user = "${REP_TEST_CONN_USER}";
    spec.passwordEnv = kEnvName;
    spec.schema = "reports_examples";

    std::string err;
    const std::string dsn = rep::ConnRegistry::dsnFor(spec, err);
    // порт не подставлен — источник целиком не собирается, но и не превращается в localhost
    REQUIRE(dsn.empty());
    REQUIRE(err.find("REP_TEST_CONN_PORT_UNSET_FALLBACK") != std::string::npos);

    spec.port = "5432";
    const std::string ok = rep::ConnRegistry::dsnFor(spec, err);
    REQUIRE(err.empty());
    REQUIRE(ok.find("host='10.1.2.3'") != std::string::npos);
    REQUIRE(ok.find("dbname='warehouse'") != std::string::npos);
    REQUIRE(ok.find("user='reader'") != std::string::npos);

    unsetenv("REP_TEST_CONN_HOST");
    unsetenv("REP_TEST_CONN_DB");
    unsetenv("REP_TEST_CONN_USER");
}

TEST_CASE("connections: подстановка смешивается с текстом — только целым значением в шаблоне") {
    EnvGuard pw("s3cret");
    setenv("REP_TEST_CONN_HOST", "db.internal", 1);
    rep::ConnSpec spec;
    spec.key = "main";
    spec.host = "${REP_TEST_CONN_HOST}";     // валидатор разрешает только это
    spec.database = "d";
    spec.user = "u";
    spec.passwordEnv = kEnvName;
    std::string err;
    const std::string dsn = rep::ConnRegistry::dsnFor(spec, err);
    REQUIRE(err.empty());
    REQUIRE(dsn.find("host='db.internal'") != std::string::npos);
    unsetenv("REP_TEST_CONN_HOST");
}

TEST_CASE("connections: неизвестный ключ не уходит в основной сервер") {
    rep::ConnRegistry registry;
    std::string err;
    const auto client = registry.resolve({}, "finance", nullptr, err);
    REQUIRE(client == nullptr);
    REQUIRE(err.find("не объявляет соединение 'finance'") != std::string::npos);
}

TEST_CASE("connections: ключ default отдаёт пул сервера") {
    rep::ConnRegistry registry;
    // Клиент не создаётся: default — это пул сервера, переданный вызывающим.
    // Поэтому без fallback здесь nullptr, но без ошибки и без записи в кэш.
    std::string err;
    REQUIRE(registry.resolve({}, "default", nullptr, err) == nullptr);
    REQUIRE(err.empty());
    REQUIRE(registry.resolve({}, "", nullptr, err) == nullptr);
    REQUIRE(err.empty());
    REQUIRE(registry.size() == 0);
}

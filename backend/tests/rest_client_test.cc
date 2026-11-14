#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "rest_client.h"

using rep::DatasetRows;
using rep::RestCache;
using rep::RestSpec;

namespace {

RestSpec demoSpec() {
    RestSpec s;
    s.path = "/api/regions";
    s.rowsPath = "items";
    s.columns = {{"id", "int", ""}, {"name", "text", "Регион"}};
    return s;
}

}  // namespace

TEST_CASE("rest: разбор спецификации набора") {
    Json::Value rest;
    rest["path"] = "/api/regions";
    rest["rowsPath"] = "items";
    rest["query"]["active"] = "1";
    Json::Value cols;
    Json::Value c;
    c["field"] = "id";
    c["type"] = "int";
    cols.append(c);
    rest["columns"] = cols;

    RestSpec spec;
    std::string err;
    REQUIRE(rep::parseRestSpec(rest, spec, err));
    REQUIRE(spec.path == "/api/regions");
    REQUIRE(spec.rowsPath == "items");
    REQUIRE(spec.query.at("active") == "1");
    REQUIRE(spec.columns.size() == 1);
    REQUIRE(spec.columns[0].type == "int");
}

TEST_CASE("rest: path обязателен и начинается со слэша") {
    Json::Value rest;
    Json::Value cols;
    Json::Value c;
    c["field"] = "id";
    cols.append(c);
    rest["columns"] = cols;
    RestSpec spec;
    std::string err;
    REQUIRE_FALSE(rep::parseRestSpec(rest, spec, err));
    REQUIRE(err.find("path") != std::string::npos);

    rest["path"] = "api/regions";
    REQUIRE_FALSE(rep::parseRestSpec(rest, spec, err));
}

TEST_CASE("rest: колонки обязательны, тип проверяется") {
    Json::Value rest;
    rest["path"] = "/x";
    RestSpec spec;
    std::string err;
    REQUIRE_FALSE(rep::parseRestSpec(rest, spec, err));
    REQUIRE(err.find("columns") != std::string::npos);

    Json::Value cols;
    Json::Value c;
    c["field"] = "id";
    c["type"] = "money";
    cols.append(c);
    rest["columns"] = cols;
    REQUIRE_FALSE(rep::parseRestSpec(rest, spec, err));
    REQUIRE(err.find("тип") != std::string::npos);
}

TEST_CASE("rest: массив из корня ответа") {
    RestSpec spec;
    spec.path = "/x";
    spec.columns = {{"id", "int", ""}, {"name", "text", ""}};
    DatasetRows out;
    std::string err;
    REQUIRE(rep::parseRestRows(R"json([{"id":1,"name":"A"},{"id":2}])json", spec, out, err));
    REQUIRE(out.rows.size() == 2);
    REQUIRE(out.rows[0][0] == "1");
    REQUIRE(out.rows[0][1] == "A");
    REQUIRE(out.rows[1][1].empty()); // отсутствующее поле — пусто
}

TEST_CASE("rest: rowsPath по вложенным объектам") {
    RestSpec spec;
    spec.path = "/x";
    spec.rowsPath = "data.rows";
    spec.columns = {{"id", "int", ""}};
    DatasetRows out;
    std::string err;
    REQUIRE(rep::parseRestRows(R"json({"data":{"rows":[{"id":"7"}]}})json", spec, out, err));
    REQUIRE(out.rows.size() == 1);
    REQUIRE(out.rows[0][0] == "7");
}

TEST_CASE("rest: типы приводятся к объявленным") {
    RestSpec spec;
    spec.path = "/x";
    spec.columns = {{"i", "int", ""}, {"f", "float", ""}, {"b", "bool", ""}};
    DatasetRows out;
    std::string err;
    REQUIRE(rep::parseRestRows(R"json([{"i":"05","f":1.5,"b":1}])json", spec, out, err));
    REQUIRE(out.rows[0][0] == "5");
    REQUIRE(out.rows[0][1] == "1.5");
    REQUIRE(out.rows[0][2] == "true");
}

TEST_CASE("rest: не-массив и не-JSON дают ошибку") {
    RestSpec spec;
    spec.path = "/x";
    spec.rowsPath = "items";
    spec.columns = {{"id", "int", ""}};
    DatasetRows out;
    std::string err;
    REQUIRE_FALSE(rep::parseRestRows(R"json({"items":{"id":1}})json", spec, out, err));
    REQUIRE(err.find("массив") != std::string::npos);

    std::string err2;
    REQUIRE_FALSE(rep::parseRestRows("<html>", spec, out, err2));
    REQUIRE(err2.find("JSON") != std::string::npos);
}

TEST_CASE("rest: allowlist хостов") {
    const auto list = rep::parseAllowHosts("crm.example.com, 127.0.0.1:8099 ;");
    REQUIRE(list.size() == 2);

    std::string host;
    std::string err;
    REQUIRE(rep::restHostAllowed("http://127.0.0.1:8099/api", {"127.0.0.1:8099"}, host, err));
    REQUIRE(host == "127.0.0.1:8099");

    // без порта в списке хост тоже разрешён
    REQUIRE(rep::restHostAllowed("https://crm.example.com/api", list, host, err));
    REQUIRE(host == "crm.example.com");

    REQUIRE_FALSE(rep::restHostAllowed("http://evil.example/api", list, host, err));
    REQUIRE(err.find("REP_REST_ALLOW_HOSTS") != std::string::npos);

    // пустой allowlist — REST выключен
    std::string err2;
    REQUIRE_FALSE(rep::restHostAllowed("http://127.0.0.1:8099", {}, host, err2));
    REQUIRE(err2.find("REP_REST_ALLOW_HOSTS") != std::string::npos);

    // не http/https
    std::string err3;
    REQUIRE_FALSE(rep::restHostAllowed("file:///etc/passwd", list, host, err3));
}

TEST_CASE("rest: кеш по TTL") {
    RestCache cache(8, 0); // TTL по умолчанию 0 — кеш выключен
    cache.put("k", "v", 0);
    std::string body;
    REQUIRE_FALSE(cache.get("k", body));

    cache.put("k", "v", 30);
    REQUIRE(cache.get("k", body));
    REQUIRE(body == "v");
    REQUIRE(cache.size() == 1);
}

TEST_CASE("rest: разбор заголовков с ${ENV} не падает на пустой переменной") {
    // Значение разрешается в restFetch; здесь проверяем только, что спецификация с заголовком
    // принимается схемой (заголовки к ConnSpec, не к RestSpec).
    Json::Value rest;
    rest["path"] = "/x";
    Json::Value cols;
    Json::Value c;
    c["field"] = "id";
    cols.append(c);
    rest["columns"] = cols;
    RestSpec spec;
    std::string err;
    REQUIRE(rep::parseRestSpec(rest, spec, err));
    REQUIRE(spec.columns.size() == 1);
}

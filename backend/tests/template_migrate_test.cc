#include <catch2/catch_test_macros.hpp>

#include <string>

#include <json/json.h>

#include "template_migrate.h"

namespace {

Json::Value parse(const std::string &raw) {
    Json::CharReaderBuilder rb;
    Json::Value root;
    std::string err;
    std::istringstream in(raw);
    Json::parseFromStream(rb, in, &root, &err);
    return root;
}

} // namespace

TEST_CASE("миграция: шаблон без version получает актуальную версию") {
    auto doc = parse(R"json({"id":"t","title":"T","blocks":[{"type":"text","text":"x"}]})json");
    const auto result = rep::migrateTemplate(doc);

    REQUIRE(result.ok);
    REQUIRE(result.fromVersion == 0);
    REQUIRE(result.toVersion == rep::kCurrentTemplateVersion);
    REQUIRE(doc["version"].asInt() == rep::kCurrentTemplateVersion);
    REQUIRE(result.steps.size() >= 2); // 0→1 и 1→2
}

TEST_CASE("миграция: у диаграммы версии 1 появляется chartType") {
    auto doc = parse(R"json({"version":1,"id":"t","title":"T","blocks":[
        {"type":"chart","dataKey":"s","xField":"x","yField":"y"},
        {"type":"chart","dataKey":"s","chartType":"pie","xField":"x","yField":"y"},
        {"type":"table","dataKey":"s","columns":[{"field":"x","title":"X"}]}]})json");

    const auto result = rep::migrateTemplate(doc);
    REQUIRE(result.ok);
    REQUIRE(doc["version"].asInt() == 2);
    REQUIRE(doc["blocks"][0]["chartType"].asString() == "bar"); // дозаполнили
    REQUIRE(doc["blocks"][1]["chartType"].asString() == "pie"); // не тронули
    REQUIRE(result.steps.front().find("1→2") == 0);
}

TEST_CASE("миграция: шаблон новее сервера отклоняется") {
    auto doc = parse(R"json({"version":99,"id":"t","title":"T","blocks":[]})json");
    const auto result = rep::migrateTemplate(doc);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("99") != std::string::npos);
}

TEST_CASE("миграция: нецелая версия — ошибка, документ не меняется") {
    auto doc = parse(R"json({"version":"2","id":"t","title":"T","blocks":[]})json");
    const auto result = rep::migrateTemplate(doc);
    REQUIRE_FALSE(result.ok);
    REQUIRE(doc["version"].asString() == "2");
}

TEST_CASE("миграция: актуальная версия остаётся без изменений") {
    auto doc = parse(R"json({"version":2,"id":"t","title":"T","blocks":[]})json");
    const auto result = rep::migrateTemplate(doc);
    REQUIRE(result.ok);
    REQUIRE(result.steps.empty());
    REQUIRE(result.fromVersion == 2);
}

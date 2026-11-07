#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include <json/json.h>

#include "validate.h"

namespace {

Json::Value parse(const std::string &raw) {
    Json::CharReaderBuilder rb;
    Json::Value root;
    std::string err;
    std::istringstream in(raw);
    Json::parseFromStream(rb, in, &root, &err);
    return root;
}

std::set<std::string> errorPaths(const rep::ValidationResult &result) {
    std::set<std::string> paths;
    for (const auto &e : result.errors) paths.insert(e.path);
    return paths;
}

Json::Value minimal() {
    return parse(R"json({"version":1,"id":"t","title":"T","params":[],
        "datasets":{"sales":{"sql":"SELECT 1 AS x"}},
        "blocks":[{"type":"table","dataKey":"sales","columns":[{"field":"x","title":"X"}]}]})json");
}

} // namespace

TEST_CASE("валидатор: фикстуры совпадают с клиентскими ожиданиями") {
    std::ifstream file(std::string(REP_SOURCE_DIR) + "/docs/fixtures/validation-cases.json");
    REQUIRE(file.good());
    std::stringstream buffer;
    buffer << file.rdbuf();
    const Json::Value fixtures = parse(buffer.str());
    REQUIRE(fixtures["cases"].isArray());

    for (const auto &item : fixtures["cases"]) {
        const std::string name = item["name"].asString();
        const auto result = rep::validateTemplateJson(item["template"], "");
        std::set<std::string> expected;
        for (const auto &path : item["errors"]) expected.insert(path.asString());
        INFO("кейс: " << name);
        const auto actual = errorPaths(result);
        REQUIRE(actual == expected);
    }
}

TEST_CASE("валидатор: id проверяется по шаблону и совпадает с URL") {
    auto tpl = minimal();
    tpl["id"] = "Bad Id";
    REQUIRE(errorPaths(rep::validateTemplateJson(tpl, "")).count("id") == 1);

    tpl["id"] = "good_id";
    REQUIRE(errorPaths(rep::validateTemplateJson(tpl, "other")).count("id") == 1);
    REQUIRE(rep::validateTemplateJson(tpl, "good_id").ok());

    tpl["id"] = "";
    REQUIRE(errorPaths(rep::validateTemplateJson(tpl, "")).count("id") == 1);
}

TEST_CASE("валидатор: SQL наборов данных только на чтение") {
    auto tpl = minimal();
    tpl["datasets"]["sales"]["sql"] = "DELETE FROM sales";
    REQUIRE(errorPaths(rep::validateTemplateJson(tpl, "")).count("datasets.sales") == 1);

    tpl["datasets"]["sales"]["sql"] = "SELECT 1; DROP TABLE sales";
    REQUIRE(!rep::validateTemplateJson(tpl, "").ok());

    tpl["datasets"]["sales"]["sql"] = "WITH t AS (SELECT 1 AS x) SELECT * FROM t";
    REQUIRE(rep::validateTemplateJson(tpl, "").ok());
}

TEST_CASE("валидатор: SQL справочника проверяется тем же гардом") {
    auto tpl = minimal();
    tpl["params"] = parse(R"json([{"name":"region","type":"select","label":"R",
        "source":{"kind":"lookup","sql":"truncate table regions"}}])json");
    REQUIRE(errorPaths(rep::validateTemplateJson(tpl, "")).count("params.0.source") == 1);
}

TEST_CASE("валидатор: имена параметров уникальны и по шаблону") {
    auto tpl = minimal();
    tpl["params"] = parse(R"json([{"name":"from","type":"date","label":"a"},
                              {"name":"from","type":"date","label":"b"},
                              {"name":"From","type":"date","label":"c"}])json");
    const auto paths = errorPaths(rep::validateTemplateJson(tpl, ""));
    REQUIRE(paths.count("params.1.name") == 1);
    REQUIRE(paths.count("params.2.name") == 1);
}

TEST_CASE("валидатор: каскад требует объявленного выше не множественного родителя") {
    auto tpl = minimal();
    tpl["params"] = parse(R"json([{"name":"city","type":"select","label":"C",
                               "source":{"kind":"lookup","sql":"SELECT 1","dependsOn":"region"}},
                              {"name":"region","type":"select","label":"R","multiple":true,
                               "source":{"kind":"lookup","sql":"SELECT 1"}}])json");
    REQUIRE(errorPaths(rep::validateTemplateJson(tpl, "")).count("params.0.source.dependsOn") == 1);
}

TEST_CASE("валидатор: групповая колонка идёт до обычных, подытог только у группы") {
    auto tpl = minimal();
    tpl["blocks"][0]["columns"] = parse(R"json([
        {"field":"x","title":"X"},
        {"field":"y","title":"Y","group":true,"subtotal":true},
        {"field":"z","title":"Z","subtotal":true}])json");
    const auto paths = errorPaths(rep::validateTemplateJson(tpl, ""));
    REQUIRE(paths.count("blocks.0.columns.1") == 1);
    REQUIRE(paths.count("blocks.0.columns.2") == 1);
}

TEST_CASE("валидатор: агрегат требует поля, expression и field несовместимы") {
    auto tpl = minimal();
    tpl["blocks"][0]["columns"] = parse(R"json([
        {"title":"A","aggregate":"sum"},
        {"field":"x","expression":"sum(x)","title":"B"}])json");
    const auto paths = errorPaths(rep::validateTemplateJson(tpl, ""));
    REQUIRE(paths.count("blocks.0.columns.0") == 1);
    REQUIRE(paths.count("blocks.0.columns.1") == 1);
}

TEST_CASE("валидатор: колонка-заголовок не группируется и не содержит полей") {
    auto tpl = minimal();
    tpl["blocks"][0]["columns"] = parse(R"json([
        {"title":"G","field":"x","children":[{"field":"x","title":"X"}]}])json");
    REQUIRE(!rep::validateTemplateJson(tpl, "").ok());
}

TEST_CASE("валидатор: набор данных блока должен существовать") {
    auto tpl = minimal();
    tpl["blocks"][0]["dataKey"] = "missing_set";
    REQUIRE(errorPaths(rep::validateTemplateJson(tpl, "")).count("blocks.0.dataKey") == 1);
}

TEST_CASE("валидатор: диаграмма требует известный chartType и поля") {
    auto tpl = minimal();
    tpl["blocks"] = parse(R"json([{"type":"chart","dataKey":"sales","chartType":"radar",
        "xField":"","yField":"x"}])json");
    const auto paths = errorPaths(rep::validateTemplateJson(tpl, ""));
    REQUIRE(paths.count("blocks.0.chartType") == 1);
    REQUIRE(paths.count("blocks.0.xField") == 1);
}

TEST_CASE("валидатор: live и folder проверяются по типу") {
    auto tpl = minimal();
    tpl["live"] = "yes";
    tpl["folder"] = 42;
    tpl["tags"] = Json::Value("not array");
    const auto paths = errorPaths(rep::validateTemplateJson(tpl, ""));
    REQUIRE(paths.count("live") == 1);
    REQUIRE(paths.count("folder") == 1);
    REQUIRE(paths.count("tags") == 1);
}

TEST_CASE("валидатор: стиль для несуществующей колонки — ошибка") {
    auto tpl = minimal();
    tpl["blocks"][0]["styles"] = parse(R"json({"columns":{"nope":{"colors":{"text":"#ffffff"}},
        "x":{"minWidth":200,"maxWidth":100}}})json");
    const auto paths = errorPaths(rep::validateTemplateJson(tpl, ""));
    REQUIRE(paths.count("blocks.0.styles.columns.nope") == 1);
    REQUIRE(paths.count("blocks.0.styles.columns.x") == 1);
}

TEST_CASE("валидатор: правила ссылаются на колонки таблицы") {
    auto tpl = minimal();
    tpl["blocks"][0]["rules"] = parse(R"json([{"when":{"column":"nope","op":"gt","value":1},
        "target":"x","apply":{"colors":{"text":"#ff0000"}}},
        {"when":{"column":"x","op":"between","value":[1]},"target":"x"}])json");
    const auto paths = errorPaths(rep::validateTemplateJson(tpl, ""));
    REQUIRE(paths.count("blocks.0.rules.0.when.column") == 1);
    REQUIRE(paths.count("blocks.0.rules.1.when.value") == 1);
}

TEST_CASE("валидатор: предупреждение о частом автообновлении") {
    auto tpl = minimal();
    tpl["refreshSeconds"] = 2;
    const auto result = rep::validateTemplateJson(tpl, "");
    REQUIRE(result.ok());
    REQUIRE(!result.warnings.empty());
    REQUIRE(result.warnings.front().path == "refreshSeconds");
}

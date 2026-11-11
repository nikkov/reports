#include <catch2/catch_test_macros.hpp>
#include "dataset.h"

TEST_CASE("compileDataset: имена, типы, пустая строка") {
    rep::ParamBag bag;
    bag.set("from", "2026-03-01");
    bag.set("empty", "");
    rep::DatasetQuery q; std::string err;
    REQUIRE(rep::compileDataset("SELECT :from::date, :empty", {}, bag, q, err));
    REQUIRE(q.sql.find("$1::date") != std::string::npos);
    REQUIRE(q.values.size() == 2);
    REQUIRE(q.values[1].empty());
    REQUIRE_FALSE(rep::compileDataset("SELECT :from::date", {}, [] { rep::ParamBag b; b.set("from", "01.03.2026"); return b; }(), q, err));
    REQUIRE(err.find("ГГГГ") != std::string::npos);
}

TEST_CASE("compileNamedParams: литералы и комментарии не разбираются") {
    rep::ParamBag bag;
    bag.set("from", "2026-03-01T09:30");
    std::string compiled;
    std::vector<std::string> values;
    std::string err;

    // ':MI' в строковом литерале — не параметр (иначе 422 «не передан параметр MI»)
    REQUIRE(rep::compileNamedParams(
        "SELECT to_char(ts, 'DD.MM.YYYY HH24:MI') FROM t WHERE ts >= :from::timestamp", bag, compiled,
        values, err));
    REQUIRE(compiled == "SELECT to_char(ts, 'DD.MM.YYYY HH24:MI') FROM t WHERE ts >= $1::timestamp");
    REQUIRE(values.size() == 1);
    REQUIRE(values[0] == "2026-03-01T09:30");

    // dollar-quoted, идентификатор в двойных кавычках и комментарии — без разбора
    REQUIRE(rep::compileNamedParams(
        R"(SELECT $$ :x $$, "a:b" FROM t -- :nope
WHERE ts < :from::timestamp /* :also_no */)",
        bag, compiled, values, err));
    REQUIRE(compiled.find("$1") != std::string::npos);
    REQUIRE(compiled.find(":x") != std::string::npos);
    REQUIRE(compiled.find(":nope") != std::string::npos);
    REQUIRE(values.size() == 1);
}

TEST_CASE("inferColumnType: значение целиком, а не подстрока") {
    REQUIRE(rep::inferColumnType("42") == "int");
    REQUIRE(rep::inferColumnType("-7") == "int");
    REQUIRE(rep::inferColumnType("3.5") == "float");
    REQUIRE(rep::inferColumnType("1e6") == "float");
    REQUIRE(rep::inferColumnType("2026-03-01") == "date");
    REQUIRE(rep::inferColumnType("01.03.2026 00:43") == "text"); // точка внутри — не float
    REQUIRE(rep::inferColumnType("search") == "text");
    REQUIRE(rep::inferColumnType("") == "text");
    REQUIRE(rep::inferColumnType("12 шт") == "text");
}

TEST_CASE("compileNamedParams: ::timestamp проверяет формат") {
    std::string compiled;
    std::vector<std::string> values;
    std::string err;

    rep::ParamBag good;
    good.set("from", "2026-03-01 09:30:00");
    REQUIRE(rep::compileNamedParams("SELECT :from::timestamp", good, compiled, values, err));

    rep::ParamBag bad;
    bad.set("from", "01.03.2026 09:30");
    REQUIRE_FALSE(rep::compileNamedParams("SELECT :from::timestamp", bad, compiled, values, err));
    REQUIRE(err.find("датой и временем") != std::string::npos);

    rep::ParamBag empty;
    empty.set("from", "");
    REQUIRE(rep::compileNamedParams("SELECT :from::timestamp", empty, compiled, values, err));
}

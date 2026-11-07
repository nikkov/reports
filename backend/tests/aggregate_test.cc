#include <catch2/catch_test_macros.hpp>

#include "aggregate.h"

TEST_CASE("buildAggregateSql строит GROUP BY по позициям") {
    std::string sql;
    std::string err;
    REQUIRE(rep::buildAggregateSql("SELECT region, category, amount FROM sales", {"region", "category"},
                                   {{"amount", "sum"}}, sql, err));
    REQUIRE(sql.find("sum(amount) AS amount") != std::string::npos);
    REQUIRE(sql.find("FROM (SELECT region, category, amount FROM sales) AS base") != std::string::npos);
    REQUIRE(sql.find("GROUP BY 1, 2") != std::string::npos);
    REQUIRE(sql.find("ORDER BY 1, 2") != std::string::npos);
}

TEST_CASE("buildAggregateSql проверяет имена и агрегаты") {
    std::string sql;
    std::string err;
    REQUIRE_FALSE(rep::buildAggregateSql("SELECT 1", {"region; DROP TABLE sales"}, {{"amount", "sum"}}, sql, err));
    REQUIRE_FALSE(rep::buildAggregateSql("SELECT 1", {"region"}, {{"amount", "median"}}, sql, err));
    REQUIRE_FALSE(rep::buildAggregateSql("SELECT 1", {}, {{"amount", "sum"}}, sql, err));
    REQUIRE_FALSE(rep::buildAggregateSql("SELECT 1", {"region"}, {}, sql, err));
    REQUIRE(rep::validIdentifier("region_1"));
    REQUIRE_FALSE(rep::validIdentifier("region-1"));
    REQUIRE_FALSE(rep::validIdentifier("1region"));
}

TEST_CASE("buildAggregateSql допускает все поддерживаемые агрегаты") {
    std::string sql;
    std::string err;
    for (const char *agg : {"sum", "avg", "min", "max", "count", "SUM"}) {
        REQUIRE(rep::buildAggregateSql("SELECT 1", {"g"}, {{"m", agg}}, sql, err));
    }
}

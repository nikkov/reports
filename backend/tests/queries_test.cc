#include <catch2/catch_test_macros.hpp>

#include "queries.h"

using namespace rep;

namespace {

ParamDef def(const std::string &name, ParamType t = ParamType::Text, bool required = true,
             const std::string &dflt = "") {
    return ParamDef{name, t, required, dflt};
}

}  // namespace

TEST_CASE("named placeholder becomes positional and keeps the cast", "[queries]") {
    const auto q = compileQuery("sales", "SELECT * FROM sales WHERE d >= :from::date AND d <= :to::date",
                                {def("from", ParamType::Date), def("to", ParamType::Date)});
    REQUIRE(q.sql == "SELECT * FROM sales WHERE d >= $1::date AND d <= $2::date");
    REQUIRE(q.binding.size() == 2);
    REQUIRE(q.binding[0].name == "from");
    REQUIRE(q.binding[1].name == "to");
}

TEST_CASE("repeated placeholder reuses the same position", "[queries]") {
    const auto q = compileQuery("q", "SELECT :a AS a, :a AS a2, :b AS b", {def("a"), def("b")});
    REQUIRE(q.sql == "SELECT $1 AS a, $1 AS a2, $2 AS b");
    REQUIRE(q.binding.size() == 2);
}

TEST_CASE("binding order follows SQL, not declaration order", "[queries]") {
    const auto q = compileQuery("q", "SELECT :b, :a", {def("a"), def("b")});
    REQUIRE(q.sql == "SELECT $1, $2");
    REQUIRE(q.binding[0].name == "b");
    REQUIRE(q.binding[1].name == "a");
}

TEST_CASE("placeholders inside strings, comments and dollar quotes are untouched", "[queries]") {
    const std::string sql =
        "SELECT ':notaparam', '--:notaparam', $$:notaparam$$ -- :notaparam\n"
        "FROM t WHERE x = :x";
    const auto q = compileQuery("q", sql, {def("x")});
    REQUIRE(q.sql.find("':notaparam'") != std::string::npos);
    REQUIRE(q.sql.find("$$:notaparam$$") != std::string::npos);
    REQUIRE(q.sql.find("-- :notaparam") != std::string::npos);
    REQUIRE(q.sql.find("x = $1") != std::string::npos);
}

TEST_CASE("undeclared placeholder is rejected", "[queries]") {
    REQUIRE_THROWS_AS(compileQuery("q", "SELECT :missing", {}), std::runtime_error);
}

TEST_CASE("declared but unused parameter is rejected", "[queries]") {
    REQUIRE_THROWS_AS(compileQuery("q", "SELECT 1", {def("unused")}), std::runtime_error);
}

TEST_CASE("empty SQL and unterminated literal are rejected", "[queries]") {
    REQUIRE_THROWS_AS(compileQuery("q", "", {}), std::runtime_error);
    REQUIRE_THROWS_AS(compileQuery("q", "SELECT 'oops", {}), std::runtime_error);
}

TEST_CASE("injection payload stays a value, never reaches the SQL text", "[queries][security]") {
    const auto q = compileQuery("q", "SELECT * FROM sales WHERE region = :region", {def("region")});
    const std::string payload = "Север'; DROP TABLE sales; --";
    // Значение привязывается как параметр: текст SQL не меняется ни при каком значении.
    REQUIRE(q.sql == "SELECT * FROM sales WHERE region = $1");
    REQUIRE(q.binding.size() == 1);
}

#include <catch2/catch_test_macros.hpp>

#include "params.h"

using namespace rep;

namespace {

ParamDef make(const std::string &name, ParamType t, bool required = true,
              const std::string &dflt = "") {
    return ParamDef{name, t, required, dflt};
}

}  // namespace

TEST_CASE("valid dates pass, invalid are rejected", "[params]") {
    REQUIRE(isValidDate("2026-01-31"));
    REQUIRE(isValidDate("2024-02-29"));   // високосный
    REQUIRE_FALSE(isValidDate("2025-02-29"));
    REQUIRE_FALSE(isValidDate("2026-13-01"));
    REQUIRE_FALSE(isValidDate("2026-00-10"));
    REQUIRE_FALSE(isValidDate("2026-1-1"));
    REQUIRE_FALSE(isValidDate("01.01.2026"));
}

TEST_CASE("int/float/bool formats", "[params]") {
    ParamBag bag;
    bag.set("n", "-42");
    bag.set("x", "3.14");
    bag.set("b", "true");
    REQUIRE(validateParams({make("n", ParamType::Int), make("x", ParamType::Float),
                            make("b", ParamType::Bool)},
                           bag)
                .empty());

    ParamBag bad;
    bad.set("n", "4.2");
    REQUIRE_FALSE(validateParams({make("n", ParamType::Int)}, bad).empty());
}

TEST_CASE("required parameter missing is an error unless it has a default", "[params]") {
    ParamBag empty;
    REQUIRE_FALSE(validateParams({make("from", ParamType::Date)}, empty).empty());
    REQUIRE(validateParams({make("from", ParamType::Date, true, "2026-01-01")}, empty).empty());
}

TEST_CASE("unknown parameter is rejected", "[params]") {
    ParamBag bag;
    bag.set("from", "2026-01-01");
    bag.set("evil", "1; DROP TABLE sales");
    REQUIRE_FALSE(validateParams({make("from", ParamType::Date)}, bag).empty());
}

TEST_CASE("defaults are applied only for missing values", "[params]") {
    ParamBag bag;
    bag.set("to", "2026-03-31");
    applyDefaults({make("from", ParamType::Date, true, "2026-01-01"),
                   make("to", ParamType::Date, true, "2026-12-31")},
                  bag);
    REQUIRE(*bag.find("from") == "2026-01-01");
    REQUIRE(*bag.find("to") == "2026-03-31");
}

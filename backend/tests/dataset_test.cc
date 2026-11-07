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

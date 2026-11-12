#include "dates_expr.h"

#include <catch2/catch_test_macros.hpp>

using rep::CheckDateExprSyntax;
using rep::LooksLikeDateExpr;

TEST_CASE("арифметика дат: что считается выражением (ADR-92)") {
    CHECK(LooksLikeDateExpr("*"));
    CHECK(LooksLikeDateExpr("*-5h"));
    CHECK(LooksLikeDateExpr("month(*)"));
    CHECK(LooksLikeDateExpr("  today(*) "));
    CHECK_FALSE(LooksLikeDateExpr(""));
    CHECK_FALSE(LooksLikeDateExpr("2026-03-01"));
    CHECK_FALSE(LooksLikeDateExpr("2026-03-01T10:30"));
}

TEST_CASE("арифметика дат: корректные выражения") {
    for (const char *expr : {"*", "*-5h", "*+5h", "*-1d", "*+2m", "*-1y", "today(*)", "week(*)",
                             "month(*)", "quarter(*)", "year(*)", "month(*)-1m", "*+1m-1d+2h",
                             "year(*)+1y"}) {
        std::string error;
        INFO(expr);
        CHECK(CheckDateExprSyntax(expr, error));
        CHECK(error.empty());
    }
}

TEST_CASE("арифметика дат: ошибки объясняют место") {
    std::string error;

    CHECK_FALSE(CheckDateExprSyntax("month2(*)", error));
    CHECK(error.find("неизвестное начало") != std::string::npos);
    CHECK(error.find("month2(*)") != std::string::npos);

    // единица не из набора h/d/m/y
    error.clear();
    CHECK_FALSE(CheckDateExprSyntax("*+5x", error));
    CHECK(error.find("+1d, -5h, +2m, -1y") != std::string::npos);

    // нет числа или нет единицы
    error.clear();
    CHECK_FALSE(CheckDateExprSyntax("*-", error));
    error.clear();
    CHECK_FALSE(CheckDateExprSyntax("month(*)+", error));
    error.clear();
    CHECK_FALSE(CheckDateExprSyntax("month(*)1m", error));

    // константа выражением не считается, синтаксис не проверяется
    CHECK_FALSE(LooksLikeDateExpr("2026-03-01"));
}

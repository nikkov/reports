// Префикс приложения (ADR-81): нормализация REP_BASE_PATH и снятие префикса с пути.
#include <catch2/catch_test_macros.hpp>

#include "base_path.h"

TEST_CASE("префикс: нормализация значения из окружения") {
    REQUIRE(rep::normalizeBasePath("/reports") == "/reports");
    REQUIRE(rep::normalizeBasePath("/reports/") == "/reports");
    REQUIRE(rep::normalizeBasePath("reports") == "/reports");
    REQUIRE(rep::normalizeBasePath("  /reports//  ") == "/reports");
    REQUIRE(rep::normalizeBasePath("/portal/x/") == "/portal/x");
    // пусто и корень — работа без префикса, старые стенды и скрипты не ломаются
    REQUIRE(rep::normalizeBasePath("") == "");
    REQUIRE(rep::normalizeBasePath("/") == "");
    REQUIRE(rep::normalizeBasePath("   ") == "");
}

TEST_CASE("префикс: снимается только целым сегментом") {
    std::string rest;
    REQUIRE(rep::stripBasePath("/reports/api/health", "/reports", rest));
    REQUIRE(rest == "/api/health");

    REQUIRE(rep::stripBasePath("/reports/report.html", "/reports", rest));
    REQUIRE(rest == "/report.html");

    REQUIRE(rep::stripBasePath("/reports/", "/reports", rest));
    REQUIRE(rest == "/");

    // сам префикс без слэша — нужен редирект на «/reports/»
    REQUIRE(rep::stripBasePath("/reports", "/reports", rest));
    REQUIRE(rest == "/");
}

TEST_CASE("префикс: чужие пути не трогаем") {
    std::string rest;
    REQUIRE_FALSE(rep::stripBasePath("/api/health", "/reports", rest));
    // «/reports-old» не должен превращаться в «-old»
    REQUIRE_FALSE(rep::stripBasePath("/reports-old/api", "/reports", rest));
    REQUIRE_FALSE(rep::stripBasePath("/", "/reports", rest));
    REQUIRE_FALSE(rep::stripBasePath("/report.html", "/reports", rest));
    REQUIRE(rest.empty());
}

TEST_CASE("префикс: без префикса пути не меняются") {
    std::string rest;
    REQUIRE_FALSE(rep::stripBasePath("/api/health", "", rest));
    REQUIRE_FALSE(rep::stripBasePath("/reports/api/health", "", rest));
}

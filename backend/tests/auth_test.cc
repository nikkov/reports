#include <catch2/catch_test_macros.hpp>

#include "auth.h"

TEST_CASE("пароль хэшируется и проверяется") {
    const std::string salt = rep::randomHex(16);
    const std::string hash = rep::hashPassword("s3cret", salt, 1000);

    REQUIRE(hash.size() == 64);
    REQUIRE(rep::verifyPassword("s3cret", salt, hash, 1000));
    REQUIRE_FALSE(rep::verifyPassword("s3cret!", salt, hash, 1000));
    REQUIRE_FALSE(rep::verifyPassword("s3cret", rep::randomHex(16), hash, 1000));
}

TEST_CASE("соль и токен уникальны") {
    REQUIRE(rep::randomHex(16) != rep::randomHex(16));
    REQUIRE(rep::randomHex(16).size() == 32);
}

TEST_CASE("матрица ролей: чтение всем, запись автору, публикация админу") {
    using rep::Role;
    using rep::requiredRole;

    REQUIRE(requiredRole("GET", "/api/health") == Role::Public);
    REQUIRE(requiredRole("POST", "/api/auth/login") == Role::Public);

    REQUIRE(requiredRole("GET", "/api/reports") == Role::Viewer);
    REQUIRE(requiredRole("GET", "/api/reports/sales-summary") == Role::Viewer);
    REQUIRE(requiredRole("POST", "/api/report/data") == Role::Viewer);
    REQUIRE(requiredRole("GET", "/api/reports/sales-summary/lookups/region") == Role::Viewer);

    REQUIRE(requiredRole("POST", "/api/reports") == Role::Author);
    REQUIRE(requiredRole("PUT", "/api/admin/reports/x/draft") == Role::Author);
    REQUIRE(requiredRole("DELETE", "/api/admin/reports/x/draft") == Role::Author);

    REQUIRE(requiredRole("POST", "/api/admin/reports/x/publish") == Role::Admin);
    REQUIRE(requiredRole("GET", "/api/admin/reports") == Role::Admin);
    REQUIRE(requiredRole("DELETE", "/api/reports/x") == Role::Admin);
    REQUIRE(requiredRole("PUT", "/api/reports/x") == Role::Admin);

    // безопасное умолчание: неизвестный путь требует админа
    REQUIRE(requiredRole("GET", "/api/unknown") == Role::Admin);
}

TEST_CASE("сравнение ролей") {
    REQUIRE(rep::roleAtLeast("admin", rep::Role::Author));
    REQUIRE(rep::roleAtLeast("author", rep::Role::Author));
    REQUIRE(rep::roleAtLeast("author", rep::Role::Viewer));
    REQUIRE_FALSE(rep::roleAtLeast("viewer", rep::Role::Author));
    REQUIRE_FALSE(rep::roleAtLeast("author", rep::Role::Admin));
    REQUIRE(rep::roleAtLeast("viewer", rep::Role::Public));
}

TEST_CASE("данные пользователя из админки проверяются") {
    std::string err;

    // корректное создание
    REQUIRE(rep::validateUserInput("analyst_1", "viewer", "s3cret-pass", true, err));

    // логин: шаблон и длина
    REQUIRE_FALSE(rep::validateUserInput("ab", "viewer", "s3cret-pass", true, err));
    REQUIRE_FALSE(rep::validateUserInput("Аналитик", "viewer", "s3cret-pass", true, err));
    REQUIRE_FALSE(rep::validateUserInput("with space", "viewer", "s3cret-pass", true, err));

    // роль — только из списка
    REQUIRE_FALSE(rep::validateUserInput("analyst_1", "owner", "s3cret-pass", true, err));
    REQUIRE(err.find("роль") != std::string::npos);

    // пароль: обязателен при создании, не короче 8 при смене
    REQUIRE_FALSE(rep::validateUserInput("analyst_1", "viewer", "", true, err));
    REQUIRE_FALSE(rep::validateUserInput("analyst_1", "viewer", "short", false, err));
    REQUIRE(rep::validateUserInput("analyst_1", "viewer", "", false, err)); // «не менять»
}


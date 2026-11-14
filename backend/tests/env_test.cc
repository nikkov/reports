#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <string>

#include "env.h"

// ADR-115: значения из файла .env важнее переменных окружения процесса.
TEST_CASE("env: файл .env перекрывает переменную окружения") {
    const std::string path = "/tmp/rep-env-test.env";
    {
        std::ofstream out(path);
        out << "# комментарий\n";
        out << "REP_TEST_VALUE=из-файла\n";
    }
#ifdef _WIN32
    _putenv_s("REP_TEST_VALUE", "из-окружения");
#else
    setenv("REP_TEST_VALUE", "из-окружения", 1);
#endif
    const size_t loaded = rep::loadDotEnv(path);
    REQUIRE(loaded >= 1);
    REQUIRE(rep::envOr("REP_TEST_VALUE", "") == "из-файла");
    std::remove(path.c_str());
}

TEST_CASE("env: без файла окружение не меняется") {
#ifdef _WIN32
    _putenv_s("REP_TEST_KEEP", "значение");
#else
    setenv("REP_TEST_KEEP", "значение", 1);
#endif
    REQUIRE(rep::loadDotEnv("/tmp/rep-env-нет-такого-файла.env") == 0);
    REQUIRE(rep::envOr("REP_TEST_KEEP", "") == "значение");
}

TEST_CASE("env: кавычки вокруг значения снимаются") {
    const std::string path = "/tmp/rep-env-quotes.env";
    {
        std::ofstream out(path);
        out << "REP_TEST_QUOTED=\"в кавычках\"\n";
    }
    rep::loadDotEnv(path);
    REQUIRE(rep::envOr("REP_TEST_QUOTED", "") == "в кавычках");
    std::remove(path.c_str());
}

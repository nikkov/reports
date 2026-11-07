#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <fstream>
#include <string>

#include "xlsx_export.h"

TEST_CASE("имя листа чистится по правилам Excel") {
    REQUIRE(rep::sheetName("Продажи: форматы/числа").find(':') == std::string::npos);
    REQUIRE(rep::sheetName("Продажи: форматы/числа").find('/') == std::string::npos);
    REQUIRE(rep::sheetName(std::string(60, 'x')).size() == 31);
    REQUIRE(rep::sheetName("") == "Отчёт");
}

TEST_CASE("книга пишется как zip-контейнер") {
    const std::string path = "/tmp/rep-xlsx-test.xlsx";
    rep::ExportSheet sheet;
    sheet.title = "Продажи: тест";
    sheet.columns = {{"Регион", "", false}, {"Сумма", "# ##0", true}};
    sheet.rows = {{"Восток", "1500"}, {"Запад", "2500"}};
    sheet.params = {{"from", "2026-03-01"}};

    std::string err;
    REQUIRE(rep::writeXlsx(path, sheet, err));

    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    char magic[2] = {0, 0};
    in.read(magic, 2);
    in.close();
    // xlsx — это zip: первые два байта PK
    REQUIRE(magic[0] == 'P');
    REQUIRE(magic[1] == 'K');
    std::remove(path.c_str());
}

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "csv_export.h"

TEST_CASE("toCsv: BOM, разделитель и кавычки") {
    rep::ExportSheet sheet;
    sheet.columns = {{"Регион", "", false}, {"Сумма", "# ##0", true}};
    sheet.rows = {{"Восток", "1500"}, {"Запад; юг", "2\"5"}};

    const std::string csv = rep::toCsv(sheet);
    REQUIRE(csv.rfind("\xEF\xBB\xBF", 0) == 0);                 // BOM
    REQUIRE(csv.find("Регион;Сумма\r\n") != std::string::npos);
    REQUIRE(csv.find("Восток;1500\r\n") != std::string::npos);
    REQUIRE(csv.find("\"Запад; юг\"") != std::string::npos);     // кавычки из-за ';'
    REQUIRE(csv.find("\"2\"\"5\"") != std::string::npos);       // удвоение кавычки
}

TEST_CASE("toCsv: пустые значения остаются пустыми полями") {
    rep::ExportSheet sheet;
    sheet.columns = {{"a", "", false}, {"b", "", false}};
    sheet.rows = {{"", "x"}};
    const std::string csv = rep::toCsv(sheet);
    REQUIRE(csv.find(";x\r\n") != std::string::npos);
}

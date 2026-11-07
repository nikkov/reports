#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "xlsx_export.h"

namespace {

rep::ExportSheet sheetWithRows(const std::vector<std::vector<std::string>> &rows) {
    rep::ExportSheet sheet;
    sheet.title = "T";
    sheet.columns = {{"region", "", false}, {"category", "", false}, {"amount", "# ##0", true}};
    sheet.rows = rows;
    return sheet;
}

} // namespace

TEST_CASE("applyGrouping: объединения и уровни по двум группам без пересортировки") {
    auto sheet = sheetWithRows({
        {"Восток", "Одежда", "10"},
        {"Восток", "Одежда", "12"},
        {"Восток", "Обувь", "15"},
        {"Юг", "Обувь", "30"},
        {"Юг", "Одежда", "40"},
    });

    rep::applyGrouping(sheet, {0, 1});

    // порядок из SQL сохранён: пересортировки нет
    REQUIRE(sheet.rows[0][1] == "Одежда");
    REQUIRE(sheet.rows[2][1] == "Обувь");
    REQUIRE(sheet.rows[3][0] == "Юг");

    // region: два объединения (Восток 0–2, Юг 3–4); category: одно (Одежда 0–1)
    REQUIRE(sheet.merges.size() == 3);
    bool regionMerged = false;
    for (const auto &m : sheet.merges) {
        if (m.col == 0 && m.firstRow == 0 && m.lastRow == 2) regionMerged = true;
    }
    REQUIRE(regionMerged);

    // уровни: опора региона 0, опора категории 1, детали 2
    REQUIRE(sheet.rowLevels.size() == 5);
    REQUIRE(sheet.rowLevels[0] == 0);
    REQUIRE(sheet.rowLevels[1] == 2); // вторая строка той же категории — деталь
    REQUIRE(sheet.rowLevels[2] == 1); // новая категория внутри региона
    REQUIRE(sheet.rowLevels[3] == 0); // новый регион
    REQUIRE(sheet.rowLevels[4] == 1); // новая категория внутри региона
}

TEST_CASE("applyGrouping: без групповых колонок лист не меняется") {
    auto sheet = sheetWithRows({{"b", "x", "1"}, {"a", "y", "2"}});
    rep::applyGrouping(sheet, {});
    REQUIRE(sheet.rows[0][0] == "b");
    REQUIRE(sheet.merges.empty());
    REQUIRE(sheet.rowLevels.empty());
}

TEST_CASE("applyGrouping: одна строка — ни объединений, ни уровней") {
    auto sheet = sheetWithRows({{"a", "b", "1"}});
    rep::applyGrouping(sheet, {0, 1});
    REQUIRE(sheet.merges.empty());
}

TEST_CASE("applyGrouping: одинаковые значения по всей колонке — одно объединение") {
    auto sheet = sheetWithRows({{"Восток", "a", "1"}, {"Восток", "b", "2"}, {"Восток", "c", "3"}});
    rep::applyGrouping(sheet, {0});
    REQUIRE(sheet.merges.size() == 1);
    REQUIRE(sheet.merges[0].firstRow == 0);
    REQUIRE(sheet.merges[0].lastRow == 2);
    REQUIRE(sheet.rowLevels[1] == 1);
    REQUIRE(sheet.rowLevels[2] == 1);
}

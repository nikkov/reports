#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "group_rows.h"
#include "xlsx_export.h"

namespace {

// Колонки: регион (группа), категория (группа), сумма (мера), штук (мера)
rep::ExportSheet sheetWithRows(const std::vector<std::vector<std::string>> &rows) {
    rep::ExportSheet sheet;
    sheet.title = "T";
    sheet.columns = {{"Регион", "", false},
                     {"Категория", "", false},
                     {"Сумма", "# ##0", true},
                     {"Штук", "# ##0", true}};
    sheet.rows = rows;
    return sheet;
}

rep::GroupLayout twoLevels(bool grand = true) {
    rep::GroupLayout layout;
    layout.groupColumns = {0, 1};
    layout.subtotalLevels = {0, 1};
    layout.measureColumns = {2, 3};
    layout.grandTotal = grand;
    return layout;
}

std::vector<std::string> kinds(const rep::ExportSheet &sheet) {
    std::vector<std::string> out;
    for (unsigned char k : sheet.rowKind) {
        out.push_back(k == 1 ? "subtotal" : k == 2 ? "grand" : "detail");
    }
    return out;
}

} // namespace

TEST_CASE("buildGroupedSheet: подытог на каждом уровне и общий итог") {
    auto sheet = sheetWithRows({
        {"Восток", "Одежда", "10", "1"},
        {"Восток", "Одежда", "12", "2"},
        {"Восток", "Обувь", "15", "3"},
        {"Юг", "Обувь", "30", "4"},
    });
    rep::buildGroupedSheet(sheet, twoLevels());

    const auto k = kinds(sheet);
    // подытог категории и подытог региона идут подряд — как на экране
    REQUIRE(k == std::vector<std::string>{"detail", "detail", "subtotal", "detail", "subtotal", "subtotal",
                                          "detail", "subtotal", "subtotal", "grand"});

    // порядок строк сохранён, порядок вложенности — как на экране
    REQUIRE(sheet.rows[0][1] == "Одежда");
    REQUIRE(sheet.rows[2][0] == "Восток");   // предок держит своё значение
    REQUIRE(sheet.rows[2][1] == "Итого Одежда");
    REQUIRE(sheet.rows[2][2] == "22");
    REQUIRE(sheet.rows[2][3] == "3");
    REQUIRE(sheet.rows[5][0] == "Итого Восток"); // подпись региона — в его колонке
    REQUIRE(sheet.rows[5][2] == "37");
    REQUIRE(sheet.rows[6][0] == "Юг");
    REQUIRE(sheet.rows[9][0] == "ИТОГО");
    REQUIRE(sheet.rows[9][2] == "67");
    REQUIRE(sheet.rows[9][3] == "10");
}

TEST_CASE("buildGroupedSheet: строка итога своего уровня не входит в объединение своей колонки") {
    auto sheet = sheetWithRows({
        {"Восток", "Одежда", "10", "1"},
        {"Восток", "Одежда", "12", "2"},
        {"Восток", "Обувь", "15", "3"},
    });
    rep::buildGroupedSheet(sheet, twoLevels());

    // объединение региона покрывает детали и подытоги категорий, но не свой подытог и не ИТОГО
    bool regionSpan = false;
    for (const auto &m : sheet.merges) {
        if (m.col == 0 && m.firstRow == 0) {
            REQUIRE(m.lastRow == 4); // 3 детали + 2 подытога категорий
            regionSpan = true;
        }
        REQUIRE((m.col != 0 || m.lastRow < 5)); // ИТОГО в объединение не попадает
    }
    REQUIRE(regionSpan);

    // объединения категории «Одежда» — только её две детальные строки
    bool clothesSpan = false;
    for (const auto &m : sheet.merges) {
        if (m.col == 1 && m.firstRow == 0 && m.lastRow == 1) clothesSpan = true;
    }
    REQUIRE(clothesSpan);
}

TEST_CASE("buildGroupedSheet: пустой ключ идёт в группу (пусто), суммы считаются") {
    auto sheet = sheetWithRows({
        {"", "Одежда", "5", "1"},
        {"", "Одежда", "7", "1"},
    });
    rep::buildGroupedSheet(sheet, twoLevels());
    REQUIRE(sheet.rows[2][0] == "(пусто)");
    REQUIRE(sheet.rows[2][1] == "Итого Одежда");
    REQUIRE(sheet.rows[2][2] == "12");
    REQUIRE(sheet.rows[3][0] == "Итого (пусто)");
    REQUIRE(sheet.rows[4][0] == "ИТОГО");
    REQUIRE(sheet.rows[4][2] == "12");
}

TEST_CASE("buildGroupedSheet: без флага subtotal остаётся только ИТОГО") {
    auto sheet = sheetWithRows({{"Восток", "Одежда", "10", "1"}, {"Юг", "Обувь", "30", "4"}});
    rep::GroupLayout layout = twoLevels();
    layout.subtotalLevels = {};
    rep::buildGroupedSheet(sheet, layout);

    REQUIRE(kinds(sheet) == std::vector<std::string>{"detail", "detail", "grand"});
    REQUIRE(sheet.rows[2][0] == "ИТОГО");
    REQUIRE(sheet.rows[2][2] == "40");
}

TEST_CASE("buildGroupedSheet: без общего итога строка ИТОГО не добавляется") {
    auto sheet = sheetWithRows({{"Восток", "Одежда", "10", "1"}});
    rep::GroupLayout layout = twoLevels(false);
    rep::buildGroupedSheet(sheet, layout);
    for (unsigned char k : sheet.rowKind) REQUIRE(k != 2);
}

TEST_CASE("buildGroupedSheet: пустой лист не падает") {
    auto sheet = sheetWithRows({});
    rep::buildGroupedSheet(sheet, twoLevels());
    REQUIRE(sheet.rows.empty());
    REQUIRE(sheet.merges.empty());
}

TEST_CASE("dropSummaryRows: CSV остаётся детальным") {
    auto sheet = sheetWithRows({{"Восток", "Одежда", "10", "1"}, {"Восток", "Обувь", "15", "3"}});
    rep::buildGroupedSheet(sheet, twoLevels());
    REQUIRE(sheet.rows.size() == 6); // 2 детали + 2 подытога категорий + подытог региона + ИТОГО
    rep::dropSummaryRows(sheet);
    REQUIRE(sheet.rows.size() == 2);
    REQUIRE(sheet.rowKind.empty());
    REQUIRE(sheet.merges.empty());
}

TEST_CASE("buildGroupedSheet: collapsible: false оставляет строки без outline-уровней (ADR-94)") {
    auto sheet = sheetWithRows({
        {"Восток", "Одежда", "10", "1"},
        {"Восток", "Обувь", "15", "2"},
        {"Юг", "Одежда", "30", "3"},
    });
    auto layout = twoLevels();
    layout.collapsible = false;

    rep::buildGroupedSheet(sheet, layout);

    // подытоги и ИТОГО на месте
    const auto kindsAfter = kinds(sheet);
    REQUIRE(std::find(kindsAfter.begin(), kindsAfter.end(), "subtotal") != kindsAfter.end());
    REQUIRE(kindsAfter.back() == "grand");
    REQUIRE(sheet.merges.size() > 0);
    // но структуры в книге нет: все строки нулевого уровня
    REQUIRE(sheet.rowLevels.size() == sheet.rows.size());
    for (int level : sheet.rowLevels) REQUIRE(level == 0);
}

TEST_CASE("buildGroupedSheet: collapsible по умолчанию даёт outline-уровни") {
    auto sheet = sheetWithRows({
        {"Восток", "Одежда", "10", "1"},
        {"Восток", "Обувь", "15", "2"},
    });
    auto layout = twoLevels();
    rep::buildGroupedSheet(sheet, layout);
    bool hasLevel = false;
    for (int level : sheet.rowLevels) {
        if (level > 0) hasLevel = true;
    }
    REQUIRE(hasLevel);
}

TEST_CASE("buildGroupedSheet: плоская таблица без группировки — ИТОГО с подписью (ADR-95)") {
    auto sheet = sheetWithRows({
        {"Восток", "Одежда", "10", "1"},
        {"Юг", "Обувь", "30", "3"},
    });
    rep::GroupLayout layout;
    layout.measureColumns = {2, 3};
    layout.grandTotal = true;

    rep::buildGroupedSheet(sheet, layout);

    REQUIRE(sheet.rows.size() == 3);
    // детали не переставлены и не потеряны
    REQUIRE(sheet.rows[0][2] == "10");
    REQUIRE(sheet.rows[1][2] == "30");
    // итог: подпись в первой колонке, суммы по мерам
    REQUIRE(sheet.rowKind.back() == static_cast<unsigned char>(rep::RowKind::Grand));
    REQUIRE(sheet.rows.back()[0] == "ИТОГО");
    REQUIRE(sheet.rows.back()[2] == "40");
    REQUIRE(sheet.rows.back()[3] == "4");
    // структуры нет: групп не объявляли
    for (int level : sheet.rowLevels) REQUIRE(level == 0);
}

TEST_CASE("buildGroupedSheet: без групп структуры книги нет (дефект sales-drill)") {
    auto sheet = sheetWithRows({
        {"Восток", "Одежда", "10", "1"},
        {"Юг", "Обувь", "30", "4"},
    });
    rep::GroupLayout layout; // групповых колонок нет: плоская таблица
    layout.measureColumns = {2, 3};
    layout.grandTotal = true;
    rep::buildGroupedSheet(sheet, layout);

    // уровни структуры не выставляются вовсе — иначе Excel показывает сворачивание строк
    for (int lvl : sheet.rowLevels) REQUIRE(lvl == 0);
}

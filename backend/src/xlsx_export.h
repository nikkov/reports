// Серверный экспорт в .xlsx (ADR-38): нужен для рассылки и для отчётов, которые
// не помещаются в браузерный экспорт. Клиентский WYSIWYG-экспорт (ADR-28) остаётся.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace rep {

struct ExportColumn {
    std::string title;
    std::string numFmt; // формат Excel ("# ##0", "0.00%"); пусто — текст
    bool numeric = false;
};

// Объединение ячеек по вертикали: колонка и диапазон строк данных (0 — первая строка после шапки).
struct ExportMerge {
    int col = 0;
    int firstRow = 0;
    int lastRow = 0;
};

struct ExportSheet {
    std::string title;
    std::vector<ExportColumn> columns;
    std::vector<std::vector<std::string>> rows;                 // значения «как есть», строками
    std::vector<std::pair<std::string, std::string>> params;    // лист «Параметры»
    std::vector<int> rowLevels;                                 // outline-уровень строки (0 — без группы)
    std::vector<ExportMerge> merges;                            // объединения групповых колонок
};

// Раскладывает строки по группам: устойчивая сортировка по групповым колонкам, объединения
// одинаковых значений по вертикали и outline-уровни (заголовок группы — уровень d, детали — глубже).
void applyGrouping(ExportSheet &sheet, const std::vector<std::size_t> &groupColumns);

// Записать книгу в path. false + текст ошибки при сбое.
bool writeXlsx(const std::string &path, const ExportSheet &sheet, std::string &err);

// Excel запрещает в имени листа * ? : \ / [ ] и ограничивает 31 символом.
std::string sheetName(const std::string &title);

} // namespace rep

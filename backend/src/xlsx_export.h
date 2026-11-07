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

struct ExportSheet {
    std::string title;
    std::vector<ExportColumn> columns;
    std::vector<std::vector<std::string>> rows;                 // значения «как есть», строками
    std::vector<std::pair<std::string, std::string>> params;    // лист «Параметры»
};

// Записать книгу в path. false + текст ошибки при сбое.
bool writeXlsx(const std::string &path, const ExportSheet &sheet, std::string &err);

// Excel запрещает в имени листа * ? : \ / [ ] и ограничивает 31 символом.
std::string sheetName(const std::string &title);

} // namespace rep

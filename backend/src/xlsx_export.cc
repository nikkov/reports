#include "xlsx_export.h"

#include <xlsxwriter.h>

#include <algorithm>
#include <cstdlib>

namespace rep {
namespace {

bool looksNumeric(const std::string &s) {
    if (s.empty()) return false;
    char *end = nullptr;
    std::strtod(s.c_str(), &end);
    return end && *end == '\0';
}

} // namespace

std::string sheetName(const std::string &title) {
    std::string out;
    for (char c : title) {
        switch (c) {
        case '*':
        case '?':
        case ':':
        case '\\':
        case '/':
        case '[':
        case ']': out += '·'; break;
        default: out += c;
        }
    }
    if (out.empty()) out = "Отчёт";
    if (out.size() > 31) out.resize(31);
    return out;
}

void applyGrouping(ExportSheet &sheet, const std::vector<std::size_t> &groupColumns) {
    if (groupColumns.empty() || sheet.rows.size() < 2) return;

    // Порядок строк не меняем: он пришёл из SQL и совпадает с экраном. Объединяем только
    // подряд идущие одинаковые значения — так же, как это делает таблица в браузере;
    // пересортировка по байтам разошлась бы с экранным порядком на кириллице.
    const auto valueAt = [&](std::size_t row, std::size_t col) -> std::string {
        return col < sheet.rows[row].size() ? sheet.rows[row][col] : std::string();
    };

    // 2) сколько ведущих групповых колонок совпадает с предыдущей строкой
    std::vector<std::size_t> sameDepth(sheet.rows.size(), 0);
    for (std::size_t r = 1; r < sheet.rows.size(); ++r) {
        std::size_t depth = 0;
        while (depth < groupColumns.size() && valueAt(r, groupColumns[depth]) == valueAt(r - 1, groupColumns[depth])) {
            ++depth;
        }
        sameDepth[r] = depth;
    }

    // 3) объединения одинаковых значений по вертикали
    for (std::size_t g = 0; g < groupColumns.size(); ++g) {
        std::size_t start = 0;
        for (std::size_t r = 1; r <= sheet.rows.size(); ++r) {
            const bool same = r < sheet.rows.size() && sameDepth[r] > g;
            if (same) continue;
            if (r - 1 > start) {
                sheet.merges.push_back({static_cast<int>(groupColumns[g]), static_cast<int>(start),
                                        static_cast<int>(r - 1)});
            }
            start = r;
        }
    }

    // 4) уровни: новая группа глубины d — «опора» уровня d, продолжение самой глубокой — d+1
    sheet.rowLevels.assign(sheet.rows.size(), 0);
    for (std::size_t r = 1; r < sheet.rows.size(); ++r) {
        const std::size_t depth = sameDepth[r];
        const std::size_t maxLevel = groupColumns.size();
        sheet.rowLevels[r] = static_cast<int>(std::min(depth, maxLevel));
    }
}

bool writeXlsx(const std::string &path, const ExportSheet &sheet, std::string &err) {
    lxw_workbook *wb = workbook_new(path.c_str());
    if (!wb) {
        err = "не удалось создать книгу";
        return false;
    }
    lxw_worksheet *ws = workbook_add_worksheet(wb, sheetName(sheet.title).c_str());
    if (!ws) {
        workbook_close(wb);
        err = "не удалось добавить лист";
        return false;
    }

    lxw_format *header = workbook_add_format(wb);
    format_set_bold(header);
    format_set_bg_color(header, 0xF2F4F7);
    format_set_border(header, LXW_BORDER_THIN);

    for (size_t c = 0; c < sheet.columns.size(); ++c) {
        worksheet_write_string(ws, 0, static_cast<lxw_col_t>(c), sheet.columns[c].title.c_str(), header);
    }
    worksheet_freeze_panes(ws, 1, 0);
    // сводные строки (заголовки групп) стоят над деталями, как в браузерном экспорте
    worksheet_outline_settings(ws, 0 /*сводные строки над деталями*/, 0, 0, 0);
    for (const auto &merge : sheet.merges) {
        if (merge.lastRow <= merge.firstRow) continue;
        if (merge.col < 0 || merge.col >= static_cast<int>(sheet.columns.size())) continue;
        worksheet_merge_range(ws, static_cast<lxw_row_t>(merge.firstRow + 1), static_cast<lxw_col_t>(merge.col),
                              static_cast<lxw_row_t>(merge.lastRow + 1), static_cast<lxw_col_t>(merge.col), "",
                              nullptr);
    }

    if (!sheet.params.empty()) {
        worksheet_autofilter(ws, 0, 0, static_cast<lxw_row_t>(sheet.rows.size()),
                             static_cast<lxw_col_t>(sheet.columns.size() - 1));
    }

    // объединения рисуем после записи значений: текст нужен в первой ячейке диапазона
    std::vector<lxw_format *> formats(sheet.columns.size(), nullptr);
    // Строки итогов (ADR-66) красятся как зоны экрана: подытог — светло-серый, ИТОГО — темнее
    // и с линией сверху. Формат нужен свой на колонку: у чисел сохраняется числовой формат.
    std::vector<lxw_format *> subtotalFormats(sheet.columns.size(), nullptr);
    std::vector<lxw_format *> grandFormats(sheet.columns.size(), nullptr);
    for (size_t c = 0; c < sheet.columns.size(); ++c) {
        const bool numeric = !sheet.columns[c].numFmt.empty();
        if (numeric) {
            lxw_format *fmt = workbook_add_format(wb);
            format_set_num_format(fmt, sheet.columns[c].numFmt.c_str());
            formats[c] = fmt;

            lxw_format *sub = workbook_add_format(wb);
            format_set_num_format(sub, sheet.columns[c].numFmt.c_str());
            format_set_bold(sub);
            format_set_bg_color(sub, 0xF7F9FC);
            subtotalFormats[c] = sub;

            lxw_format *grand = workbook_add_format(wb);
            format_set_num_format(grand, sheet.columns[c].numFmt.c_str());
            format_set_bold(grand);
            format_set_bg_color(grand, 0xE9EDF2);
            format_set_top(grand, LXW_BORDER_MEDIUM);
            grandFormats[c] = grand;
            continue;
        }
        lxw_format *sub = workbook_add_format(wb);
        format_set_bold(sub);
        format_set_bg_color(sub, 0xF7F9FC);
        subtotalFormats[c] = sub;

        lxw_format *grand = workbook_add_format(wb);
        format_set_bold(grand);
        format_set_bg_color(grand, 0xE9EDF2);
        format_set_top(grand, LXW_BORDER_MEDIUM);
        grandFormats[c] = grand;
    }

    // какие ячейки объединены и должны заполняться только с первой строки диапазона
    std::vector<std::vector<int>> mergeStart(sheet.rows.size());
    for (const auto &merge : sheet.merges) {
        if (merge.lastRow <= merge.firstRow) continue;
        if (merge.lastRow < 0 || merge.firstRow >= static_cast<int>(sheet.rows.size())) continue;
        for (int row = merge.firstRow; row <= merge.lastRow && row < static_cast<int>(sheet.rows.size()); ++row) {
            if (row != merge.firstRow) mergeStart[static_cast<std::size_t>(row)].push_back(merge.col);
        }
    }

    for (size_t r = 0; r < sheet.rows.size(); ++r) {
        const auto &row = sheet.rows[r];
        if (r < sheet.rowLevels.size() && sheet.rowLevels[r] > 0) {
            lxw_row_col_options options = {.hidden = 0,
                                           .level = static_cast<uint8_t>(std::min(sheet.rowLevels[r], 7)),
                                           .collapsed = 0};
            worksheet_set_row_opt(ws, static_cast<lxw_row_t>(r + 1), LXW_DEF_ROW_HEIGHT, nullptr, &options);
        }
        const unsigned char kind = sheet.rowKind.empty() ? 0 : sheet.rowKind[r];
        for (size_t c = 0; c < sheet.columns.size() && c < row.size(); ++c) {
            const std::string &value = row[c];
            if (value.empty()) continue;
            const auto &skip = mergeStart[r];
            if (std::find(skip.begin(), skip.end(), static_cast<int>(c)) != skip.end()) continue;
            const auto rowIdx = static_cast<lxw_row_t>(r + 1);
            const auto colIdx = static_cast<lxw_col_t>(c);
            lxw_format *fmt = formats[c];
            if (kind == static_cast<unsigned char>(RowKind::Subtotal)) fmt = subtotalFormats[c];
            else if (kind == static_cast<unsigned char>(RowKind::Grand)) fmt = grandFormats[c];
            if (sheet.columns[c].numeric && looksNumeric(value)) {
                worksheet_write_number(ws, rowIdx, colIdx, std::strtod(value.c_str(), nullptr), fmt);
            } else {
                worksheet_write_string(ws, rowIdx, colIdx, value.c_str(), fmt);
            }
        }
    }

    lxw_worksheet *ps = workbook_add_worksheet(wb, "Параметры");
    if (ps) {
        worksheet_write_string(ps, 0, 0, "Параметр", header);
        worksheet_write_string(ps, 0, 1, "Значение", header);
        for (size_t i = 0; i < sheet.params.size(); ++i) {
            worksheet_write_string(ps, static_cast<lxw_row_t>(i + 1), 0, sheet.params[i].first.c_str(), nullptr);
            worksheet_write_string(ps, static_cast<lxw_row_t>(i + 1), 1, sheet.params[i].second.c_str(), nullptr);
        }
    }

    if (workbook_close(wb) != LXW_NO_ERROR) {
        err = "не удалось сохранить книгу";
        return false;
    }
    return true;
}

} // namespace rep

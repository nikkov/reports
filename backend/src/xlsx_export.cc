#include "xlsx_export.h"

#include <xlsxwriter.h>

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
    if (!sheet.columns.empty()) {
        worksheet_autofilter(ws, 0, 0, static_cast<lxw_row_t>(sheet.rows.size()),
                             static_cast<lxw_col_t>(sheet.columns.size() - 1));
    }

    std::vector<lxw_format *> formats(sheet.columns.size(), nullptr);
    for (size_t c = 0; c < sheet.columns.size(); ++c) {
        if (sheet.columns[c].numFmt.empty()) continue;
        lxw_format *fmt = workbook_add_format(wb);
        format_set_num_format(fmt, sheet.columns[c].numFmt.c_str());
        formats[c] = fmt;
    }

    for (size_t r = 0; r < sheet.rows.size(); ++r) {
        const auto &row = sheet.rows[r];
        for (size_t c = 0; c < sheet.columns.size() && c < row.size(); ++c) {
            const std::string &value = row[c];
            if (value.empty()) continue;
            const auto rowIdx = static_cast<lxw_row_t>(r + 1);
            const auto colIdx = static_cast<lxw_col_t>(c);
            if (sheet.columns[c].numeric && looksNumeric(value)) {
                worksheet_write_number(ws, rowIdx, colIdx, std::strtod(value.c_str(), nullptr), formats[c]);
            } else {
                worksheet_write_string(ws, rowIdx, colIdx, value.c_str(), nullptr);
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

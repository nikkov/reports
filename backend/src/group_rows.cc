#include "group_rows.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace rep {
namespace {

std::string cellValue(const std::vector<std::string> &row, std::size_t col) {
    return col < row.size() ? row[col] : std::string();
}

// Ключ уровня: значения ведущих групповых колонок; пусто — общая подпись (пусто).
std::string groupKey(const std::vector<std::string> &row, const GroupLayout &layout, int level,
                     const std::string &nullLabel) {
    std::string out;
    for (int i = 0; i <= level; ++i) {
        if (i > 0) out.push_back('\x1f');
        const std::string value = cellValue(row, layout.groupColumns[static_cast<std::size_t>(i)]);
        out += value.empty() ? nullLabel : value;
    }
    return out;
}

std::string labelPart(const std::vector<std::string> &row, const GroupLayout &layout, int index,
                      const std::string &nullLabel) {
    const std::string value = cellValue(row, layout.groupColumns[static_cast<std::size_t>(index)]);
    return value.empty() ? nullLabel : value;
}

// Сумма хранится в строке; целые печатаем без дробной части, иначе — с разумной точностью.
std::string formatSum(long double value) {
    const long double rounded = std::round(value);
    if (std::fabs(value - rounded) < 1e-9L && std::fabs(value) < 1e18L) {
        return std::to_string(static_cast<long long>(rounded));
    }
    std::ostringstream os;
    os << std::setprecision(10) << static_cast<double>(value);
    return os.str();
}

bool isDetail(const ExportSheet &sheet, std::size_t row) {
    return sheet.rowKind.empty() || sheet.rowKind[row] == static_cast<unsigned char>(RowKind::Detail);
}

unsigned char kindAt(const ExportSheet &sheet, std::size_t row) {
    return sheet.rowKind.empty() ? static_cast<unsigned char>(RowKind::Detail) : sheet.rowKind[row];
}

std::vector<std::string> summaryRow(const ExportSheet &sheet, const GroupLayout &layout, int level,
                                    const std::vector<std::string> &anchor, const std::string &label) {
    std::vector<std::string> out(sheet.columns.size());
    if (level < 0) {
        // общий итог: подпись в первой групповой колонке
        if (!layout.groupColumns.empty() && layout.groupColumns[0] < out.size()) out[layout.groupColumns[0]] = label;
    } else {
        // предки сохраняют свои значения — строка итога «висит» в своей группе, а не без неё
        for (int i = 0; i < level; ++i) {
            const std::size_t col = layout.groupColumns[static_cast<std::size_t>(i)];
            if (col < out.size()) out[col] = labelPart(anchor, layout, i, layout.nullLabel);
        }
        const std::size_t own = layout.groupColumns[static_cast<std::size_t>(level)];
        if (own < out.size()) out[own] = label;
    }
    return out;
}

void accumulateRow(const std::vector<std::string> &row, const GroupLayout &layout, std::vector<long double> &sums) {
    for (std::size_t i = 0; i < layout.measureColumns.size(); ++i) {
        const std::size_t col = layout.measureColumns[i];
        const std::string value = cellValue(row, col);
        if (value.empty()) continue;
        char *end = nullptr;
        const double parsed = std::strtod(value.c_str(), &end);
        if (end && *end == '\0') sums[i] += static_cast<long double>(parsed);
    }
}

void writeSums(std::vector<std::string> &row, const GroupLayout &layout, const std::vector<long double> &sums) {
    for (std::size_t i = 0; i < layout.measureColumns.size(); ++i) {
        const std::size_t col = layout.measureColumns[i];
        if (col < row.size()) row[col] = formatSum(sums[i]);
    }
}

// Объединения и уровни по правилу экрана: строка итога уровня L входит в объединения
// колонок-предков (col < L), но не в объединение своей колонки — иначе подпись «Итого …»
// прячется под общей ячейкой группы.
void rebuildMergesAndLevels(ExportSheet &sheet, const GroupLayout &layout) {
    sheet.merges.clear();
    sheet.rowLevels.assign(sheet.rows.size(), 0);
    if (layout.groupColumns.empty()) return;

    const int depth = static_cast<int>(layout.groupColumns.size());
    for (std::size_t r = 0; r < sheet.rows.size(); ++r) {
        const unsigned char kind = kindAt(sheet, r);
        if (kind == static_cast<unsigned char>(RowKind::Grand)) {
            sheet.rowLevels[r] = 0;
            continue;
        }
        if (kind == static_cast<unsigned char>(RowKind::Detail)) {
            sheet.rowLevels[r] = depth;
            continue;
        }
        // подытог: уровень самой группы; в объединениях участвует только как предок
        const int level = r < sheet.rowSubtotalLevel.size() ? sheet.rowSubtotalLevel[r] : -1;
        sheet.rowLevels[r] = level < 0 ? 0 : level;
    }

    for (std::size_t g = 0; g < layout.groupColumns.size(); ++g) {
        const std::size_t col = layout.groupColumns[g];
        std::size_t start = 0;
        bool haveStart = false;
        std::string currentKey;
        for (std::size_t r = 0; r <= sheet.rows.size(); ++r) {
            bool member = false;
            std::string key;
            if (r < sheet.rows.size()) {
                const unsigned char kind = kindAt(sheet, r);
                if (kind == static_cast<unsigned char>(RowKind::Detail) ||
                    (kind == static_cast<unsigned char>(RowKind::Subtotal) &&
                     sheet.rowLevels[r] > static_cast<int>(g))) {
                    member = true;
                    key = groupKey(sheet.rows[r], layout, static_cast<int>(g), layout.nullLabel);
                }
            }
            if (member && (!haveStart || key == currentKey)) {
                if (!haveStart) {
                    start = r;
                    currentKey = key;
                    haveStart = true;
                }
                continue;
            }
            if (haveStart && r > 0) {
                const std::size_t last = r - 1;
                if (last > start) {
                    sheet.merges.push_back({static_cast<int>(col), static_cast<int>(start), static_cast<int>(last)});
                }
            }
            haveStart = member;
            if (member) {
                start = r;
                currentKey = key;
            }
        }
    }
}

} // namespace

void buildGroupedSheet(ExportSheet &sheet, const GroupLayout &layout) {
    sheet.rowKind.assign(sheet.rows.size(), static_cast<unsigned char>(RowKind::Detail));
    sheet.rowSubtotalLevel.assign(sheet.rows.size(), -1);

    const int depth = static_cast<int>(layout.groupColumns.size());
    // Подытоги вставляем от самого глубокого уровня к верхнему: так вставки глубже
    // не сдвигают уже посчитанные границы верхних групп.
    for (int level = depth - 1; level >= 0; --level) {
        if (std::find(layout.subtotalLevels.begin(), layout.subtotalLevels.end(), level) ==
            layout.subtotalLevels.end()) {
            continue;
        }
        std::vector<std::vector<std::string>> rows;
        std::vector<unsigned char> kinds;
        std::vector<int> subtotalLevels;
        rows.reserve(sheet.rows.size() + sheet.rows.size() / 4);
        kinds.reserve(rows.capacity());
        subtotalLevels.reserve(rows.capacity());

        std::size_t i = 0;
        while (i < sheet.rows.size()) {
            if (!isDetail(sheet, i)) {
                rows.push_back(sheet.rows[i]);
                kinds.push_back(sheet.rowKind[i]);
                subtotalLevels.push_back(i < sheet.rowSubtotalLevel.size() ? sheet.rowSubtotalLevel[i] : -1);
                ++i;
                continue;
            }
            const std::string key = groupKey(sheet.rows[i], layout, level, layout.nullLabel);
            std::vector<long double> sums(layout.measureColumns.size(), 0);
            std::size_t j = i;
            while (j < sheet.rows.size()) {
                if (isDetail(sheet, j)) {
                    if (groupKey(sheet.rows[j], layout, level, layout.nullLabel) != key) break;
                    accumulateRow(sheet.rows[j], layout, sums);
                }
                rows.push_back(sheet.rows[j]);
                kinds.push_back(sheet.rowKind[j]);
                subtotalLevels.push_back(j < sheet.rowSubtotalLevel.size() ? sheet.rowSubtotalLevel[j] : -1);
                ++j;
            }
            auto summary = summaryRow(sheet, layout, level, sheet.rows[i],
                                      "Итого " + labelPart(sheet.rows[i], layout, level, layout.nullLabel));
            writeSums(summary, layout, sums);
            rows.push_back(summary);
            kinds.push_back(static_cast<unsigned char>(RowKind::Subtotal));
            subtotalLevels.push_back(level);
            i = j;
        }
        sheet.rows = std::move(rows);
        sheet.rowKind = std::move(kinds);
        sheet.rowSubtotalLevel = std::move(subtotalLevels);
    }

    if (layout.grandTotal && !sheet.rows.empty()) {
        std::vector<long double> sums(layout.measureColumns.size(), 0);
        for (std::size_t r = 0; r < sheet.rows.size(); ++r) {
            if (isDetail(sheet, r)) accumulateRow(sheet.rows[r], layout, sums);
        }
        auto total = summaryRow(sheet, layout, -1, {}, layout.grandLabel);
        writeSums(total, layout, sums);
        sheet.rows.push_back(total);
        sheet.rowKind.push_back(static_cast<unsigned char>(RowKind::Grand));
        sheet.rowSubtotalLevel.push_back(-1);
    }

    rebuildMergesAndLevels(sheet, layout);
}

void dropSummaryRows(ExportSheet &sheet) {
    if (sheet.rowKind.empty()) return;
    std::vector<std::vector<std::string>> rows;
    rows.reserve(sheet.rows.size());
    for (std::size_t r = 0; r < sheet.rows.size(); ++r) {
        if (sheet.rowKind[r] != static_cast<unsigned char>(RowKind::Detail)) continue;
        rows.push_back(sheet.rows[r]);
    }
    sheet.rows = std::move(rows);
    sheet.rowKind.clear();
    sheet.rowSubtotalLevel.clear();
    sheet.rowLevels.clear();
    sheet.merges.clear();
}

} // namespace rep

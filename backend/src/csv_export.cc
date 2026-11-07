#include "csv_export.h"

#include <cstdio>

namespace rep {
namespace {

std::string quote(const std::string &value) {
    const bool needQuotes = value.find(';') != std::string::npos || value.find('"') != std::string::npos ||
                            value.find('\n') != std::string::npos || value.find('\r') != std::string::npos;
    if (!needQuotes) return value;
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += '"';
    return out;
}

} // namespace

std::string toCsv(const ExportSheet &sheet) {
    std::string out = "\xEF\xBB\xBF"; // BOM: Excel распознаёт UTF-8

    for (size_t i = 0; i < sheet.columns.size(); ++i) {
        if (i) out += ';';
        out += quote(sheet.columns[i].title);
    }
    out += "\r\n";

    for (const auto &row : sheet.rows) {
        for (size_t i = 0; i < sheet.columns.size(); ++i) {
            if (i) out += ';';
            out += quote(i < row.size() ? row[i] : std::string());
        }
        out += "\r\n";
    }
    return out;
}

} // namespace rep

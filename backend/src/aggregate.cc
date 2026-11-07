#include "aggregate.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace rep {

bool validIdentifier(const std::string &name) {
    if (name.empty() || name.size() > 63) return false;
    if (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_') return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
}

bool buildAggregateSql(const std::string &datasetSql, const std::vector<std::string> &groupBy,
                       const std::vector<std::pair<std::string, std::string>> &measures,
                       std::string &out, std::string &err) {
    if (groupBy.empty()) {
        err = "нужен хотя бы один groupBy";
        return false;
    }
    if (measures.empty()) {
        err = "нужен хотя бы один measure";
        return false;
    }
    if (datasetSql.empty()) {
        err = "пустой SQL набора данных";
        return false;
    }

    static const std::set<std::string> allowed = {"sum", "avg", "min", "max", "count"};
    std::string select;
    for (const auto &g : groupBy) {
        if (!validIdentifier(g)) {
            err = "недопустимое имя колонки группировки: " + g;
            return false;
        }
        select += (select.empty() ? "" : ", ") + g;
    }
    for (const auto &m : measures) {
        if (!validIdentifier(m.first)) {
            err = "недопустимое имя measure: " + m.first;
            return false;
        }
        std::string agg = m.second;
        std::transform(agg.begin(), agg.end(), agg.begin(), [](unsigned char c) { return std::tolower(c); });
        if (!allowed.count(agg)) {
            err = "недопустимый агрегат: " + m.second;
            return false;
        }
        select += ", " + agg + "(" + m.first + ") AS " + m.first;
    }

    std::string group;
    for (size_t i = 0; i < groupBy.size(); ++i) group += (i ? ", " : "") + std::to_string(i + 1);

    out = "SELECT " + select + " FROM (" + datasetSql + ") AS base GROUP BY " + group + " ORDER BY " + group;
    return true;
}

} // namespace rep

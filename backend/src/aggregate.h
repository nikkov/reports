// SQL для серверной агрегации (ADR-37): GROUP BY поверх SQL набора данных.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace rep {

bool validIdentifier(const std::string &name);

// measures: пары «колонка, агрегат» (sum|avg|min|max|count). Имена проверяются,
// в текст SQL они попадают только после проверки.
bool buildAggregateSql(const std::string &datasetSql, const std::vector<std::string> &groupBy,
                       const std::vector<std::pair<std::string, std::string>> &measures,
                       std::string &out, std::string &err);

} // namespace rep

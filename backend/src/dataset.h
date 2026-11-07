// Сборка SQL набора данных: ':name' → $N с проверкой типа по касту в SQL (ADR-37).
// Вынесено из HTTP-слоя, чтобы данные, агрегация и экспорт использовали одну реализацию.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "params.h"

namespace rep {

struct DatasetQuery {
    std::string sql;
    std::vector<std::string> values;
};

// datasetParams: «имя параметра SQL» → «$параметр шаблона»; пусто — берём имена как есть.
bool compileDataset(const std::string &datasetSql, const std::map<std::string, std::string> &datasetParams,
                    const ParamBag &bag, DatasetQuery &out, std::string &err);

} // namespace rep

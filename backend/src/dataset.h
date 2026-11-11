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

// ':name' → $N с проверкой типа по касту (::date, ::int, ::timestamp).
// Литералы ('...', "..." и $$...$$) и комментарии не разбираются: ':' внутри них —
// часть текста, а не параметр (иначе to_char(..., 'HH24:MI') просил параметр MI).
bool compileNamedParams(const std::string &sql, const ParamBag &bag, std::string &compiled,
                        std::vector<std::string> &values, std::string &err);

// Тип колонки по значению, если шаблон его не объявил (ADR-24): число — int/float,
// дата — date, остальное — text. Сравнивается значение целиком: строка
// «01.03.2026 00:43» — текст, а не float из-за точки внутри.
std::string inferColumnType(const std::string &value);

} // namespace rep

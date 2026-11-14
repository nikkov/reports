// Объединение двух наборов по ключам (ADR-104). Вариант A: hash join в прокси,
// без материализации в БД. Один уровень: стороны — SQL- или REST-наборы.
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

#include "dataset_rows.h"

namespace rep {

struct JoinKey {
    std::string left;   // колонка левого набора
    std::string right;  // колонка правого набора
    std::string type = "text"; // int | text | date | datetime
};

struct JoinColumn {
    std::string field;          // колонка правого набора
    std::string as;             // имя в результате; пусто — field
    std::string title;
    std::string type = "text";
};

struct JoinSpec {
    std::string left;   // ключ набора datasets
    std::string right;
    std::vector<JoinKey> on;
    std::string type = "inner"; // inner | left
    std::vector<JoinColumn> columns;
};

// Разбор datasets.<key>.join. Семантику ссылок (набор существует, нет цикла) проверяет
// вызывающий: у парсера нет доступа к остальным наборам.
bool parseJoin(const Json::Value &join, JoinSpec &out, std::string &error);

// Собирает строки: колонки левого набора + объявленные колонки правого.
// Дубли справа размножают строки (как в SQL). Пустой ключ не совпадает ни с чем.
bool hashJoin(const DatasetRows &left, const DatasetRows &right, const JoinSpec &spec,
              DatasetRows &out, std::string &error);

} // namespace rep

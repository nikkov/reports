// Единый результат набора данных (ADR-104): SQL, REST и join отдают одно и то же,
// поэтому данные, агрегация, группы, текст и экспорт работают с любым источником.
#pragma once

#include <string>
#include <vector>

namespace rep {

struct RowColumn {
    std::string name;   // имя в результате (для join — с учётом `as`)
    std::string type;   // text | int | float | date | datetime | bool
    std::string title;  // подпись для UI и экспорта; пусто — берём name
};

struct DatasetRows {
    std::vector<RowColumn> columns;
    // Значения хранятся строками: PostgreSQL и JSON одинаково приводятся к тексту,
    // а форматирование (числа, даты) делает клиент и экспорт по типу колонки.
    std::vector<std::vector<std::string>> rows;

    size_t size() const { return rows.size(); }
};

} // namespace rep

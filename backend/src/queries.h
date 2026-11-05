#pragma once

#include <map>
#include <string>
#include <vector>

#include <json/json.h>

#include "params.h"

namespace rep {

// Скомпилированный запрос: SQL с позиционными плейсхолдерами $1..$N (libpq)
// + порядок привязки значений.
struct CompiledQuery {
    std::string id;
    std::string sql;                    // ':from::date' → '$1::date'
    std::vector<ParamDef> binding;      // в порядке появления в SQL (для $1..$N)
    std::map<std::string, std::string> columns;  // имя колонки → тип (int/float/text/date/bool)
};

// Переписывает именованные плейсхолдеры ':name' в позиционные '$N'.
// Правила: ':' перед идентификатором и не сразу после ':' (иначе ломается каст '::date');
// внутри строковых литералов '...', комментариев '--' и dollar-quoting $$...$$ замен нет.
// Бросает std::runtime_error, если плейсхолдер не объявлен или объявлен, но не использован.
CompiledQuery compileQuery(const std::string &id, const std::string &sql,
                           const std::vector<ParamDef> &declared);

// Реестр запросов из queries.json: { "<id>": { "sql": "...", "params": {...} } }.
class QueryRegistry {
  public:
    // Читает и валидирует файл. Бросает std::runtime_error с именем запроса при ошибке (fail-fast, ADR-4).
    void load(const std::string &path);

    const CompiledQuery *find(const std::string &id) const;
    const std::map<std::string, CompiledQuery> &all() const { return queries_; }
    size_t size() const { return queries_.size(); }

  private:
    std::map<std::string, CompiledQuery> queries_;
};

}  // namespace rep

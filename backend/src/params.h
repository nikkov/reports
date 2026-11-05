#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

namespace rep {

// Тип параметра запроса. Привязка к libpq всегда текстовая, приведение — в SQL через ::cast.
enum class ParamType { Text, Int, Float, Date, Bool };

std::optional<ParamType> parseParamType(const std::string &s);
const char *toString(ParamType t);

struct ParamDef {
    std::string name;
    ParamType type{ParamType::Text};
    bool required{false};
    std::string defaultValue;  // как текст; пусто = нет значения по умолчанию
};

// Читает объявление параметров из JSON-объекта { "name": {"type": "date", ...} }.
// Бросает std::runtime_error с указанием параметра при невалидном описании.
std::vector<ParamDef> parseParamDefs(const Json::Value &obj);

// Значения параметров запроса (все как текст).
class ParamBag {
  public:
    void set(const std::string &name, std::string value);
    bool has(const std::string &name) const;
    const std::string *find(const std::string &name) const;
    const std::map<std::string, std::string> &values() const { return values_; }

  private:
    std::map<std::string, std::string> values_;
};

// Строгая проверка даты YYYY-MM-DD с учётом календаря (високосные годы).
bool isValidDate(const std::string &s);

// Проверяет значения по объявлениям: обязательность, наличие лишних, формат типа
// (int — целое, float — число, date — YYYY-MM-DD и существующая дата, bool — true/false/1/0).
// Возвращает текст ошибки; пустая строка = ошибок нет.
std::string validateParams(const std::vector<ParamDef> &defs, const ParamBag &bag);

// Подставляет значения по умолчанию для отсутствующих параметров.
void applyDefaults(const std::vector<ParamDef> &defs, ParamBag &bag);

}  // namespace rep

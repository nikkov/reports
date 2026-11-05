#include "params.h"

#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace rep {

std::optional<ParamType> parseParamType(const std::string &s) {
    if (s == "text" || s == "string") return ParamType::Text;
    if (s == "int" || s == "integer") return ParamType::Int;
    if (s == "float" || s == "number") return ParamType::Float;
    if (s == "date") return ParamType::Date;
    if (s == "bool" || s == "boolean") return ParamType::Bool;
    return std::nullopt;
}

const char *toString(ParamType t) {
    switch (t) {
        case ParamType::Text: return "text";
        case ParamType::Int: return "int";
        case ParamType::Float: return "float";
        case ParamType::Date: return "date";
        case ParamType::Bool: return "bool";
    }
    return "text";
}

std::vector<ParamDef> parseParamDefs(const Json::Value &obj) {
    std::vector<ParamDef> out;
    if (!obj.isObject()) throw std::runtime_error("params: ожидался объект");

    for (auto it = obj.begin(); it != obj.end(); ++it) {
        ParamDef d;
        d.name = it.name();
        if (d.name.empty()) throw std::runtime_error("params: пустое имя параметра");

        const Json::Value &spec = *it;
        if (!spec.isObject()) throw std::runtime_error("params." + d.name + ": ожидался объект");

        const std::string typeName = spec.get("type", "text").asString();
        const auto t = parseParamType(typeName);
        if (!t) throw std::runtime_error("params." + d.name + ": неизвестный тип '" + typeName + "'");
        d.type = *t;
        d.required = spec.get("required", false).asBool();
        if (spec.isMember("default")) {
            if (!spec["default"].isString())
                throw std::runtime_error("params." + d.name + ": default должен быть строкой");
            d.defaultValue = spec["default"].asString();
        }
        out.push_back(std::move(d));
    }
    return out;
}

void ParamBag::set(const std::string &name, std::string value) { values_[name] = std::move(value); }

bool ParamBag::has(const std::string &name) const { return values_.count(name) > 0; }

const std::string *ParamBag::find(const std::string &name) const {
    const auto it = values_.find(name);
    return it == values_.end() ? nullptr : &it->second;
}

static bool isDigits(const std::string &s, size_t from, size_t to) {
    if (from >= to) return false;
    for (size_t i = from; i < to; ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

bool isValidDate(const std::string &s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    if (!isDigits(s, 0, 4) || !isDigits(s, 5, 7) || !isDigits(s, 8, 10)) return false;

    const int y = std::stoi(s.substr(0, 4));
    const int m = std::stoi(s.substr(5, 2));
    const int d = std::stoi(s.substr(8, 2));
    if (m < 1 || m > 12 || d < 1) return false;

    static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maxDay = daysInMonth[m - 1];
    const bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    if (m == 2 && leap) maxDay = 29;
    return d <= maxDay;
}

static bool isInt(const std::string &s) {
    size_t i = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
    return isDigits(s, i, s.size());
}

static bool isFloat(const std::string &s) {
    if (s.empty()) return false;
    char *end = nullptr;
    std::strtod(s.c_str(), &end);
    return end != nullptr && *end == '\0';
}

std::string validateParams(const std::vector<ParamDef> &defs, const ParamBag &bag) {
    for (const auto &d : defs) {
        const std::string *v = bag.find(d.name);
        if (v == nullptr) {
            if (d.required && d.defaultValue.empty())
                return "параметр '" + d.name + "' обязателен";
            continue;
        }
        switch (d.type) {
            case ParamType::Int:
                if (!isInt(*v)) return "параметр '" + d.name + "' должен быть целым числом";
                break;
            case ParamType::Float:
                if (!isFloat(*v)) return "параметр '" + d.name + "' должен быть числом";
                break;
            case ParamType::Date:
                if (!isValidDate(*v)) return "параметр '" + d.name + "' должен быть датой YYYY-MM-DD";
                break;
            case ParamType::Bool:
                if (*v != "true" && *v != "false" && *v != "1" && *v != "0")
                    return "параметр '" + d.name + "' должен быть true/false";
                break;
            case ParamType::Text:
                break;
        }
    }

    for (const auto &kv : bag.values()) {
        bool known = false;
        for (const auto &d : defs)
            if (d.name == kv.first) {
                known = true;
                break;
            }
        if (!known) return "неизвестный параметр '" + kv.first + "'";
    }
    return {};
}

void applyDefaults(const std::vector<ParamDef> &defs, ParamBag &bag) {
    for (const auto &d : defs) {
        if (!bag.has(d.name) && !d.defaultValue.empty()) bag.set(d.name, d.defaultValue);
    }
}

}  // namespace rep

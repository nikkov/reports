#include "queries.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rep {

namespace {

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identBody(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

}  // namespace

CompiledQuery compileQuery(const std::string &id, const std::string &sql,
                           const std::vector<ParamDef> &declared) {
    if (sql.empty()) throw std::runtime_error("запрос '" + id + "': пустой SQL");

    std::map<std::string, size_t> position;      // имя → номер $N
    std::vector<ParamDef> binding;
    std::string out;
    out.reserve(sql.size());

    bool inSingleQuote = false;
    bool inLineComment = false;
    bool inDollarQuote = false;
    std::string dollarTag;

    for (size_t i = 0; i < sql.size(); ++i) {
        const char c = sql[i];

        if (inLineComment) {
            out.push_back(c);
            if (c == '\n') inLineComment = false;
            continue;
        }
        if (inDollarQuote) {
            // Закрывающий тег начинается с текущего символа: '$$' или '$tag$'.
            if (c == '$' && sql.compare(i, dollarTag.size(), dollarTag) == 0) {
                out.append(dollarTag);
                i += dollarTag.size() - 1;
                inDollarQuote = false;
                continue;
            }
            out.push_back(c);
            continue;
        }
        if (inSingleQuote) {
            out.push_back(c);
            if (c == '\'') {
                if (i + 1 < sql.size() && sql[i + 1] == '\'') {  // '' — экранированная кавычка
                    out.push_back('\'');
                    ++i;
                } else {
                    inSingleQuote = false;
                }
            }
            continue;
        }

        if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
            inLineComment = true;
            out.push_back(c);
            continue;
        }
        if (c == '\'') {
            inSingleQuote = true;
            out.push_back(c);
            continue;
        }
        if (c == '$') {
            size_t j = i + 1;
            while (j < sql.size() && identBody(sql[j])) ++j;
            if (j < sql.size() && sql[j] == '$') {
                dollarTag = sql.substr(i, j - i + 1);  // '$$' или '$tag$'
                inDollarQuote = true;
                out.append(dollarTag);
                i = j;
                continue;
            }
            out.push_back(c);
            continue;
        }

        // Плейсхолдер: ':' + идентификатор, но не '::' (каст PostgreSQL).
        if (c == ':' && i + 1 < sql.size() && identStart(sql[i + 1]) &&
            (i == 0 || sql[i - 1] != ':')) {
            size_t j = i + 1;
            while (j < sql.size() && identBody(sql[j])) ++j;
            const std::string name = sql.substr(i + 1, j - i - 1);

            const auto decl = std::find_if(declared.begin(), declared.end(),
                                           [&](const ParamDef &d) { return d.name == name; });
            if (decl == declared.end())
                throw std::runtime_error("запрос '" + id + "': плейсхолдер ':" + name +
                                         "' не объявлен в params");

            auto pos = position.find(name);
            if (pos == position.end()) {
                position.emplace(name, binding.size() + 1);
                binding.push_back(*decl);
                pos = position.find(name);
            }
            out += '$' + std::to_string(pos->second);
            i = j - 1;
            continue;
        }

        out.push_back(c);
    }

    if (inSingleQuote || inDollarQuote)
        throw std::runtime_error("запрос '" + id + "': незакрытый строковый литерал");

    for (const auto &d : declared) {
        if (position.find(d.name) == position.end())
            throw std::runtime_error("запрос '" + id + "': параметр '" + d.name +
                                     "' объявлен, но не используется в SQL");
    }

    return CompiledQuery{id, out, binding};
}

void QueryRegistry::load(const std::string &path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("не удалось открыть реестр запросов: " + path);

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, in, &root, &errs))
        throw std::runtime_error("queries.json: " + errs);

    if (!root.isObject()) throw std::runtime_error("queries.json: ожидался объект верхнего уровня");

    std::map<std::string, CompiledQuery> result;
    for (auto it = root.begin(); it != root.end(); ++it) {
        const std::string id = it.name();
        const Json::Value &spec = *it;
        if (!spec.isObject()) throw std::runtime_error("queries.json: '" + id + "' — ожидался объект");
        if (!spec.isMember("sql") || !spec["sql"].isString())
            throw std::runtime_error("queries.json: '" + id + "' — нет строкового поля sql");

        std::vector<ParamDef> defs;
        if (spec.isMember("params")) defs = parseParamDefs(spec["params"]);

        std::map<std::string, std::string> columns;
        if (spec.isMember("columns")) {
            const Json::Value &cols = spec["columns"];
            if (!cols.isObject()) throw std::runtime_error("queries.json: '" + id + "' — columns должен быть объектом");
            for (auto c = cols.begin(); c != cols.end(); ++c) {
                if (!c->isString()) throw std::runtime_error("queries.json: '" + id + "' — тип колонки должен быть строкой");
                columns[c.name()] = c->asString();
            }
        }

        result[id] = compileQuery(id, spec["sql"].asString(), defs);
        result[id].columns = std::move(columns);
    }

    queries_ = std::move(result);
}

const CompiledQuery *QueryRegistry::find(const std::string &id) const {
    const auto it = queries_.find(id);
    return it == queries_.end() ? nullptr : &it->second;
}

}  // namespace rep

#include "dataset.h"

#include <algorithm>
#include <cctype>

namespace rep {
namespace {

bool looksLikeDate(const std::string &s) {
    return s.size() >= 10 && std::isdigit(static_cast<unsigned char>(s[0])) && s[4] == '-' && s[7] == '-';
}

bool looksLikeInt(const std::string &s) {
    return std::all_of(s.begin(), s.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) || c == '-';
    });
}

// «2026-03-01T09:30», «2026-03-01 09:30», с секундами или без — всё, что примет ::timestamp
bool looksLikeTimestamp(const std::string &s) {
    if (!looksLikeDate(s)) return false;
    if (s.size() == 10) return true;
    if (s.size() < 16 || (s[10] != 'T' && s[10] != ' ')) return false;
    if (!std::isdigit(static_cast<unsigned char>(s[11])) || !std::isdigit(static_cast<unsigned char>(s[12]))) return false;
    if (s[13] != ':') return false;
    if (!std::isdigit(static_cast<unsigned char>(s[14])) || !std::isdigit(static_cast<unsigned char>(s[15]))) return false;
    return true;
}

// Пропускает литерал с парным экранированием ('' в строках, "" в идентификаторах).
// Возвращает позицию после закрывающей кавычки.
size_t skipQuoted(const std::string &sql, size_t i, char quote) {
    size_t pos = i + 1;
    while (pos < sql.size()) {
        if (sql[pos] == quote) {
            if (pos + 1 < sql.size() && sql[pos + 1] == quote) {
                pos += 2;
                continue;
            }
            return pos + 1;
        }
        ++pos;
    }
    return sql.size();
}

} // namespace

std::string inferColumnType(const std::string &value) {
    if (value.empty()) return "text";
    // дата в ISO: ГГГГ-ММ-ДД (время и прочее сюда не попадают — это text)
    if (value.size() >= 10 && value[4] == '-' && value[7] == '-' &&
        std::isdigit(static_cast<unsigned char>(value[0])) &&
        std::isdigit(static_cast<unsigned char>(value[1])) &&
        std::isdigit(static_cast<unsigned char>(value[2])) &&
        std::isdigit(static_cast<unsigned char>(value[3]))) {
        return "date";
    }
    // число целиком: необязательный минус, цифры, необязательные дробь и экспонента
    size_t i = 0;
    if (value[i] == '-') ++i;
    bool digits = false;
    bool dot = false;
    bool exp = false;
    for (; i < value.size(); ++i) {
        const char c = value[i];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digits = true;
            continue;
        }
        if (c == '.' && !dot && !exp) {
            dot = true;
            continue;
        }
        if ((c == 'e' || c == 'E') && digits && !exp) {
            exp = true;
            if (i + 1 < value.size() && (value[i + 1] == '+' || value[i + 1] == '-')) ++i;
            continue;
        }
        return "text";
    }
    if (!digits) return "text";
    return dot || exp ? "float" : "int";
}

bool compileNamedParams(const std::string &sql, const ParamBag &bag, std::string &compiled,
                        std::vector<std::string> &values, std::string &err) {
    compiled.clear();
    values.clear();

    for (size_t i = 0; i < sql.size();) {
        const char c = sql[i];

        // Строки и идентификаторы: ':' внутри них — не плейсхолдер (иначе
        // to_char(..., 'HH24:MI') требовал параметр MI).
        if (c == '\'' || c == '"') {
            const size_t end = skipQuoted(sql, i, c);
            compiled += sql.substr(i, end - i);
            i = end;
            continue;
        }

        // Dollar-quoted $$...$$ / $tag$...$tag$ — тоже без разбора содержимого
        if (c == '$') {
            size_t tagEnd = i + 1;
            while (tagEnd < sql.size() &&
                   (std::isalnum(static_cast<unsigned char>(sql[tagEnd])) || sql[tagEnd] == '_')) ++tagEnd;
            if (tagEnd < sql.size() && sql[tagEnd] == '$') {
                const std::string tag = sql.substr(i, tagEnd - i + 1);
                const size_t close = sql.find(tag, tagEnd + 1);
                if (close != std::string::npos) {
                    compiled += sql.substr(i, close + tag.size() - i);
                    i = close + tag.size();
                    continue;
                }
            }
            compiled.push_back(c);
            ++i;
            continue;
        }

        // Комментарии: ':' в них не параметр
        if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
            const size_t eol = sql.find('\n', i);
            const size_t end = eol == std::string::npos ? sql.size() : eol;
            compiled += sql.substr(i, end - i);
            i = end;
            continue;
        }
        if (c == '/' && i + 1 < sql.size() && sql[i + 1] == '*') {
            const size_t close = sql.find("*/", i + 2);
            const size_t end = close == std::string::npos ? sql.size() : close + 2;
            compiled += sql.substr(i, end - i);
            i = end;
            continue;
        }

        if (c != ':') {
            compiled.push_back(c);
            ++i;
            continue;
        }

        // '::type' — каст, а не параметр
        if (i + 1 < sql.size() && sql[i + 1] == ':') {
            compiled += "::";
            i += 2;
            continue;
        }

        size_t j = i + 1;
        while (j < sql.size() &&
               (std::isalnum(static_cast<unsigned char>(sql[j])) || sql[j] == '_')) ++j;
        const std::string name = sql.substr(i + 1, j - i - 1);
        if (name.empty()) {
            compiled.push_back(c);
            ++i;
            continue;
        }
        const auto *v = bag.find(name);
        if (v == nullptr) {
            err = "не передан параметр '" + name + "'";
            return false;
        }
        // тип параметра берём из каста в SQL: без реестра это единственный источник
        const std::string tail = sql.substr(std::min(j, sql.size()));
        if (tail.rfind("::date", 0) == 0 && !v->empty() && !looksLikeDate(*v)) {
            err = "параметр '" + name + "' должен быть датой в формате ГГГГ-ММ-ДД";
            return false;
        }
        if (tail.rfind("::int", 0) == 0 && !v->empty() && !looksLikeInt(*v)) {
            err = "параметр '" + name + "' должен быть целым числом";
            return false;
        }
        if (tail.rfind("::timestamp", 0) == 0 && !v->empty() && !looksLikeTimestamp(*v)) {
            err = "параметр '" + name + "' должен быть датой и временем в формате ГГГГ-ММ-ДД ЧЧ:ММ";
            return false;
        }
        // пустая строка — законное значение («все», фильтр не задан)
        values.push_back(*v);
        compiled += "$" + std::to_string(values.size());
        i = j;
    }
    return true;
}

bool compileDataset(const std::string &datasetSql, const std::map<std::string, std::string> &datasetParams,
                    const ParamBag &bag, DatasetQuery &out, std::string &err) {
    if (datasetSql.empty()) {
        err = "в шаблоне не задан datasets.<key>.sql";
        return false;
    }

    // Значения приходят по имени шаблона ("$from"), уходят в SQL как значения параметра.
    ParamBag mapped;
    if (!datasetParams.empty()) {
        for (const auto &kv : datasetParams) {
            const std::string &src = kv.second;
            const std::string name = (!src.empty() && src[0] == '$') ? src.substr(1) : src;
            if (const auto *v = bag.find(name)) mapped.set(kv.first, *v);
        }
        for (const auto &kv : bag.values())
            if (!mapped.has(kv.first)) mapped.set(kv.first, kv.second);
    } else {
        mapped = bag;
    }

    std::string compiled;
    std::vector<std::string> values;
    if (!compileNamedParams(datasetSql, mapped, compiled, values, err)) return false;

    out.sql = compiled;
    out.values = values;
    return true;
}

} // namespace rep

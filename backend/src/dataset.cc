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

} // namespace

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
    for (size_t i = 0; i < datasetSql.size();) {
        const char c = datasetSql[i];
        if (c != ':') {
            compiled.push_back(c);
            ++i;
            continue;
        }
        // '::type' — каст, а не параметр
        if (i + 1 < datasetSql.size() && datasetSql[i + 1] == ':') {
            compiled += "::";
            i += 2;
            continue;
        }
        size_t j = i + 1;
        while (j < datasetSql.size() &&
               (std::isalnum(static_cast<unsigned char>(datasetSql[j])) || datasetSql[j] == '_')) ++j;
        const std::string name = datasetSql.substr(i + 1, j - i - 1);
        if (name.empty()) {
            compiled.push_back(c);
            ++i;
            continue;
        }
        const auto *v = mapped.find(name);
        if (v == nullptr) {
            err = "не передан параметр '" + name + "'";
            return false;
        }
        // тип параметра берём из каста в SQL: без реестра это единственный источник
        const std::string tail = datasetSql.substr(std::min(j, datasetSql.size()));
        if (tail.rfind("::date", 0) == 0 && !v->empty() && !looksLikeDate(*v)) {
            err = "параметр '" + name + "' должен быть датой в формате ГГГГ-ММ-ДД";
            return false;
        }
        if (tail.rfind("::int", 0) == 0 && !v->empty() && !looksLikeInt(*v)) {
            err = "параметр '" + name + "' должен быть целым числом";
            return false;
        }
        // пустая строка — законное значение («все», фильтр не задан)
        values.push_back(*v);
        compiled += "$" + std::to_string(values.size());
        i = j;
    }

    out.sql = compiled;
    out.values = values;
    return true;
}

} // namespace rep

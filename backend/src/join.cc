#include "join.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

namespace rep {

namespace {

const std::vector<std::string> kTypes = {"text", "int", "float", "date", "datetime", "bool"};

bool validType(const std::string &type) {
    return std::find(kTypes.begin(), kTypes.end(), type) != kTypes.end();
}

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Ключ приводится к типу: "05" и 5 при type=int — одно и то же; текст сравнивается
// без крайних пробелов; нечисловое значение при type=int не совпадает ни с чем.
std::string normalizeKey(const std::string &raw, const std::string &type) {
    const std::string v = trim(raw);
    if (v.empty()) return {};
    if (type == "int") {
        try {
            return "i:" + std::to_string(std::stoll(v));
        } catch (...) {
            return "bad:" + v;
        }
    }
    if (type == "float") {
        try {
            return "f:" + std::to_string(std::stod(v));
        } catch (...) {
            return "bad:" + v;
        }
    }
    if (type == "date" || type == "datetime") {
        return "d:" + v.substr(0, type == "date" ? 10 : 19);
    }
    return "t:" + v;
}

std::string compositeKey(const std::vector<std::string> &parts) {
    std::string out;
    for (const auto &p : parts) {
        out += p;
        out += '\x1f';
    }
    return out;
}

int indexOf(const DatasetRows &rows, const std::string &name) {
    for (size_t i = 0; i < rows.columns.size(); ++i) {
        if (rows.columns[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

bool parseJoin(const Json::Value &join, JoinSpec &out, std::string &error) {
    out = JoinSpec{};
    if (!join.isObject()) {
        error = "datasets.<key>.join должен быть объектом";
        return false;
    }
    const Json::Value &l = join["left"];
    const Json::Value &r = join["right"];
    out.left = l.isObject() ? l.get("dataset", "").asString() : l.asString();
    out.right = r.isObject() ? r.get("dataset", "").asString() : r.asString();
    if (out.left.empty() || out.right.empty()) {
        error = "join.left.dataset и join.right.dataset обязательны";
        return false;
    }
    out.type = join.get("type", "inner").asString();
    if (out.type != "inner" && out.type != "left") {
        error = "join.type: допустимо inner или left";
        return false;
    }
    const Json::Value &on = join["on"];
    if (!on.isArray() || on.empty() || on.size() > 4) {
        error = "join.on: нужен массив из 1…4 пар полей";
        return false;
    }
    for (const auto &pair : on) {
        if (!pair.isObject()) {
            error = "join.on: пара должна быть объектом {left, right, type}";
            return false;
        }
        JoinKey key;
        key.left = pair.get("left", "").asString();
        key.right = pair.get("right", "").asString();
        key.type = pair.get("type", "text").asString();
        if (key.left.empty() || key.right.empty()) {
            error = "join.on: у пары должны быть left и right";
            return false;
        }
        if (!validType(key.type)) {
            error = "join.on: неизвестный тип ключа '" + key.type + "'";
            return false;
        }
        out.on.push_back(key);
    }
    const Json::Value &cols = join["columns"];
    if (!cols.isArray() || cols.empty()) {
        error = "join.columns: нужен непустой массив колонок правой стороны";
        return false;
    }
    for (const auto &c : cols) {
        if (!c.isObject()) {
            error = "join.columns: колонка должна быть объектом";
            return false;
        }
        JoinColumn col;
        col.field = c.get("field", "").asString();
        if (col.field.empty()) {
            error = "join.columns: у колонки нет field";
            return false;
        }
        col.as = c.get("as", "").asString();
        col.title = c.get("title", "").asString();
        col.type = c.get("type", "text").asString();
        if (!validType(col.type)) {
            error = "join.columns." + col.field + ": неизвестный тип '" + col.type + "'";
            return false;
        }
        const std::string name = col.as.empty() ? col.field : col.as;
        for (const auto &prev : out.columns) {
            if ((prev.as.empty() ? prev.field : prev.as) == name) {
                error = "join.columns: имя '" + name + "' повторяется";
                return false;
            }
        }
        out.columns.push_back(col);
    }
    return true;
}

bool hashJoin(const DatasetRows &left, const DatasetRows &right, const JoinSpec &spec,
              DatasetRows &out, std::string &error) {
    out = DatasetRows{};
    error.clear();

    std::vector<int> leftIdx, rightIdx;
    for (const auto &key : spec.on) {
        const int li = indexOf(left, key.left);
        if (li < 0) {
            error = "join.on: в левом наборе нет колонки '" + key.left + "'";
            return false;
        }
        const int ri = indexOf(right, key.right);
        if (ri < 0) {
            error = "join.on: в правом наборе нет колонки '" + key.right + "'";
            return false;
        }
        leftIdx.push_back(li);
        rightIdx.push_back(ri);
    }

    std::vector<int> outIdx;
    for (const auto &col : spec.columns) {
        const int ri = indexOf(right, col.field);
        if (ri < 0) {
            error = "join.columns: в правом наборе нет колонки '" + col.field + "'";
            return false;
        }
        outIdx.push_back(ri);
    }

    // Колонки результата: левый набор как есть + объявленные колонки правого.
    out.columns = left.columns;
    for (const auto &col : spec.columns) {
        RowColumn c;
        c.name = col.as.empty() ? col.field : col.as;
        c.type = col.type;
        c.title = col.title;
        out.columns.push_back(c);
    }

    // Хеш по правой стороне: ключ → индексы строк (дубли допустимы).
    std::map<std::string, std::vector<size_t>> index;
    for (size_t r = 0; r < right.rows.size(); ++r) {
        const auto &row = right.rows[r];
        std::vector<std::string> parts;
        bool bad = false;
        for (size_t k = 0; k < spec.on.size(); ++k) {
            const size_t idx = static_cast<size_t>(rightIdx[k]);
            const std::string v = idx < row.size() ? row[idx] : "";
            const std::string norm = normalizeKey(v, spec.on[k].type);
            if (norm.empty() || norm.rfind("bad:", 0) == 0) {
                bad = true;
                break;
            }
            parts.push_back(norm);
        }
        if (bad) continue;
        index[compositeKey(parts)].push_back(r);
    }

    out.rows.reserve(left.rows.size());
    for (const auto &row : left.rows) {
        std::vector<std::string> parts;
        bool bad = false;
        for (size_t k = 0; k < spec.on.size(); ++k) {
            const size_t idx = static_cast<size_t>(leftIdx[k]);
            const std::string v = idx < row.size() ? row[idx] : "";
            const std::string norm = normalizeKey(v, spec.on[k].type);
            if (norm.empty() || norm.rfind("bad:", 0) == 0) {
                bad = true;
                break;
            }
            parts.push_back(norm);
        }
        const auto it = bad ? index.end() : index.find(compositeKey(parts));
        if (it == index.end()) {
            if (spec.type == "left") {
                std::vector<std::string> joined = row;
                joined.resize(left.columns.size() + spec.columns.size());
                out.rows.push_back(std::move(joined));
            }
            continue;
        }
        for (size_t ri : it->second) {
            std::vector<std::string> joined = row;
            for (size_t ci : outIdx) {
                joined.push_back(ci < right.rows[ri].size() ? right.rows[ri][ci] : "");
            }
            out.rows.push_back(std::move(joined));
        }
    }
    return true;
}

} // namespace rep

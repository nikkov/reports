#include "validate.h"

#include "template_migrate.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace rep {
namespace {

bool isParamName(const std::string &s) {
    if (s.empty() || !std::islower(static_cast<unsigned char>(s[0]))) return false;
    for (char c : s) {
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_')) {
            return false;
        }
    }
    return true;
}

bool isIdName(const std::string &s) {
    if (s.empty()) return false;
    if (!std::islower(static_cast<unsigned char>(s[0])) && !std::isdigit(static_cast<unsigned char>(s[0]))) {
        return false;
    }
    for (char c : s) {
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) ||
              c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool isOneOf(const std::string &v, const std::vector<std::string> &list) {
    return std::find(list.begin(), list.end(), v) != list.end();
}

std::string asString(const Json::Value &v) {
    return v.isString() ? v.asString() : std::string();
}

void addError(ValidationResult &r, const std::string &path, const std::string &message) {
    r.errors.push_back({path, message});
}

void addWarning(ValidationResult &r, const std::string &path, const std::string &message) {
    r.warnings.push_back({path, message});
}

// drill: ссылки на значения точки/строки, текущие параметры или литерал (ADR-29, ADR-43)
void checkDrill(ValidationResult &r, const Json::Value &block, const std::string &base, bool isTable, bool isChart) {
    if (!block.isMember("drill")) return;
    const Json::Value &drill = block["drill"];
    if (!isTable && !isChart) {
        addError(r, base + ".drill", "drill допустим только в табличном блоке или диаграмме");
        return;
    }
    if (!drill.isObject()) {
        addError(r, base + ".drill", "drill должен быть объектом");
        return;
    }
    if (asString(drill["report"]).empty()) {
        addError(r, base + ".drill.report", "drill.report обязателен");
    }
    if (!drill.isMember("params")) return;
    if (!drill["params"].isObject()) {
        addError(r, base + ".drill.params", "drill.params должен быть объектом");
        return;
    }
    // у gauge нет ни категории, ни серии: подставлять нечего (ADR-56)
    const bool gauge = isChart && asString(block["chartType"]) == "gauge";
    for (const auto &name : drill["params"].getMemberNames()) {
        const std::string ref = asString(drill["params"][name]);
        const std::string path = base + ".drill.params." + name;
        if (ref.empty()) {
            addError(r, path, "значение должно быть строкой");
            continue;
        }
        if (gauge && (ref == "x" || ref == "series")) {
            addError(r, path, "у диаграммы gauge нет x и series: подставляйте param.<name> или литерал");
            continue;
        }
        if (ref[0] == '$') {
            addError(r, path, std::string("неизвестная ссылка: используйте ") +
                                  (isChart ? "x, series, " : "group.<field>, ") + "param.<name> или строку");
            continue;
        }
        const bool known = ref.rfind("param.", 0) == 0 || (isChart && !gauge && (ref == "x" || ref == "series")) ||
                           (isTable && ref.rfind("group.", 0) == 0);
        if (!known && (ref.rfind("group.", 0) == 0 || ref == "x" || ref == "series")) {
            addError(r, path, "в диаграмме допустимы только x, series, param.<name> или литерал");
        }
    }
}

void checkTableBlock(ValidationResult &r,
                     const Json::Value &block,
                     const std::string &base,
                     const std::map<std::string, std::set<std::string>> &columnsByDataKey) {
    (void)columnsByDataKey;
    // поля этой таблицы: по ним проверяются правила и стили
    std::set<std::string> known;
    std::function<void(const Json::Value &)> collect = [&](const Json::Value &col) {
        if (col["children"].isArray()) {
            for (const auto &child : col["children"]) collect(child);
            return;
        }
        const std::string field = asString(col["field"]);
        if (!field.empty()) known.insert(field);
        const std::string expr = asString(col["expression"]);
        if (!expr.empty() && !asString(col["title"]).empty()) known.insert(asString(col["title"]));
    };
    for (const auto &col : block["columns"]) collect(col);

    bool seenLeaf = false;
    std::set<std::string> leafFields;

    std::function<void(const Json::Value &, const std::string &, bool)> walk =
        [&](const Json::Value &col, const std::string &cpath, bool topLevel) {
            const bool hasChildren = col["children"].isArray();
            const bool hasField = !asString(col["field"]).empty();
            const bool hasExpr = !asString(col["expression"]).empty();

            if (hasChildren) {
                if (hasField || hasExpr || !col["aggregate"].isNull() || col["group"].asBool() ||
                    col["subtotal"].asBool()) {
                    addError(r, cpath,
                             "у колонки-группы с children не может быть field/expression/aggregate/group");
                }
                if (topLevel && col["group"].asBool()) {
                    addError(r, cpath, "группировать по колонке-заголовку нельзя: нужна колонка с field");
                }
                Json::ArrayIndex ci = 0;
                for (const auto &child : col["children"]) {
                    walk(child, cpath + ".children." + std::to_string(ci), false);
                    ++ci;
                }
                return;
            }

            if (!hasField && !hasExpr) addError(r, cpath, "нужно указать field или expression");
            if (hasField && hasExpr) addError(r, cpath, "нельзя указывать одновременно field и expression");

            const bool group = col["group"].asBool();
            if (group && seenLeaf) {
                addError(r, cpath, "групповая колонка должна идти до обычных: порядок колонок задаёт порядок группировки");
            }
            if (!group) seenLeaf = true;
            if (col["subtotal"].asBool() && !group) {
                addError(r, cpath, "подытог допустим только у групповой колонки");
            }
            if (!group && !col["aggregate"].isNull() && !hasField) {
                addError(r, cpath, "агрегат требует поля field");
            }
            if (hasField) leafFields.insert(asString(col["field"]));
        };

    Json::ArrayIndex ci = 0;
    for (const auto &col : block["columns"]) {
        walk(col, base + ".columns." + std::to_string(ci), true);
        ++ci;
    }

    // правила условного форматирования адресуются колонкам этой же таблицы
    if (block["rules"].isArray()) {
        Json::ArrayIndex ri = 0;
        for (const auto &rule : block["rules"]) {
            const std::string rpath = base + ".rules." + std::to_string(ri);
            const auto checkWhen = [&](const Json::Value &when, const std::string &wpath) {
                if (!when.isObject()) return;
                std::function<void(const Json::Value &, const std::string &)> walkWhen =
                    [&](const Json::Value &node, const std::string &np) {
                        for (const auto &group : {"all", "any"}) {
                            if (!node[group].isArray()) continue;
                            Json::ArrayIndex gi = 0;
                            for (const auto &leaf : node[group]) {
                                walkWhen(leaf, np + "." + group + "." + std::to_string(gi));
                                ++gi;
                            }
                        }
                        const std::string column = asString(node["column"]);
                        if (column.empty()) return;
                        if (!known.empty() && !known.count(column)) {
                            addError(r, np + ".column",
                                     "правило ссылается на «" + column + "», которого нет в колонках таблицы");
                        }
                        const std::string op = asString(node["op"]);
                        if (op == "between" && (!node["value"].isArray() || node["value"].size() != 2)) {
                            addError(r, np + ".value", "для between нужен массив из двух значений");
                        }
                        if (op != "empty" && op != "notEmpty" && !node.isMember("value")) {
                            addError(r, np + ".value", "для оператора " + op + " нужно значение");
                        }
                    };
                walkWhen(when, wpath);
            };
            checkWhen(rule["when"], rpath + ".when");
            const std::string target = asString(rule["target"]);
            if (!target.empty() && !known.empty() && !known.count(target)) {
                addError(r, rpath + ".target", "target «" + target + "» отсутствует в колонках таблицы");
            }
            if (rule["zones"].isArray() && rule["zones"].empty()) {
                addError(r, rpath + ".zones", "zones не может быть пустым");
            }
            ++ri;
        }
    }

    // стили: цвет либо hex/transparent, либо из палитры; minWidth ≤ maxWidth
    const Json::Value &styles = block["styles"];
    if (styles.isObject() && styles["columns"].isObject()) {
        std::set<std::string> palette;
        for (const auto &name : styles["palette"].getMemberNames()) palette.insert(name);
        for (const auto &field : styles["columns"].getMemberNames()) {
            const std::string spath = base + ".styles.columns." + field;
            if (!known.empty() && !known.count(field)) {
                addError(r, spath, "стиль задан для поля «" + field + "», которого нет в колонках таблицы");
            }
            const Json::Value &style = styles["columns"][field];
            if (style["minWidth"].isNumeric() && style["maxWidth"].isNumeric() &&
                style["minWidth"].asInt() > style["maxWidth"].asInt()) {
                addError(r, spath, "minWidth больше maxWidth");
            }
            const auto checkColor = [&](const std::string &value, const std::string &cpath) {
                if (value.empty() || value == "transparent") return;
                const bool hex = value.size() == 4 || value.size() == 7;
                if (hex && value[0] == '#') return;
                if (!palette.count(value)) {
                    addError(r, cpath, "цвет «" + value + "» не hex и не объявлен в styles.palette");
                }
            };
            checkColor(asString(style["colors"]["text"]), spath + ".colors.text");
            checkColor(asString(style["colors"]["background"]), spath + ".colors.background");
        }
    }
    (void)leafFields;
}

void checkChartBlock(ValidationResult &r, const Json::Value &block, const std::string &base) {
    static const std::vector<std::string> types = {"bar", "line", "pie", "area", "scatter", "gauge", "sankey", "band"};
    const std::string type = asString(block["chartType"]);
    if (!isOneOf(type, types)) {
        addError(r, base + ".chartType",
                 "chartType должен быть одним из: bar, line, pie, area, scatter, gauge, sankey, band");
        // для неизвестного вида поля всё равно проверяем: сообщений об опечатках должно быть больше одного
        if (asString(block["xField"]).empty()) addError(r, base + ".xField", "xField обязателен");
        if (asString(block["yField"]).empty()) addError(r, base + ".yField", "yField обязателен");
        return;
    }

    // Состав полей зависит от вида диаграммы (ADR-56); правила зеркалят клиентский валидатор.
    const auto need = [&](const char *field) {
        if (asString(block[field]).empty()) {
            addError(r, base + "." + field, std::string("для диаграммы «") + type + "» обязательно поле " + field);
        }
    };
    const auto forbid = [&](const char *field, const char *why) {
        if (!block[field].isNull()) {
            addError(r, base + "." + field, std::string("для диаграммы «") + type + "» " + field +
                                                " не используется: " + why);
        }
    };

    if (type == "gauge") {
        need("yField");
        forbid("xField", "значение одно, категории нет");
        forbid("seriesField", "значение одно, серий нет");
        if (block["min"].isNumeric() && block["max"].isNumeric() &&
            block["max"].asDouble() <= block["min"].asDouble()) {
            addError(r, base + ".max", "max должен быть больше min");
        }
        // Цветовые зоны: N границ → N+1 зона и столько же цветов (ADR-57).
        const bool hasZones = block.isMember("thresholds") || block.isMember("zoneColors");
        if (hasZones) {
            const Json::Value &th = block["thresholds"];
            const Json::Value &colors = block["zoneColors"];
            // границы схемы (2…8) и цвета (3…9) зеркалят JSON Schema клиента
            if (colors.isArray() && colors.size() < 3) {
                addError(r, base + ".zoneColors", "нужно не меньше трёх цветов — по одному на зону");
            } else if (colors.isArray() && colors.size() > 9) {
                addError(r, base + ".zoneColors", "больше девяти цветов не поддерживается");
            }
            if (!th.isArray() || th.size() < 2) {
                addError(r, base + ".thresholds", "нужно не меньше двух границ (например p1…p4)");
            } else if (th.size() > 8) {
                addError(r, base + ".thresholds", "больше восьми границ не поддерживается");
            } else {
                for (Json::ArrayIndex i = 1; i < th.size(); ++i) {
                    if (!th[i].isNumeric() || !th[i - 1].isNumeric() ||
                        th[i].asDouble() <= th[i - 1].asDouble()) {
                        addError(r, base + ".thresholds." + std::to_string(i),
                                 "границы должны возрастать: каждое значение больше предыдущего");
                    }
                }
                if (!colors.isArray()) {
                    addError(r, base + ".zoneColors",
                             "нужно " + std::to_string(th.size() + 1) + " цвет(а/ов) — по одному на зону");
                } else if (colors.size() != th.size() + 1) {
                    addError(r, base + ".zoneColors",
                             "границ " + std::to_string(th.size()) + " — зон " + std::to_string(th.size() + 1) +
                                 ", а цветов " + std::to_string(colors.size()));
                }
            }
        }
    } else if (type == "band") {
        // значение и две границы полосы (ADR-58)
        need("xField");
        need("yField");
        need("lowField");
        need("highField");
        forbid("seriesField", "в полосе одна серия значений");
        const std::string lo = asString(block["lowField"]);
        const std::string hi = asString(block["highField"]);
        const std::string y = asString(block["yField"]);
        if (!lo.empty() && lo == hi) {
            addError(r, base + ".highField", "lower и upper — разные поля");
        }
        if (!y.empty() && (y == lo || y == hi)) {
            addError(r, base + ".yField", "значение должно быть отдельным полем, не границей");
        }
    } else if (type == "sankey") {
        need("sourceField");
        need("targetField");
        need("valueField");
        forbid("xField", "узлы задаются sourceField и targetField");
        forbid("yField", "вес потока задаётся valueField");
        const std::string s = asString(block["sourceField"]);
        const std::string t = asString(block["targetField"]);
        const std::string v = asString(block["valueField"]);
        if (!s.empty() && !t.empty() && !v.empty() && (s == t || s == v || t == v)) {
            addError(r, base + ".targetField",
                     "sourceField, targetField и valueField должны быть разными полями");
        }
    } else {
        need("xField");
        need("yField");
        if (!block["sourceField"].isNull() || !block["targetField"].isNull() || !block["valueField"].isNull()) {
            addError(r, base + ".chartType", "sourceField/targetField/valueField допустимы только для sankey");
        }
        if (!block["lowField"].isNull() || !block["highField"].isNull()) {
            addError(r, base + ".chartType", "lowField/highField допустимы только для band");
        }
    }
}

} // namespace

bool isReadOnlySql(const std::string &sql, std::string &err) {
    std::string s;
    for (char c : sql) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (s.rfind("select", 0) != 0 && s.rfind("with", 0) != 0) {
        err = "SQL должен начинаться с SELECT или WITH";
        return false;
    }
    if (sql.find(';') != std::string::npos || sql.find("--") != std::string::npos ||
        sql.find("/*") != std::string::npos) {
        err = "SQL не может содержать ';', '--' или '/*'";
        return false;
    }
    static const std::vector<std::string> forbidden = {"insert", "update", "delete", "drop", "alter", "create",
                                                       "truncate", "grant", "revoke", "copy", "merge", "call",
                                                       "do", "vacuum", "reindex"};
    for (const auto &word : forbidden) {
        std::size_t pos = 0;
        while ((pos = s.find(word, pos)) != std::string::npos) {
            const bool leftOk = pos == 0 || !std::isalnum(static_cast<unsigned char>(s[pos - 1]));
            const bool rightOk =
                pos + word.size() >= s.size() || !std::isalnum(static_cast<unsigned char>(s[pos + word.size()]));
            if (leftOk && rightOk) {
                err = "SQL содержит запрещённую операцию «" + word + "»";
                return false;
            }
            pos += word.size();
        }
    }
    return true;
}

ValidationResult validateTemplateJson(const Json::Value &root,
                                      const std::string &urlId,
                                      std::size_t maxBytes,
                                      const SqlCheck &sqlCheck) {
    ValidationResult r;
    const SqlCheck checkSql = sqlCheck ? sqlCheck : SqlCheck(isReadOnlySql);

    if (!root.isObject()) {
        addError(r, "(корень)", "шаблон должен быть JSON-объектом");
        return r;
    }
    if (maxBytes) {
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        const std::string raw = Json::writeString(wb, root);
        if (raw.size() > maxBytes) {
            addError(r, "(корень)", "шаблон больше " + std::to_string(maxBytes) + " байт");
            return r;
        }
    }

    if (root["version"].isNull()) {
        addError(r, "version", "version обязателен");
    } else if (!root["version"].isInt() || root["version"].asInt() < 1 ||
               root["version"].asInt() > kCurrentTemplateVersion) {
        addError(r, "version", "version должен быть от 1 до " +
                                   std::to_string(kCurrentTemplateVersion) + " (старые версии мигрируются)");
    }

    const std::string id = asString(root["id"]);
    if (id.empty()) {
        addError(r, "id", "id обязателен");
    } else if (!isIdName(id)) {
        // id — ключ в URL и в БД: строчные буквы, цифры, '_' и '-'
        addError(r, "id", "id должен соответствовать ^[a-z0-9][a-z0-9_-]*$");
    } else if (!urlId.empty() && id != urlId) {
        addError(r, "id", "id в шаблоне не совпадает с URL");
    }

    if (asString(root["title"]).empty()) addError(r, "title", "title обязателен");

    if (!root["blocks"].isArray() || root["blocks"].empty()) {
        addError(r, "blocks", "blocks должен быть непустым массивом");
    }
    if (!root["params"].isNull() && !root["params"].isArray()) {
        addError(r, "params", "params должен быть массивом");
    }
    if (!root["datasets"].isNull() && !root["datasets"].isObject()) {
        addError(r, "datasets", "datasets должен быть объектом");
    }
    if (root.isMember("folder") && !root["folder"].isString()) {
        addError(r, "folder", "folder должен быть строкой");
    }
    if (root.isMember("tags")) {
        if (!root["tags"].isArray()) {
            addError(r, "tags", "tags должен быть массивом строк");
        } else {
            for (const auto &t : root["tags"]) {
                if (!t.isString()) {
                    addError(r, "tags", "tags должен быть массивом строк");
                    break;
                }
            }
        }
    }
    if (root.isMember("live") && !root["live"].isBool()) {
        addError(r, "live", "live должен быть true или false");
    }

    // автообновление: целое 0…3600, меньше 5 — предупреждение (ADR-39)
    if (root.isMember("refreshSeconds") && !root["refreshSeconds"].isNull()) {
        const Json::Value &refresh = root["refreshSeconds"];
        if (!refresh.isInt()) {
            addError(r, "refreshSeconds", "refreshSeconds должен быть целым числом");
        } else if (refresh.asInt() < 0 || refresh.asInt() > 3600) {
            addError(r, "refreshSeconds", "refreshSeconds: допустимо 0…3600");
        } else if (refresh.asInt() > 0 && refresh.asInt() < 5) {
            addWarning(r, "refreshSeconds", "меньше 5 секунд — лишняя нагрузка на базу");
        }
    }

    // объявленные параметры: имена, уникальность, типы
    std::map<std::string, int> paramIndex;
    static const std::vector<std::string> paramTypes = {"date", "datetime", "int", "float", "text", "bool", "select"};
    if (root["params"].isArray()) {
        int index = 0;
        for (const auto &param : root["params"]) {
            const std::string path = "params." + std::to_string(index);
            const std::string name = asString(param["name"]);
            if (name.empty()) {
                addError(r, path + ".name", "name обязателен");
            } else if (!isParamName(name)) {
                addError(r, path + ".name", "имя параметра должно соответствовать ^[a-z][a-z0-9_]*$");
            } else if (paramIndex.count(name)) {
                addError(r, path + ".name", "параметр «" + name + "» уже объявлен");
            } else {
                paramIndex[name] = index;
            }
            const std::string type = asString(param["type"]);
            if (!type.empty() && !isOneOf(type, paramTypes)) {
                addError(r, path + ".type", "неизвестный тип параметра «" + type + "»");
            }
            ++index;
        }
    }

    // параметры: способы получения значений (ADR-22) и каскад (dependsOn)
    if (root["params"].isArray()) {
        int index = 0;
        for (const auto &param : root["params"]) {
            const std::string path = "params." + std::to_string(index);
            const std::string type = asString(param["type"]);
            const Json::Value &src = param["source"];

            if (src.isNull()) {
                if (type == "select" && (!param["options"].isArray() || param["options"].empty())) {
                    addError(r, path, "для select нужен source.options или source.kind=lookup");
                }
                ++index;
                continue;
            }
            if (!src.isObject()) {
                addError(r, path + ".source", "source должен быть объектом");
                ++index;
                continue;
            }

            const std::string kind = asString(src["kind"]);
            static const std::vector<std::string> kinds = {"lookup", "static", "date", "datetime"};
            if (!kind.empty() && !isOneOf(kind, kinds)) {
                addError(r, path + ".source", "неизвестный source.kind «" + kind + "»");
            }

            if (kind == "lookup") {
                const std::string sql = asString(src["sql"]);
                if (sql.empty()) {
                    addError(r, path + ".source", "для source.kind=lookup обязателен sql (SQL списка задаётся в шаблоне)");
                } else {
                    std::string sqlErr;
                    if (!checkSql(sql, sqlErr)) addError(r, path + ".source", sqlErr);
                }
                if (src.isMember("queryId")) {
                    addError(r, path + ".source", "queryId больше не поддерживается: перенесите SQL в source.sql");
                }
                if (src.isMember("options")) addError(r, path + ".source", "lookup и options взаимоисключающие");
                if (type != "select") addError(r, path + ".type", "параметр со справочником должен иметь type=select");
            } else if (kind == "static") {
                if (!src["options"].isArray() || src["options"].empty()) {
                    addError(r, path + ".source", "для source.kind=static нужен непустой options");
                }
                if (src.isMember("queryId") || src.isMember("sql")) {
                    addError(r, path + ".source", "static не принимает queryId/sql");
                }
                if (type != "select") {
                    addError(r, path + ".type", "параметр со статическим набором должен иметь type=select");
                }
            } else if (kind == "date" || kind == "datetime") {
                if (src.isMember("queryId") || src.isMember("sql") || src.isMember("options")) {
                    addError(r, path + ".source", "для даты/времени queryId и options недопустимы");
                }
                if (type != kind) addError(r, path + ".type", "для source.kind=" + kind + " ожидался type=" + kind);
            }

            if (param["multiple"].asBool() && kind != "lookup" && kind != "static") {
                addError(r, path + ".multiple", "множественный выбор допустим только для справочника или набора");
            }

            const std::string parentName = asString(src["dependsOn"]);
            if (!parentName.empty()) {
                const auto parent = paramIndex.find(parentName);
                if (parent == paramIndex.end()) {
                    addError(r, path + ".source.dependsOn", "неизвестный параметр-родитель «" + parentName + "»");
                } else {
                    if (parent->second > index) {
                        addError(r, path + ".source.dependsOn", "параметр-родитель должен быть объявлен выше");
                    }
                    if (root["params"][parent->second]["multiple"].asBool()) {
                        addError(r, path + ".source.dependsOn", "зависимость от множественного выбора не поддерживается");
                    }
                }
            }
            ++index;
        }
    }

    // наборы данных: SQL в шаблоне, ссылки только на объявленные параметры (ADR-27)
    std::map<std::string, std::set<std::string>> columnsByDataKey;
    if (root["datasets"].isObject()) {
        for (const auto &key : root["datasets"].getMemberNames()) {
            const Json::Value &ds = root["datasets"][key];
            const std::string path = "datasets." + key;
            if (!ds.isObject()) {
                addError(r, path, "набор данных должен быть объектом");
                continue;
            }
            if (ds.isMember("queryId")) {
                // два пути, как у клиента: поле не по схеме + смысловая подсказка
                addError(r, path + ".queryId", "недопустимое поле: «queryId»");
                addError(r, path, "queryId больше не поддерживается: SQL задаётся в datasets.<key>.sql");
            }
            const std::string sql = asString(ds["sql"]);
            if (sql.empty()) {
                // путь совпадает с клиентским (required.sql в схеме) — см. фикстуры parity
                addError(r, path + ".sql", "у набора данных нет sql");
            } else {
                std::string sqlErr;
                if (!checkSql(sql, sqlErr)) addError(r, path, sqlErr);
            }
            if (ds["params"].isObject()) {
                for (const auto &paramName : ds["params"].getMemberNames()) {
                    const Json::Value &source = ds["params"][paramName];
                    if (!source.isString()) continue;
                    const std::string ref = source.asString();
                    if (ref.empty() || ref[0] != '$') continue;
                    if (!paramIndex.count(ref.substr(1))) {
                        addError(r, path + ".params." + paramName,
                                 "ссылка на необъявленный параметр «" + ref.substr(1) + "»");
                    }
                }
            }
        }
    }

    // блоки: тип, набор данных, колонки, диаграммы, текст
    if (root["blocks"].isArray()) {
        Json::ArrayIndex index = 0;
        for (const auto &block : root["blocks"]) {
            const std::string base = "blocks." + std::to_string(index);
            const std::string type = asString(block["type"]);
            const bool isTable = type == "table";
            const bool isChart = type == "chart";

            if (type != "text" && !isTable && !isChart) {
                addError(r, base + ".type", "неизвестный тип блока «" + (type.empty() ? "—" : type) +
                                                "» (допустимо: text, table, chart)");
                ++index;
                continue;
            }

            if (type == "text") {
                const std::string text = asString(block["text"]);
                if (text.empty()) addError(r, base + ".text", "text обязателен для текстового блока");
                for (std::size_t pos = 0; (pos = text.find("{{", pos)) != std::string::npos;) {
                    const std::size_t end = text.find("}}", pos);
                    if (end == std::string::npos) break;
                    std::string name = text.substr(pos + 2, end - pos - 2);
                    name.erase(std::remove_if(name.begin(), name.end(),
                                              [](unsigned char c) { return std::isspace(c) != 0; }),
                               name.end());
                    if (!name.empty() && !paramIndex.count(name)) {
                        addError(r, base + ".text", "в тексте используется необъявленный параметр «" + name + "»");
                    }
                    pos = end + 2;
                }
                checkDrill(r, block, base, false, false);
                ++index;
                continue;
            }

            const std::string dataKey = asString(block["dataKey"]);
            if (dataKey.empty()) {
                addError(r, base + ".dataKey", "dataKey обязателен");
            } else if (!root["datasets"].isObject() || !root["datasets"].isMember(dataKey)) {
                addError(r, base + ".dataKey", "набор данных «" + dataKey + "» не объявлен в datasets");
            }

            if (isTable) {
                if (!block["columns"].isArray() || block["columns"].empty()) {
                    addError(r, base + ".columns", "columns должен быть непустым массивом");
                } else {
                    checkTableBlock(r, block, base, columnsByDataKey);
                    if (block["grandTotal"].asBool()) {
                        bool hasLeaf = false;
                        for (const auto &col : block["columns"]) {
                            if (!col["group"].asBool()) hasLeaf = true;
                        }
                        if (!hasLeaf) {
                            addWarning(r, base + ".grandTotal", "нет листовых колонок для строки ИТОГО");
                        }
                    }
                }
                checkDrill(r, block, base, true, false);
            } else {
                checkChartBlock(r, block, base);
                checkDrill(r, block, base, false, true);
            }
            ++index;
        }
    }

    return r;
}

} // namespace rep

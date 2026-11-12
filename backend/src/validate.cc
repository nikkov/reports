#include "validate.h"

#include "dates_expr.h"
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

// Имя переменной окружения: PGPASSWORD, REP_CONN_FINANCE_PASSWORD — буквы любого
// регистра, цифры и подчёркивание, начинается с буквы или подчёркивания.
bool isEnvName(const std::string &s) {
    if (s.empty()) return false;
    const auto first = static_cast<unsigned char>(s[0]);
    if (!std::isalpha(first) && first != '_') return false;
    for (char c : s) {
        const auto ch = static_cast<unsigned char>(c);
        if (!std::isalnum(ch) && ch != '_') return false;
    }
    return true;
}

// Значение поля источника может быть целиком подстановкой ${ИМЯ} (ADR-77).
bool isEnvRefValue(const std::string &s) {
    if (s.size() < 4 || s.rfind("${", 0) != 0 || s.back() != '}') return false;
    const std::string name = s.substr(2, s.size() - 3);
    return isEnvName(name);
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
    // цепочка уровней sankey умеет отдавать значения уровней в переход (ADR-72)
    const bool chain = isChart && asString(block["chartType"]) == "sankey" && block["levels"].isArray();
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
        if (ref == "node") {
            if (!chain) addError(r, path, "node допустим только в sankey с levels");
            continue;
        }
        if (ref.rfind("level.", 0) == 0) {
            if (!chain) {
                addError(r, path, "level.<поле> допустим только в sankey с levels");
                continue;
            }
            const std::string field = ref.substr(6);
            bool found = false;
            for (const auto &level : block["levels"]) {
                if (asString(level) == field) found = true;
            }
            if (!found) addError(r, path, "уровня «" + field + "» нет в levels");
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

// объявлены ниже: нужны и табличному блоку, и диаграмме
void checkBlockHeight(ValidationResult &r, const Json::Value &block, const std::string &path);
void checkDateFormat(ValidationResult &r, const Json::Value &fmt, const std::string &path);

void checkTableBlock(ValidationResult &r,
                     const Json::Value &block,
                     const std::string &base,
                     const std::map<std::string, std::set<std::string>> &columnsByDataKey) {
    (void)columnsByDataKey;
    checkBlockHeight(r, block, base);
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
            checkDateFormat(r, col["format"], cpath + ".format");
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
            checkDateFormat(r, style["format"], spath + ".format");
        }
    }
    (void)leafFields;
}

// Глобальные styles.columns: шаблон даты проверяется так же, как у блока (ADR-88).
void checkGlobalStyleColumns(ValidationResult &r, const Json::Value &root) {
    const Json::Value &styles = root["styles"];
    if (!styles.isObject() || !styles["columns"].isObject()) return;
    for (const auto &field : styles["columns"].getMemberNames()) {
        const Json::Value &style = styles["columns"][field];
        checkDateFormat(r, style["format"], "styles.columns." + field + ".format");
    }
}

// Высота блока (ADR-85): пиксели целым числом или процент от высоты окна строкой «60%».
// Ограничения общие для таблицы и диаграммы; клиент переводит процент в пиксели.
// Шаблон даты в format.pattern (ADR-88): известные токены и разделители,
// обязателен год. Те же правила, что в клиентском валидаторе (parity).
void checkDateFormat(ValidationResult &r, const Json::Value &fmt, const std::string &path) {
    if (!fmt.isObject() || !fmt.isMember("pattern") || fmt["pattern"].isNull()) return;
    const std::string pattern = asString(fmt["pattern"]);
    if (pattern.empty()) {
        addError(r, path + ".pattern", "pattern: непустая строка с токенами даты");
        return;
    }
    if (!fmt["style"].isNull() && asString(fmt["style"]) != "date") {
        addError(r, path + ".pattern", "pattern допустим только для формата даты (style: \"date\")");
    }
    static const std::vector<std::string> tokens = {"YYYY", "YY", "MM", "M", "DD", "D", "HH", "H", "mm", "ss"};
    size_t i = 0;
    while (i < pattern.size()) {
        bool matched = false;
        for (const auto &t : tokens) {
            if (pattern.compare(i, t.size(), t) == 0) {
                i += t.size();
                matched = true;
                break;
            }
        }
        if (matched) continue;
        const unsigned char c = static_cast<unsigned char>(pattern[i]);
        const bool separator = std::isdigit(c) || c == ' ' || c == '.' || c == ',' || c == ':' ||
                               c == '/' || c == ';' || c == '-' || c == 'T';
        if (!separator) {
            addError(r, path + ".pattern",
                     "pattern: недопустимые буквы; допустимы YYYY, YY, MM, M, DD, D, HH, H, mm, ss");
            return;
        }
        ++i;
    }
}

void checkBlockHeight(ValidationResult &r, const Json::Value &block, const std::string &path) {
    if (!block.isMember("height") || block["height"].isNull()) return;
    const Json::Value &h = block["height"];
    const std::string hint = "height: пиксели — целое 160…2000 или процент от окна («60%»)";
    if (h.isInt()) {
        const int px = h.asInt();
        if (px < 160 || px > 2000) addError(r, path + ".height", hint);
        return;
    }
    if (h.isString()) {
        const std::string s = h.asString();
        if (s.size() < 2 || s.size() > 4 || s.back() != '%') {
            addError(r, path + ".height", hint);
            return;
        }
        const std::string digits = s.substr(0, s.size() - 1);
        if (digits.find_first_not_of("0123456789") != std::string::npos) {
            addError(r, path + ".height", hint);
            return;
        }
        const int pct = std::stoi(digits);
        if (pct < 10 || pct > 100) {
            addError(r, path + ".height", "height: процент от высоты окна — от 10 до 100 («60%»)");
        }
        return;
    }
    addError(r, path + ".height", hint);
}

void checkChartBlock(ValidationResult &r, const Json::Value &block, const std::string &base) {
    checkBlockHeight(r, block, base);
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
        if (!block["levels"].isNull()) {
            addError(r, base + ".levels", "levels допустимы только для sankey");
        }
        if (!block["bands"].isNull()) {
            addError(r, base + ".bands", "bands допустимы только для band");
        }
    } else if (type == "band") {
        // значение и одна или несколько пар границ (ADR-58, ADR-69)
        need("xField");
        need("yField");
        forbid("seriesField", "в полосе одна серия значений");
        const std::string y = asString(block["yField"]);
        if (!block["bands"].isNull()) {
            const Json::Value &bands = block["bands"];
            if (!bands.isArray() || bands.size() < 1 || bands.size() > 4) {
                addError(r, base + ".bands", "bands: от 1 до 4 коридоров");
            } else {
                for (Json::ArrayIndex i = 0; i < bands.size(); ++i) {
                    const Json::Value &corridor = bands[i];
                    const std::string path = base + ".bands." + std::to_string(i);
                    if (!corridor.isObject()) {
                        addError(r, path, "коридор — объект { lowField, highField }");
                        continue;
                    }
                    const std::string lo = asString(corridor["lowField"]);
                    const std::string hi = asString(corridor["highField"]);
                    if (lo.empty()) addError(r, path + ".lowField", "нужен lowField");
                    if (hi.empty()) addError(r, path + ".highField", "нужен highField");
                    if (!lo.empty() && lo == hi) {
                        addError(r, path + ".highField", "границы коридора — разные поля");
                    }
                    if (!y.empty() && (y == lo || y == hi)) {
                        addError(r, path + ".lowField", "значение должно быть отдельным полем, не границей");
                    }
                }
                if (!block["lowField"].isNull() || !block["highField"].isNull()) {
                    addError(r, base + ".lowField", "вместе с bands поля lowField/highField не задаются");
                }
            }
        } else {
            need("lowField");
            need("highField");
            const std::string lo = asString(block["lowField"]);
            const std::string hi = asString(block["highField"]);
            if (!lo.empty() && lo == hi) {
                addError(r, base + ".highField", "lower и upper — разные поля");
            }
            if (!y.empty() && (y == lo || y == hi)) {
                addError(r, base + ".yField", "значение должно быть отдельным полем, не границей");
            }
        }
        if (!block["levels"].isNull()) {
            addError(r, base + ".levels", "levels допустимы только для sankey");
        }
    } else if (type == "sankey") {
        // Поток задаётся либо парой source/target, либо цепочкой уровней (ADR-68)
        need("valueField");
        std::vector<std::string> chain;
        const bool hasLevels = !block["levels"].isNull();
        if (hasLevels) {
            if (!block["levels"].isArray() || block["levels"].size() < 2 || block["levels"].size() > 4) {
                addError(r, base + ".levels", "levels: от 2 до 4 полей");
            } else {
                for (const auto &item : block["levels"]) {
                    const std::string field = asString(item);
                    if (field.empty()) {
                        addError(r, base + ".levels", "в levels только непустые имена полей");
                    }
                    chain.push_back(field);
                }
                if (std::set<std::string>(chain.begin(), chain.end()).size() != chain.size()) {
                    addError(r, base + ".levels", "поля уровней не должны повторяться");
                }
                if (!block["sourceField"].isNull() || !block["targetField"].isNull()) {
                    addError(r, base + ".sourceField", "у цепочки уровней sourceField/targetField не задаются");
                }
            }
        } else {
            need("sourceField");
            need("targetField");
        }
        forbid("xField", "узлы задаются sourceField/targetField или levels");
        forbid("yField", "вес потока задаётся valueField");
        std::vector<std::string> names;
        if (hasLevels) {
            for (const auto &field : chain) {
                if (!field.empty()) names.push_back(field);
            }
            const std::string weight = asString(block["valueField"]);
            if (!weight.empty()) names.push_back(weight);
            if (std::set<std::string>(names.begin(), names.end()).size() != names.size()) {
                addError(r, base + ".levels", "поля уровней и веса должны быть разными");
            }
        } else {
            for (const char *field : {"sourceField", "targetField", "valueField"}) {
                const std::string value = asString(block[field]);
                if (!value.empty()) names.push_back(value);
            }
            if (std::set<std::string>(names.begin(), names.end()).size() != names.size()) {
                addError(r, base + ".targetField",
                         "sourceField, targetField и valueField должны быть разными полями");
            }
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
        if (!block["levels"].isNull()) {
            addError(r, base + ".levels", "levels допустимы только для sankey");
        }
        if (!block["bands"].isNull()) {
            addError(r, base + ".bands", "bands допустимы только для band");
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

    // Источники данных (ADR-76): блок connections описывает, КУДА идти за данными —
    // адрес, базу, пользователя, схему и переменную окружения с паролем. Пароль в шаблоне
    // хранить нельзя, поэтому поле password — ошибка, а не предупреждение. Наборы и
    // справочники ссылаются на источник по ключу; `default` (и пустое значение) — окружение
    // сервера, объявлять его не обязательно.
    // ключи наборов нужны и для ссылок source.dataset (ADR-78), поэтому собираем заранее
    std::set<std::string> datasetKeys;
    if (root["datasets"].isObject()) {
        for (const auto &key : root["datasets"].getMemberNames()) datasetKeys.insert(key);
    }

    std::set<std::string> connKeys;
    if (!root["connections"].isNull()) {
        if (!root["connections"].isObject()) {
            addError(r, "connections", "connections должен быть объектом");
        } else {
            static const std::set<std::string> kinds = {"postgres", "rest"};
            for (const auto &key : root["connections"].getMemberNames()) {
                const std::string base = "connections." + key;
                const Json::Value &conn = root["connections"][key];
                if (!conn.isObject()) {
                    addError(r, base, "источник должен быть объектом");
                    continue;
                }
                if (!isIdName(key)) {
                    addError(r, base, "ключ источника должен соответствовать ^[a-z0-9][a-z0-9_-]*$");
                }
                connKeys.insert(key);
                const std::string kind = conn.get("kind", "postgres").asString();
                if (!kinds.count(kind)) {
                    addError(r, base + ".kind",
                             "неизвестный kind «" + kind + "»: поддержаны postgres и rest");
                    continue;
                }
                if (conn.isMember("password")) {
                    addError(r, base + ".password",
                             "пароль в шаблоне не хранится: укажите passwordEnv с именем переменной окружения");
                }
                if (kind == "postgres") {
                    if (asString(conn["host"]).empty()) addError(r, base + ".host", "для postgres нужен host");
                    if (asString(conn["database"]).empty()) {
                        addError(r, base + ".database", "для postgres нужна database");
                    }
                    if (asString(conn["user"]).empty()) addError(r, base + ".user", "для postgres нужен user");
                    // подстановка окружения допускается только целым значением поля: так
                    // видно, что именно берётся извне, и ошибочная запись ловится заранее
                    for (const char *field : {"host", "port", "database", "user", "schema"}) {
                        const std::string raw = asString(conn[field]);
                        if (raw.find("${") == std::string::npos) continue;
                        if (!isEnvRefValue(raw)) {
                            addError(r, base + "." + field,
                                     "подстановка окружения задаётся целиком: ${ИМЯ_ПЕРЕМЕННОЙ}");
                        }
                    }
                    const std::string pwEnv = asString(conn["passwordEnv"]);
                    if (pwEnv.empty()) {
                        addError(r, base + ".passwordEnv",
                                 "для postgres нужен passwordEnv — имя переменной окружения с паролем");
                    } else if (!isEnvName(pwEnv)) {
                        addError(r, base + ".passwordEnv", "passwordEnv должен быть именем переменной окружения");
                    }
                    if (conn.isMember("port") && !conn["port"].isString() && !conn["port"].isInt()) {
                        addError(r, base + ".port", "port должен быть числом или строкой");
                    }
                    const std::string schema = asString(conn["schema"]);
                    if (!schema.empty() && !isIdName(schema) && !isEnvRefValue(schema)) {
                        addError(r, base + ".schema", "schema должен быть идентификатором или ${ИМЯ_ПЕРЕМЕННОЙ}");
                    }
                    const std::string ssl = asString(conn.isMember("sslmode") ? conn["sslmode"] : conn["sslMode"]);
                    if (!ssl.empty()) {
                        static const std::set<std::string> modes = {"disable", "allow", "prefer",
                                                                    "require", "verify-ca", "verify-full"};
                        if (!modes.count(ssl)) {
                            addError(r, base + ".sslmode",
                                     "sslmode: disable, allow, prefer, require, verify-ca или verify-full");
                        }
                    }
                    if (conn.isMember("connectTimeout")) {
                        const auto &value = conn["connectTimeout"];
                        if (!value.isInt() || value.asInt() < 1 || value.asInt() > 120) {
                            addError(r, base + ".connectTimeout", "connectTimeout — целое 1…120 секунд");
                        }
                    }
                    if (conn.isMember("statementTimeoutMs")) {
                        const auto &value = conn["statementTimeoutMs"];
                        if (!value.isInt() || value.asInt() < 0 || value.asInt() > 600000) {
                            addError(r, base + ".statementTimeoutMs",
                                     "statementTimeoutMs — целое 0…600000 (0 — без ограничения)");
                        }
                    }
                } else {  // rest — структура зарезервирована под будущие источники
                    if (asString(conn["baseUrl"]).empty()) addError(r, base + ".baseUrl", "для rest нужен baseUrl");
                    if (conn.isMember("headers") && !conn["headers"].isObject()) {
                        addError(r, base + ".headers", "headers должен быть объектом");
                    }
                }
            }
        }
    }
    // Источник по умолчанию — окружение сервера: объявлять его не обязательно, а если
    // объявлен, он должен быть postgres (rest на месте default сломал бы все отчёты).
    if (connKeys.count("default") && root["connections"]["default"].isObject() &&
        root["connections"]["default"].get("kind", "postgres").asString() != "postgres") {
        addError(r, "connections.default", "источник default должен быть postgres");
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
            // Арифметика дат в default (ADR-92): выражения допустимы у date и datetime.
            // Разбор тот же, что у клиента (frontend/src/template/dates.js), — см. фикстуры parity.
            if ((type == "date" || type == "datetime") && param["default"].isString()) {
                const std::string def = param["default"].asString();
                std::string derr;
                if (LooksLikeDateExpr(def) && !CheckDateExprSyntax(def, derr)) {
                    addError(r, path + ".default", "default: " + derr);
                }
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
            const std::string kind = src.isObject() ? asString(src["kind"]) : "";

            // Явный пустой элемент списка (ADR-84): интерфейс больше не подставляет «—»,
            // шаблон сам говорит, что добавить в начало списка и с каким значением.
            // Проверяется до раннего выхода: от source не зависит.
            if (param.isMember("emptyOption")) {
                const Json::Value &blank = param["emptyOption"];
                if (kind != "lookup") {
                    addError(r, path + ".emptyOption",
                             "emptyOption допустим только у справочника (source.kind=lookup)");
                }
                if (param["multiple"].asBool()) {
                    addError(r, path + ".emptyOption",
                             "emptyOption не сочетается с multiple: пустой элемент — для одиночного выбора");
                }
                if (!blank.isObject()) {
                    addError(r, path + ".emptyOption",
                             "emptyOption должен быть объектом {value, label}");
                } else {
                    if (!blank.isMember("value") ||
                        !(blank["value"].isString() || blank["value"].isNumeric() || blank["value"].isBool())) {
                        addError(r, path + ".emptyOption.value",
                                 "emptyOption.value должен быть строкой, числом или логическим значением");
                    }
                    if (!blank["label"].isString() || asString(blank["label"]).empty()) {
                        addError(r, path + ".emptyOption.label",
                                 "emptyOption.label должен быть непустой строкой");
                    }
                    for (const auto &name : blank.getMemberNames()) {
                        if (name != "value" && name != "label") {
                            addError(r, path + ".emptyOption." + name,
                                     "недопустимое поле: «" + name + "»");
                        }
                    }
                }
            }

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

            static const std::vector<std::string> kinds = {"lookup", "static", "date", "datetime"};
            if (!kind.empty() && !isOneOf(kind, kinds)) {
                addError(r, path + ".source", "неизвестный source.kind «" + kind + "»");
            }

            if (kind == "lookup") {
                // Справочник — это набор данных (ADR-78): SQL и соединение живут в datasets,
                // параметр ссылается на набор. Старый source.sql отклоняется с подсказкой.
                if (src.isMember("sql")) {
                    addError(r, path + ".source.sql",
                             "SQL справочника задаётся набором datasets.<ключ>: source.sql больше не поддерживается");
                }
                const std::string dataset = asString(src["dataset"]);
                if (dataset.empty()) {
                    addError(r, path + ".source",
                             "для source.kind=lookup обязателен dataset — набор из datasets");
                } else if (!datasetKeys.count(dataset)) {
                    addError(r, path + ".source.dataset",
                             "набор «" + dataset + "» не объявлен в datasets");
                }
                if (src.isMember("queryId")) {
                    addError(r, path + ".source", "queryId больше не поддерживается");
                }
                if (src.isMember("options")) addError(r, path + ".source", "lookup и options взаимоисключающие");
                if (src.isMember("connection")) {
                    addError(r, path + ".source.connection",
                             "соединение задаётся в datasets.<ключ>.connection");
                }
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
                if (src.isMember("dataset")) {
                    addError(r, path + ".source.dataset", "dataset допустим только у source.kind=lookup");
                }
            } else if (src.isMember("dataset")) {
                addError(r, path + ".source.dataset", "dataset допустим только у source.kind=lookup");
            }

            // соединение и набор — свойства datasets (ADR-78), у параметра их быть не может
            if (kind != "lookup" && src.isMember("connection")) {
                addError(r, path + ".source.connection", "соединение задаётся в datasets.<ключ>.connection");
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
            const std::string dsConn = asString(ds["connection"]);
            if (!dsConn.empty() && dsConn != "default" && !connKeys.count(dsConn)) {
                addError(r, path + ".connection", "источник «" + dsConn + "» не объявлен в connections");
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

            // Стили набора (ADR-91): блоки этого набора наследуют их, block.styles перекрывает.
            // Поля колонок набора статически неизвестны, поэтому проверяем формат даты и цвета.
            const Json::Value &dsStyles = ds["styles"];
            if (dsStyles.isObject() && dsStyles["columns"].isObject()) {
                std::set<std::string> palette;
                for (const auto &name : root["styles"]["palette"].getMemberNames()) palette.insert(name);
                for (const auto &field : dsStyles["columns"].getMemberNames()) {
                    const std::string spath = path + ".styles.columns." + field;
                    const Json::Value &style = dsStyles["columns"][field];
                    checkDateFormat(r, style["format"], spath + ".format");
                    for (const char *zone : {"text", "background"}) {
                        const std::string value = asString(style["colors"][zone]);
                        if (value.empty() || value == "transparent") continue;
                        const bool hex = value.size() == 4 || value.size() == 7;
                        if (hex && value[0] == '#') continue;
                        if (!palette.count(value)) {
                            addError(r, spath + ".colors." + zone,
                                     "цвет «" + value + "» не hex и не объявлен в styles.palette");
                        }
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
                if (block.isMember("height") && !block["height"].isNull()) {
                    addError(r, base + ".height", "height допустим только у таблицы и диаграммы");
                }
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
                // порядок деталей группы (ADR-71): только у блока с серверной агрегацией
                if (!block["detailOrder"].isNull()) {
                    const Json::Value &order = block["detailOrder"];
                    const bool aggregated = block["aggregate"].isObject();
                    if (!order.isArray() || order.size() < 1 || order.size() > 4) {
                        addError(r, base + ".detailOrder", "detailOrder: от 1 до 4 полей");
                    } else if (!aggregated) {
                        addError(r, base + ".detailOrder", "detailOrder имеет смысл только с aggregate");
                    } else {
                        for (Json::ArrayIndex i = 0; i < order.size(); ++i) {
                            const Json::Value &item = order[i];
                            const std::string path = base + ".detailOrder." + std::to_string(i);
                            if (!item.isObject()) {
                                addError(r, path, "элемент — объект {field, dir}");
                                continue;
                            }
                            const std::string field = item["field"].asString();
                            if (field.empty() || !std::all_of(field.begin(), field.end(), [](unsigned char c) {
                                    return std::isalnum(c) || c == '_';
                                }) || std::isdigit(static_cast<unsigned char>(field[0]))) {
                                addError(r, path + ".field", "нужно имя поля");
                            }
                            if (!item["dir"].isNull()) {
                                const std::string dir = item["dir"].asString();
                                if (dir != "asc" && dir != "desc") {
                                    addError(r, path + ".dir", "направление: asc или desc");
                                }
                            }
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

    checkGlobalStyleColumns(r, root);

    return r;
}

} // namespace rep

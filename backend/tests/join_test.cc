#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "join.h"

using rep::DatasetRows;
using rep::JoinSpec;
using rep::RowColumn;
using rep::hashJoin;
using rep::parseJoin;

namespace {

JoinSpec spec(const std::string &type) {
    JoinSpec s;
    s.left = "orders";
    s.right = "regions";
    s.type = type;
    s.on = {{"region_id", "id", "int"}};
    s.columns = {{"name", "region_name", "Регион", "text"}};
    return s;
}

DatasetRows leftRows() {
    DatasetRows r;
    r.columns = {{"region_id", "int", ""}, {"amount", "int", ""}};
    r.rows = {{"1", "100"}, {"2", "200"}, {"3", "300"}, {"", "400"}};
    return r;
}

DatasetRows rightRows() {
    DatasetRows r;
    r.columns = {{"id", "int", ""}, {"name", "text", ""}};
    r.rows = {{"1", "Север"}, {"2", "Юг"}, {"2", "Дубль"}};
    return r;
}

}  // namespace

TEST_CASE("join: inner оставляет только совпадения") {
    DatasetRows out;
    std::string err;
    REQUIRE(hashJoin(leftRows(), rightRows(), spec("inner"), out, err));
    REQUIRE(err.empty());
    REQUIRE(out.columns.size() == 3);
    REQUIRE(out.columns[2].name == "region_name");
    // 1 → одна строка, 2 → две (дубль справа), 3 и пустой — нет
    REQUIRE(out.rows.size() == 3);
    REQUIRE(out.rows[0][2] == "Север");
    REQUIRE(out.rows[1][2] == "Юг");
    REQUIRE(out.rows[2][2] == "Дубль");
}

TEST_CASE("join: left сохраняет строки без пары") {
    DatasetRows out;
    std::string err;
    REQUIRE(hashJoin(leftRows(), rightRows(), spec("left"), out, err));
    REQUIRE(out.rows.size() == 5); // 1 + (2,2) + 3 + пустой ключ
    REQUIRE(out.rows[3][0] == "3");
    REQUIRE(out.rows[3][2].empty());
    REQUIRE(out.rows[4][0].empty());
    REQUIRE(out.rows[4].size() == 3);
}

TEST_CASE("join: ключ int нормализуется по значению") {
    DatasetRows left;
    left.columns = {{"region_id", "int", ""}};
    left.rows = {{"05"}};
    DatasetRows right;
    right.columns = {{"id", "int", ""}, {"name", "text", ""}};
    right.rows = {{"5", "Пять"}};
    JoinSpec s = spec("inner");
    DatasetRows out;
    std::string err;
    REQUIRE(hashJoin(left, right, s, out, err));
    REQUIRE(out.rows.size() == 1);
    REQUIRE(out.rows[0][1] == "Пять");
}

TEST_CASE("join: нечисловое значение при type=int не совпадает") {
    DatasetRows left;
    left.columns = {{"region_id", "int", ""}};
    left.rows = {{"нет"}};
    DatasetRows right;
    right.columns = {{"id", "int", ""}, {"name", "text", ""}};
    right.rows = {{"0", "Ноль"}};
    DatasetRows out;
    std::string err;
    REQUIRE(hashJoin(left, right, spec("inner"), out, err));
    REQUIRE(out.rows.empty());
}

TEST_CASE("join: составной ключ из двух пар") {
    JoinSpec s = spec("inner");
    s.on = {{"region_id", "id", "int"}, {"year", "y", "int"}};
    DatasetRows left;
    left.columns = {{"region_id", "int", ""}, {"year", "int", ""}, {"v", "int", ""}};
    left.rows = {{"1", "2026", "10"}, {"1", "2025", "20"}};
    DatasetRows right;
    right.columns = {{"id", "int", ""}, {"y", "int", ""}, {"name", "text", ""}};
    right.rows = {{"1", "2026", "Север-2026"}, {"1", "2025", "Север-2025"}};
    DatasetRows out;
    std::string err;
    REQUIRE(hashJoin(left, right, s, out, err));
    REQUIRE(out.rows.size() == 2);
    REQUIRE(out.rows[0][3] == "Север-2026");
    REQUIRE(out.rows[1][3] == "Север-2025");
}

TEST_CASE("join: нет колонки ключа — понятная ошибка") {
    JoinSpec s = spec("inner");
    s.on = {{"nope", "id", "int"}};
    DatasetRows out;
    std::string err;
    REQUIRE_FALSE(hashJoin(leftRows(), rightRows(), s, out, err));
    REQUIRE(err.find("nope") != std::string::npos);
}

TEST_CASE("join: разбор спецификации") {
    Json::Value join;
    join["left"]["dataset"] = "orders";
    join["right"]["dataset"] = "regions";
    join["type"] = "left";
    Json::Value on;
    Json::Value k;
    k["left"] = "region_id";
    k["right"] = "id";
    k["type"] = "int";
    on.append(k);
    join["on"] = on;
    Json::Value cols;
    Json::Value c;
    c["field"] = "name";
    c["as"] = "region_name";
    c["title"] = "Регион";
    cols.append(c);
    join["columns"] = cols;

    JoinSpec out;
    std::string err;
    REQUIRE(parseJoin(join, out, err));
    REQUIRE(out.left == "orders");
    REQUIRE(out.right == "regions");
    REQUIRE(out.type == "left");
    REQUIRE(out.on.size() == 1);
    REQUIRE(out.on[0].type == "int");
    REQUIRE(out.columns[0].as == "region_name");
}

TEST_CASE("join: разбор отклоняет битые спецификации") {
    JoinSpec out;
    std::string err;
    Json::Value empty;
    REQUIRE_FALSE(parseJoin(empty, out, err));

    Json::Value join;
    join["left"]["dataset"] = "a";
    join["right"]["dataset"] = "b";
    join["on"] = Json::Value(Json::arrayValue); // пусто
    Json::Value cols;
    Json::Value c;
    c["field"] = "x";
    cols.append(c);
    join["columns"] = cols;
    REQUIRE_FALSE(parseJoin(join, out, err));
    REQUIRE(err.find("join.on") != std::string::npos);

    join["type"] = "cross";
    Json::Value k;
    k["left"] = "x";
    k["right"] = "y";
    Json::Value on;
    on.append(k);
    join["on"] = on;
    REQUIRE_FALSE(parseJoin(join, out, err));
    REQUIRE(err.find("inner") != std::string::npos);

    join["type"] = "inner";
    Json::Value cols2;
    Json::Value c2;
    c2["field"] = "x";
    c2["as"] = "x";
    cols2.append(c2);
    Json::Value c3;
    c3["field"] = "y";
    c3["as"] = "x";
    cols2.append(c3);
    join["columns"] = cols2;
    REQUIRE_FALSE(parseJoin(join, out, err));
    REQUIRE(err.find("повторяется") != std::string::npos);
}

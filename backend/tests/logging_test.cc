#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <string>

#include "logging.h"

using rep::log::Action;
using rep::log::Level;

TEST_CASE("log: уровни и действия переводятся в строки") {
    REQUIRE(std::string(rep::log::levelName(Level::Trace)) == "trace");
    REQUIRE(std::string(rep::log::levelName(Level::Debug)) == "debug");
    REQUIRE(std::string(rep::log::levelName(Level::Info)) == "info");
    REQUIRE(std::string(rep::log::levelName(Level::Warning)) == "warning");
    REQUIRE(std::string(rep::log::levelName(Level::Error)) == "error");

    REQUIRE(std::string(rep::log::actionName(Action::OpenReport)) == "open_report");
    REQUIRE(std::string(rep::log::actionName(Action::ChangeUser)) == "change_user");
    REQUIRE(std::string(rep::log::actionName(Action::AccessDenied)) == "access_denied");
    REQUIRE(std::string(rep::log::actionName(Action::RunSubscription)) == "run_subscription");
}

TEST_CASE("log: тип действия — enum, а не свободная строка") {
    // все значения перечисления должны иметь имя: «unknown» означает забытую ветку
    const Action all[] = {
        Action::OpenReport,      Action::ExportReport,   Action::PrintReport,
        Action::Login,           Action::Logout,         Action::CreateUser,
        Action::ChangeUser,      Action::DeleteUser,     Action::ChangeRoles,
        Action::ChangeTemplate,  Action::PublishTemplate, Action::RunSubscription,
        Action::AccessDenied,    Action::ServerStart,    Action::ServerStop,
        Action::SqlSlow,         Action::SqlError,       Action::ConnectionError,
        Action::RestError,       Action::AuditFailed};
    for (Action a : all) {
        REQUIRE(std::string(rep::log::actionName(a)) != "unknown");
    }
}

TEST_CASE("log: JSON события содержит обязательные поля") {
    rep::log::Event e;
    e.level = Level::Warning;
    e.action = Action::ExportReport;
    e.user = "analyst_1";
    e.message = "экспорт отчёта";
    e.context["reportId"] = "sales-crm";
    e.context["rows"] = 550;

    const std::string json = rep::log::formatJson(e, "2026-11-14T10:00:00.123Z");
    Json::CharReaderBuilder b;
    Json::Value root;
    std::string errs;
    std::istringstream in(json);
    REQUIRE(Json::parseFromStream(b, in, &root, &errs));

    REQUIRE(root["ts"].asString() == "2026-11-14T10:00:00.123Z");
    REQUIRE(root["level"].asString() == "warning");
    REQUIRE(root["action"].asString() == "export_report");
    REQUIRE(root["user"].asString() == "analyst_1");
    REQUIRE(root["message"].asString() == "экспорт отчёта");
    REQUIRE(root["context"]["reportId"].asString() == "sales-crm");
    REQUIRE(root["context"]["rows"].asInt() == 550);
}

TEST_CASE("log: пустой пользователь — системное событие") {
    rep::log::Event e;
    e.level = Level::Info;
    e.action = Action::ServerStart;
    e.user = "";
    e.message = "сервер запущен";

    const std::string json = rep::log::formatJson(e, "2026-11-14T10:00:00.000Z");
    Json::CharReaderBuilder b;
    Json::Value root;
    std::string errs;
    std::istringstream in(json);
    REQUIRE(Json::parseFromStream(b, in, &root, &errs));
    REQUIRE(root["user"].asString().empty());
    REQUIRE(root["context"].isObject());
}

TEST_CASE("log: временная метка в ISO-8601 UTC") {
    const std::string ts = rep::log::timestampNow();
    REQUIRE(ts.size() == 24);              // 2026-11-14T10:00:00.123Z
    REQUIRE(ts.back() == 'Z');
    REQUIRE(ts[4] == '-');
    REQUIRE(ts[10] == 'T');
    REQUIRE(ts[19] == '.');
}

TEST_CASE("log: приёмник получает событие с той же меткой") {
    std::string seenTs;
    std::string seenAction;
    rep::log::setSink([&](const rep::log::Event &e, const std::string &ts) {
        seenTs = ts;
        seenAction = rep::log::actionName(e.action);
    });
    rep::log::event(Level::Info, Action::Login, "admin", "вход выполнен");
    rep::log::setSink(nullptr);
    REQUIRE(!seenTs.empty());
    REQUIRE(seenAction == "login");
}

TEST_CASE("log: уровень из настройки и фильтр REP_LOG_LEVEL") {
    REQUIRE(rep::log::parseLevel("trace") == Level::Trace);
    REQUIRE(rep::log::parseLevel("warning") == Level::Warning);
    REQUIRE(rep::log::parseLevel("warn") == Level::Warning);
    REQUIRE(rep::log::parseLevel("error") == Level::Error);
    REQUIRE(rep::log::parseLevel("чепуха") == Level::Info); // неизвестное — info

    int seen = 0;
    rep::log::setSink([&](const rep::log::Event &, const std::string &) { ++seen; });
    rep::log::setMinLevel(Level::Error);
    rep::log::event(Level::Info, Action::OpenReport, "admin", "не должно попасть");
    rep::log::event(Level::Error, Action::SqlError, "", "должно попасть");
    rep::log::setMinLevel(Level::Info);
    rep::log::setSink(nullptr);
    REQUIRE(seen == 1);
}

#include <catch2/catch_test_macros.hpp>

#include <ctime>
#include <string>

#include "smtp.h"
#include "subscriptions.h"

TEST_CASE("base64: векторы RFC 4648") {
    REQUIRE(rep::base64Encode("") == "");
    REQUIRE(rep::base64Encode("f") == "Zg==");
    REQUIRE(rep::base64Encode("fo") == "Zm8=");
    REQUIRE(rep::base64Encode("foo") == "Zm9v");
    REQUIRE(rep::base64Encode("hello") == "aGVsbG8=");
    REQUIRE(rep::base64Encode("Привет") == "0J/RgNC40LLQtdGC");
}

TEST_CASE("mimeHeader: ascii не кодируется, utf-8 кодируется") {
    REQUIRE(rep::mimeHeader("Report") == "Report");
    REQUIRE(rep::mimeHeader("Отчёт по продажам").rfind("=?UTF-8?B?", 0) == 0);
    const std::string encoded = rep::mimeHeader(std::string(120, 'a') + "б");
    REQUIRE(encoded.find("\r\n ") != std::string::npos); // длинный заголовок разбит
}

TEST_CASE("buildMime: заголовки, текст и вложение в base64") {
    rep::MailMessage message;
    message.from = "reports@example.test";
    message.to = "po@example.test";
    message.subject = "Продажи за март";
    message.text = "Во вложении отчёт.";
    message.attachmentName = "sales.csv";
    message.attachmentType = "text/csv";
    message.attachmentData = "a;b\r\n1;2\r\n";

    const std::string mime = rep::buildMime(message);
    REQUIRE(mime.find("From: reports@example.test") != std::string::npos);
    REQUIRE(mime.find("To: po@example.test") != std::string::npos);
    REQUIRE(mime.find("Subject: =?UTF-8?B?") != std::string::npos);
    REQUIRE(mime.find("multipart/mixed; boundary=") != std::string::npos);
    REQUIRE(mime.find("Content-Type: text/csv; name=") != std::string::npos);
    REQUIRE(mime.find("Content-Disposition: attachment; filename=") != std::string::npos);
    REQUIRE(mime.find(rep::base64Encode(message.attachmentData)) != std::string::npos);
    REQUIRE(mime.rfind("--") != std::string::npos);
}

TEST_CASE("buildMime: точка в начале строки удваивается") {
    rep::MailMessage message;
    message.from = "a@b";
    message.to = "c@d";
    message.subject = "s";
    message.text = ".скрытая строка\nобычная";
    const std::string mime = rep::buildMime(message);
    REQUIRE(mime.find("..скрытая строка") != std::string::npos);
}

TEST_CASE("cron: разбор и ошибки") {
    rep::Cron cron;
    std::string err;
    REQUIRE(rep::parseCron("*/5 9-18 * * 1-5", cron, err));
    REQUIRE(cron.minutes[0]);
    REQUIRE(cron.minutes[5]);
    REQUIRE(!cron.minutes[1]);
    REQUIRE(cron.hours[9]);
    REQUIRE(cron.hours[18]);

    REQUIRE(!rep::parseCron("* * * *", cron, err));
    REQUIRE(rep::parseCron("0 0 * * *", cron, err));
    REQUIRE(!rep::parseCron("60 * * * *", cron, err));
    REQUIRE(!rep::parseCron("*/0 * * * *", cron, err));
    REQUIRE(!rep::parseCron("5-1 * * * *", cron, err));
    REQUIRE(!rep::parseCron("a * * * *", cron, err));
}

TEST_CASE("cron: следующий запуск в известные моменты") {
    rep::Cron cron;
    std::string err;
    REQUIRE(rep::parseCron("*/15 * * * *", cron, err));

    std::tm start{};
    start.tm_year = 126; // 2026
    start.tm_mon = 2;    // март
    start.tm_mday = 10;
    start.tm_hour = 9;
    start.tm_min = 7;
    start.tm_sec = 0;
    start.tm_isdst = -1;
    const std::time_t from = std::mktime(&start);

    const std::time_t next = rep::nextRun(cron, from);
    REQUIRE(next != 0);
    const std::tm local = *std::localtime(&next);
    REQUIRE(local.tm_min == 15);
    REQUIRE(local.tm_hour == 9);

    // ежедневное расписание: следующий день, то же время
    rep::Cron daily;
    REQUIRE(rep::parseCron("30 8 * * *", daily, err));
    const std::time_t tomorrow = rep::nextRun(daily, from);
    const std::tm tomorrowLocal = *std::localtime(&tomorrow);
    REQUIRE(tomorrowLocal.tm_mday == 11);
    REQUIRE(tomorrowLocal.tm_hour == 8);
    REQUIRE(tomorrowLocal.tm_min == 30);
}

TEST_CASE("cron: следующее срабатывание в наивном локальном времени") {
    rep::Cron cron;
    std::string err;
    REQUIRE(rep::parseCron("0 9 * * *", cron, err));

    // 2026-03-01 08:00 «местного» времени → 09:00 того же дня
    std::tm base{};
    base.tm_year = 2026 - 1900;
    base.tm_mon = 3 - 1;
    base.tm_mday = 1;
    base.tm_hour = 8;
    base.tm_min = 0;
    const std::time_t naive = ::timegm(&base);
    const std::time_t next = rep::nextRunNaive(cron, naive);
    std::tm out = *std::gmtime(&next);
    REQUIRE(out.tm_hour == 9);
    REQUIRE(out.tm_mday == 1);

    // ровно в момент срабатывания — следующее через сутки, а не «сейчас»
    std::tm atNine = base;
    atNine.tm_hour = 9;
    const std::time_t nextAfter = rep::nextRunNaive(cron, ::timegm(&atNine));
    std::tm out2 = *std::gmtime(&nextAfter);
    REQUIRE(out2.tm_mday == 2);
    REQUIRE(out2.tm_hour == 9);
}

TEST_CASE("cron: будни в наивном времени не попадают на выходные") {
    rep::Cron cron;
    std::string err;
    REQUIRE(rep::parseCron("30 9 * * 1-5", cron, err));
    std::tm base{};
    base.tm_year = 2026 - 1900;
    base.tm_mon = 2 - 1;
    base.tm_mday = 7; // суббота
    base.tm_hour = 10;
    const std::time_t next = rep::nextRunNaive(cron, ::timegm(&base));
    std::tm out = *std::gmtime(&next);
    REQUIRE(out.tm_mday == 9); // понедельник
    REQUIRE(out.tm_hour == 9);
    REQUIRE(out.tm_min == 30);
}

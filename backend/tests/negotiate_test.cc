// Тесты помощников Kerberos без живого KDC (ADR-123): разбор base64 из challenge
// и текст ошибки с подсказкой «kinit». Сам обмен с KDC проверяется скриптом
// scripts/check-kerberos.sh — в CI инфраструктуры Kerberos нет.
#include "catch2/catch_test_macros.hpp"

#include "negotiate.h"

#ifdef REP_HAVE_GSSAPI
#include <gssapi/gssapi.h>
#endif

using rep::base64Decode;
using rep::kerbErrorText;

TEST_CASE("negotiate: base64 с padding", "[negotiate]") {
    REQUIRE(base64Decode("SGVsbG8=") == "Hello");
    REQUIRE(base64Decode("SGVsbG8sIG1pciE=") == "Hello, mir!");
}

TEST_CASE("negotiate: base64 без padding", "[negotiate]") {
    // серверы присылают токен и без '=' — длина должна давать тот же результат
    REQUIRE(base64Decode("SGVsbG8") == "Hello");
    REQUIRE(base64Decode("TWFtYQ") == "Mama");
}

TEST_CASE("negotiate: base64 терпит пробелы и переносы", "[negotiate]") {
    // пробел — разделитель base64, а не данные: «Hello!», не «Hel lo!»
    REQUIRE(base64Decode("SGVs bG8h") == "Hello!");
}

TEST_CASE("negotiate: base64 пропускает мусорные символы", "[negotiate]") {
    // поведение зафиксировано: минус и подчёркивание (base64url) не наши, они отбрасываются
    REQUIRE(base64Decode("SGVs-bG8_") == "Hello");
    REQUIRE(base64Decode("") == "");
    REQUIRE(base64Decode("=") == "");
}

TEST_CASE("negotiate: пустой challenge не ломает разбор", "[negotiate]") {
    REQUIRE(base64Decode(" ").empty());
    REQUIRE(base64Decode("YQ==") == "a");
}

TEST_CASE("negotiate: текст ошибки при отсутствии тикета подсказывает kinit", "[negotiate]") {
#ifdef REP_HAVE_GSSAPI
    const std::string text = kerbErrorText(GSS_S_NO_CRED, 0);
    REQUIRE(text.find("kinit") != std::string::npos);
    REQUIRE(text.find("нет тикета") != std::string::npos);
#else
    REQUIRE(kerbErrorText(0, 0).find("без GSSAPI") != std::string::npos);
#endif
}

TEST_CASE("negotiate: текст ошибки при истёкшем тикете говорит «истёк»", "[negotiate]") {
#ifdef REP_HAVE_GSSAPI
    const std::string text = kerbErrorText(GSS_S_CREDENTIALS_EXPIRED, 0);
    REQUIRE(text.find("kinit") != std::string::npos);
    REQUIRE(text.find("истёк") != std::string::npos);
#else
    REQUIRE(kerbErrorText(0, 0).find("без GSSAPI") != std::string::npos);
#endif
}

TEST_CASE("negotiate: прочие ошибки GSSAPI не получают подсказку", "[negotiate]") {
#ifdef REP_HAVE_GSSAPI
    const std::string text = kerbErrorText(GSS_S_BAD_NAME, 0);
    REQUIRE(text.find("kinit") == std::string::npos);
#else
    REQUIRE(kerbErrorText(0, 0).find("без GSSAPI") != std::string::npos);
#endif
}

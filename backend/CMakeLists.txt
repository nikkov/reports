cmake_minimum_required(VERSION 3.20)

# Версия приложения — из файла VERSION в корне репозитория (единая точка).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" REP_VERSION)
string(STRIP "${REP_VERSION}" REP_VERSION)
# VERSION участвует в компиляции (REP_VERSION), поэтому изменение файла должно
# само запускать configure: иначе `cmake --build` оставлял в бинарнике старую версию.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION")

project(rep VERSION ${REP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Drogon CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)
find_package(libxlsxwriter CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# Ядро без HTTP-слоя: тестируется Catch2 отдельно от сервера.
add_library(rep_core STATIC
  src/aggregate.cc
  src/auth.cc
  src/base_path.cc
  src/connections.cc
  src/csv_export.cc
  src/dataset.cc
  src/join.cc
  src/logging.cc
  src/rest_client.cc
  src/smtp.cc
  src/subscriptions.cc
  src/template_migrate.cc
  src/validate.cc
  src/dates_expr.cc
  src/env.cc
  src/net.cc
  src/params.cc
  src/queries.cc
  src/static_web.cc
)
target_include_directories(rep_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rep_core PUBLIC Drogon::Drogon OpenSSL::SSL OpenSSL::Crypto)

# Платформенные различия собраны в src/net.*: остальной код их не знает.
if(WIN32)
  # winsock: сокеты и разрешение имён
  target_link_libraries(rep_core PUBLIC ws2_32)
  # windows.h не должен тянуть winsock.h и ломать std::min/std::max
  target_compile_definitions(rep_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
endif()

if(MSVC)
  # /utf-8 обязателен: в строках русский текст, иначе MSVC читает файлы в CP1251
  # и получает мусор в логах, JSON-ответах и сообщениях валидатора.
  add_compile_options(/utf-8 /W3 /permissive- /Zc:__cplusplus /EHsc /bigobj)
  # CRT как MD/MDd: conan-пакеты (drogon, openssl, libpq) собраны с этим рантаймом
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(rep src/main.cc src/xlsx_export.cc src/group_rows.cc)
target_compile_definitions(rep PRIVATE REP_VERSION="${REP_VERSION}")
target_link_libraries(rep PRIVATE rep_core libxlsxwriter::libxlsxwriter)

enable_testing()
add_executable(rep_tests
  src/xlsx_export.cc
  src/group_rows.cc
  tests/aggregate_test.cc
  tests/csv_test.cc
  tests/xlsx_test.cc
  tests/xlsx_group_test.cc
  tests/group_rows_test.cc
  tests/template_migrate_test.cc
  tests/auth_test.cc
  tests/base_path_test.cc
  tests/connections_test.cc
  tests/static_web_test.cc
  tests/dataset_test.cc
  tests/join_test.cc
  tests/logging_test.cc
  tests/rest_client_test.cc
  tests/dates_expr_test.cc
  tests/queries_test.cc
  tests/params_test.cc
  tests/validate_test.cc
  tests/smtp_cron_test.cc
)
target_link_libraries(rep_tests PRIVATE rep_core Catch2::Catch2WithMain libxlsxwriter::libxlsxwriter)
# фикстуры parity лежат в репозитории: тест читает их по этому пути
# На Windows пути приходят с обратными слэшами, а это литерал C++: обратный слэш
# перед U даёт неверный escape. Приводим к прямым слэшам — строка собирается
# одинаково на обеих системах.
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/.." REP_SOURCE_DIR_POSIX)
target_compile_definitions(rep_tests PRIVATE REP_SOURCE_DIR="${REP_SOURCE_DIR_POSIX}")
add_test(NAME rep_tests COMMAND rep_tests)

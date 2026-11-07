cmake_minimum_required(VERSION 3.20)
project(rep CXX)

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
  src/csv_export.cc
  src/dataset.cc
  src/smtp.cc
  src/subscriptions.cc
  src/template_migrate.cc
  src/validate.cc
  src/env.cc
  src/params.cc
  src/queries.cc
)
target_include_directories(rep_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rep_core PUBLIC Drogon::Drogon OpenSSL::SSL OpenSSL::Crypto)

add_executable(rep src/main.cc src/xlsx_export.cc)
target_link_libraries(rep PRIVATE rep_core libxlsxwriter::libxlsxwriter)

enable_testing()
add_executable(rep_tests
  src/xlsx_export.cc
  tests/aggregate_test.cc
  tests/csv_test.cc
  tests/xlsx_test.cc
  tests/xlsx_group_test.cc
  tests/template_migrate_test.cc
  tests/auth_test.cc
  tests/dataset_test.cc
  tests/queries_test.cc
  tests/params_test.cc
  tests/validate_test.cc
  tests/smtp_cron_test.cc
)
target_link_libraries(rep_tests PRIVATE rep_core Catch2::Catch2WithMain libxlsxwriter::libxlsxwriter)
# фикстуры parity лежат в репозитории: тест читает их по этому пути
target_compile_definitions(rep_tests PRIVATE REP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME rep_tests COMMAND rep_tests)

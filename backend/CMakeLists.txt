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

# Ядро без HTTP-слоя: тестируется Catch2 отдельно от сервера.
add_library(rep_core STATIC
  src/env.cc
  src/params.cc
  src/queries.cc
)
target_include_directories(rep_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rep_core PUBLIC Drogon::Drogon)

add_executable(rep src/main.cc)
target_link_libraries(rep PRIVATE rep_core)

enable_testing()
add_executable(rep_tests
  tests/queries_test.cc
  tests/params_test.cc
)
target_link_libraries(rep_tests PRIVATE rep_core Catch2::Catch2WithMain)
add_test(NAME rep_tests COMMAND rep_tests)

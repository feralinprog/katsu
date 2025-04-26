cmake_minimum_required(VERSION 3.15...4.0)

project(Katsu VERSION 0.0
              DESCRIPTION "Katsu Language"
              LANGUAGES CXX)

add_executable(katsu src/main.cc)
target_link_libraries(katsu PUBLIC libkatsu)

add_library(libkatsu STATIC src/katsu.cc src/katsu.h)

Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.4.0 # or a later release
)

FetchContent_MakeAvailable(Catch2)

add_executable(test src/katsu_test.cc)
target_link_libraries(test PRIVATE Catch2::Catch2WithMain)
target_link_libraries(test PRIVATE libkatsu)
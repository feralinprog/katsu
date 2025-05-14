cmake_minimum_required(VERSION 3.15...4.0)

add_compile_options(-fsanitize=address -Wall -Wno-unused-function -Werror)
add_link_options(-fsanitize=address)

project(Katsu
  VERSION 0.0
  DESCRIPTION "Katsu Language"
  LANGUAGES CXX
)

# Single library (libkatsudon.a) available for static linking
# for both a main executable and tests.
add_library(katsudon STATIC
  src/span.cc
  src/token.cc
  src/lexer.cc
  src/ast.cc
  src/parser.cc
  src/gc.cc
  src/vm.cc
  src/katsu.cc
)
target_include_directories(katsudon PUBLIC src/)

# Main katsu executable.
add_executable(katsu src/main.cc)
target_link_libraries(katsu PUBLIC katsudon)

# Unit tests, using the Catch2 framework.
Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)

FetchContent_MakeAvailable(Catch2)

# Unit tests.
add_executable(test
  src/lexer_test.cc
  src/value_test.cc
  src/gc_test.cc
  src/vm_test.cc
  src/katsu_test.cc
)
target_link_libraries(test PRIVATE
  Catch2::Catch2WithMain
  katsudon
)

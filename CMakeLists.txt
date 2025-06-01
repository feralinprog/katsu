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
  src/value_utils.cc
  src/compile.cc
  src/vm.cc
  src/builtin.cc
  src/katsu.cc
)
target_include_directories(katsudon PUBLIC src/)
# TODO: hook this up to ./run somehow... need to able to run tests both with and without DEBUG_GC_COLLECT_EVERY_ALLOC.
target_compile_options(katsudon PUBLIC
  # -DDEBUG_ASSERTIONS=0
  # -DDEBUG_GC_LOG
  # -DDEBUG_GC_FILL=0
  -DDEBUG_GC_COLLECT_EVERY_ALLOC
  -DDEBUG_GC_NEW_SEMISPACE
  # -DDEBUG_GC_VERIFY_ROOT_ORDERING=0
)

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
  src/value_utils_test.cc
  src/vm_test.cc
  src/katsu_test.cc
)
target_link_libraries(test PRIVATE
  Catch2::Catch2WithMain
  katsudon
)

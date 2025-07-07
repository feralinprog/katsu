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
  vm/span.cc
  vm/token.cc
  vm/lexer.cc
  vm/ast.cc
  vm/parser.cc
  vm/gc.cc
  vm/value_utils.cc
  vm/compile.cc
  vm/vm.cc
  vm/builtin.cc
  vm/builtin_ffi.cc
  vm/katsu.cc
)
target_include_directories(katsudon PUBLIC vm/)
# TODO: hook this up to ./run somehow... need to able to run tests both with and without DEBUG_GC_COLLECT_EVERY_ALLOC.
target_compile_options(katsudon PUBLIC
  # -DDEBUG_ASSERTIONS=0
  # -DDEBUG_GC_LOG
  # -DDEBUG_GC_FILL=0
  # -DDEBUG_GC_COLLECT_EVERY_ALLOC
  -DDEBUG_GC_NEW_SEMISPACE
  # -DDEBUG_GC_VERIFY_ROOT_ORDERING=0
)
# For a C foreign function interface:
target_link_libraries(katsudon PUBLIC ffi)

# Main katsu executable.
add_executable(katsu vm/main.cc)
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
  vm/lexer_test.cc
  vm/value_test.cc
  vm/gc_test.cc
  vm/value_utils_test.cc
  vm/vm_test.cc
  vm/katsu_test.cc
)
target_link_libraries(test PRIVATE
  Catch2::Catch2WithMain
  katsudon
)

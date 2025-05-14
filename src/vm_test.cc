#include <catch2/catch_test_macros.hpp>

#include "vm.h"

using namespace Katsu;

TEST_CASE("VM smoketest", "[vm]")
{
    GC gc(1024 * 1024);
    VM vm(gc, 10 * 1024);
    // Let destructors run.
}

TEST_CASE("VM executes basic bytecode (no invocations)", "[vm]")
{
    GC gc(1024 * 1024);
    VM vm(gc, 10 * 1024);
    // TODO
}

TEST_CASE("VM executes a native invocation", "[vm]")
{
    GC gc(1024 * 1024);
    VM vm(gc, 10 * 1024);
    // TODO
}

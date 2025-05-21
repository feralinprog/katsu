#include "gc.h"
#include <catch2/catch_test_macros.hpp>

using namespace Katsu;

TEST_CASE("align_up", "[gc]")
{
    CHECK(align_up(0, 0) == 0);
    CHECK(align_up(1, 0) == 1);
    CHECK(align_up(2, 0) == 2);

    CHECK(align_up(0, 1) == 0);
    CHECK(align_up(1, 1) == 2);
    CHECK(align_up(2, 1) == 2);
    CHECK(align_up(3, 1) == 4);
    CHECK(align_up(4, 1) == 4);
    CHECK(align_up(5, 1) == 6);
    CHECK(align_up(6, 1) == 6);

    CHECK(align_up(0, 2) == 0);
    CHECK(align_up(1, 2) == 4);
    CHECK(align_up(2, 2) == 4);
    CHECK(align_up(3, 2) == 4);
    CHECK(align_up(4, 2) == 4);
    CHECK(align_up(5, 2) == 8);
    CHECK(align_up(6, 2) == 8);
    CHECK(align_up(7, 2) == 8);
    CHECK(align_up(8, 2) == 8);
    CHECK(align_up(9, 2) == 12);
    CHECK(align_up(10, 2) == 12);
    CHECK(align_up(11, 2) == 12);
    CHECK(align_up(12, 2) == 12);
}

namespace Katsu
{
    uint8_t* TESTONLY_get_mem(GC& gc)
    {
        return gc.mem;
    }
}

TEST_CASE("walk GC through simple allocations, collection, and OOM", "[gc]")
{
    // The test verifies that the GC collects when expected -- when not collecting on every alloc.
    if (DEBUG_GC_COLLECT_EVERY_ALLOC) {
        SKIP();
    }

    GC gc(13 * sizeof(Value));

    Tuple* a = gc.alloc<Tuple>(4);
    REQUIRE(reinterpret_cast<uint8_t*>(a) == TESTONLY_get_mem(gc) + 0);
    Vector* b = gc.alloc<Vector>(4);
    REQUIRE(reinterpret_cast<uint8_t*>(b) == TESTONLY_get_mem(gc) + 48);

    // Add only `b` to the roots.
    b->capacity = 4;
    b->length = 4;
    b->components()[0] = Value::_float(1.0);
    b->components()[1] = Value::_float(2.0);
    b->components()[2] = Value::_float(3.0);
    b->components()[3] = Value::_float(4.0);
    Root root_b(gc, Value::object(b));

    String* c;
    REQUIRE_NOTHROW(c = gc.alloc<String>(sizeof(String) + 1));
    REQUIRE(reinterpret_cast<uint8_t*>(root_b.get().object()) == TESTONLY_get_mem(gc) + 0);
    REQUIRE(reinterpret_cast<uint8_t*>(c) == TESTONLY_get_mem(gc) + 56);

    // Keep `c` around via another root.
    c->length = 1;
    Root root_c(gc, Value::object(c));

    REQUIRE_THROWS_AS(
        gc._alloc_raw(12 * sizeof(Value) - sizeof(Object) - 5 * sizeof(Value) - sizeof(String)),
        std::bad_alloc);
}

// TODO: test that GC actually follows every internal reference of each object type
// TODO: test Root more (e.g. move semantics, destructor)

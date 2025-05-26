#include <catch2/catch_test_macros.hpp>

#include "value_utils.h"

using namespace Katsu;

TEST_CASE("make_ref - roots", "[value-utils]")
{
    GC gc(1024 * 1024);

    String* string = make_string(gc, "pointee");
    Root r_string(gc, Value::object(string));
    Ref* ref = make_ref(gc, r_string);
    CHECK(string_eq(ref->v_ref.obj_string(), "pointee"));
}

TEST_CASE("make_ref - values", "[value-utils]")
{
    GC gc(1024 * 1024);

    String* string = make_string(gc, "pointee");
    Ref* ref = make_ref(gc, Value::object(string));
    CHECK(string_eq(ref->v_ref.obj_string(), "pointee"));
}

// TODO: test the rest

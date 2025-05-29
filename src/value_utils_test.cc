#include <catch2/catch_test_macros.hpp>

#include "value_utils.h"

using namespace Katsu;

TEST_CASE("make_ref", "[value-utils]")
{
    GC gc(1024 * 1024);

    ValueRoot r_string(gc, Value::object(make_string(gc, "pointee")));
    Ref* ref = make_ref(gc, r_string);
    CHECK(string_eq(ref->v_ref.obj_string(), "pointee"));
}

TEST_CASE("vector append", "[value-utils]")
{
    GC gc(1024 * 1024);

    ValueRoot r_value_0(gc, Value::object(make_string(gc, "value 0")));
    ValueRoot r_value_1(gc, Value::object(make_string(gc, "value 1")));
    ValueRoot r_value_2(gc, Value::object(make_string(gc, "value 2")));

    Root<Vector> r_vector(gc, make_vector(gc, /* capacity */ 0));
    CHECK(r_vector->capacity() == 0);
    CHECK(r_vector->length == 0);

    append(gc, r_vector, r_value_0);
    CHECK(r_vector->capacity() == 1);
    CHECK(r_vector->length == 1);
    CHECK(r_vector->v_array.obj_array()->components()[0] == *r_value_0);

    append(gc, r_vector, r_value_1);
    CHECK(r_vector->capacity() == 2);
    CHECK(r_vector->length == 2);
    CHECK(r_vector->v_array.obj_array()->components()[0] == *r_value_0);
    CHECK(r_vector->v_array.obj_array()->components()[1] == *r_value_1);

    append(gc, r_vector, r_value_2);
    CHECK(r_vector->capacity() == 4);
    CHECK(r_vector->length == 3);
    CHECK(r_vector->v_array.obj_array()->components()[0] == *r_value_0);
    CHECK(r_vector->v_array.obj_array()->components()[1] == *r_value_1);
    CHECK(r_vector->v_array.obj_array()->components()[2] == *r_value_2);
    CHECK(r_vector->v_array.obj_array()->components()[3] == Value::null());
}

TEST_CASE("module append", "[value-utils]")
{
    GC gc(1024 * 1024);

    Root<String> r_key(gc, make_string(gc, "key"));
    ValueRoot r_value(gc, Value::object(make_string(gc, "value")));

    OptionalRoot<Module> r_base(gc, nullptr);
    Root<Module> r_module(gc, make_module(gc, r_base, /* capacity */ 0));
    CHECK(module_lookup(*r_module, *r_key) == nullptr);

    append(gc, r_module, r_key, r_value);
    Value* lookup = module_lookup(*r_module, *r_key);
    REQUIRE(lookup != nullptr);
    CHECK(*lookup == *r_value);
}

// TODO: test the rest

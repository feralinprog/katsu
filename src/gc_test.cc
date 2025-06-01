#include <catch2/catch_test_macros.hpp>

#include "gc.h"

#include "value_utils.h"
#include <sstream>

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

    GC gc(12 * sizeof(Value));

    Tuple* a = gc.alloc<Tuple>(4);
    REQUIRE(reinterpret_cast<uint8_t*>(a) == TESTONLY_get_mem(gc) + 0);
    Array* b = gc.alloc<Array>(4);
    REQUIRE(reinterpret_cast<uint8_t*>(b) == TESTONLY_get_mem(gc) + 48);

    // Add only `b` to the roots.
    b->length = 4;
    b->components()[0] = Value::_float(1.0);
    b->components()[1] = Value::_float(2.0);
    b->components()[2] = Value::_float(3.0);
    b->components()[3] = Value::_float(4.0);
    Root<Array> root_b(gc, std::move(b));

    String* c;
    REQUIRE_NOTHROW(c = gc.alloc<String>(sizeof(String) + 1));
    REQUIRE(reinterpret_cast<uint8_t*>(root_b.value().object()) == TESTONLY_get_mem(gc) + 0);
    REQUIRE(reinterpret_cast<uint8_t*>(c) == TESTONLY_get_mem(gc) + 48);

    // Keep `c` around via another root.
    c->length = 1;
    Root<String> root_c(gc, std::move(c));

    REQUIRE_THROWS_AS(
        gc._alloc_raw(12 * sizeof(Value) - sizeof(Object) - 5 * sizeof(Value) - sizeof(String)),
        std::bad_alloc);
}

TEST_CASE("GC follows internal references", "[gc]")
{
    GC gc(10 * 1024);

    // Arbitrary, just needs to be at least as many as any SECTION needs.
    const size_t NUM_POINTEES = 10;
    String* pointees[NUM_POINTEES];
    Value v_pointees[NUM_POINTEES];
    for (size_t i = 0; i < NUM_POINTEES; i++) {
        std::stringstream ss;
        ss << "pointee " << i;
        pointees[i] = make_string(gc, ss.str());
        v_pointees[i] = Value::object(pointees[i]);
        gc.roots.push_back(&v_pointees[i]);
    }

    auto single_root_collect = [&gc](Value* root) {
        gc.roots.clear();
        gc.roots.push_back(root);
        gc.collect();
    };

    auto CHECK_POINTEE = [](size_t i, Value pointee) {
        std::stringstream ss;
        ss << "pointee " << i;

        String* string;
        REQUIRE_NOTHROW(string = pointee.obj_string());
        CHECK(string_eq(string, ss.str()));
    };

    SECTION("Ref")
    {
        // Set up object.
        Ref* obj = gc.alloc<Ref>();
        obj->v_ref = v_pointees[0];

        Value v_obj = Value::object(obj);
        single_root_collect(&v_obj);

        // Unpack and verify object.
        REQUIRE_NOTHROW(obj = v_obj.obj_ref());
        CHECK_POINTEE(0, obj->v_ref);
    }

    SECTION("Tuple")
    {
        // Set up object.
        Tuple* obj = gc.alloc<Tuple>(2);
        obj->length = 2;
        obj->components()[0] = v_pointees[0];
        obj->components()[1] = v_pointees[1];

        Value v_obj = Value::object(obj);
        single_root_collect(&v_obj);

        // Unpack and verify object.
        REQUIRE_NOTHROW(obj = v_obj.obj_tuple());
        CHECK(obj->length == 2);
        CHECK_POINTEE(0, obj->components()[0]);
        CHECK_POINTEE(1, obj->components()[1]);
    }

    SECTION("Array")
    {
        // Set up object.
        Array* obj = gc.alloc<Array>(2);
        obj->length = 2;
        obj->components()[0] = v_pointees[0];
        obj->components()[1] = v_pointees[1];

        Value v_obj = Value::object(obj);
        single_root_collect(&v_obj);

        // Unpack and verify object.
        REQUIRE_NOTHROW(obj = v_obj.obj_array());
        CHECK(obj->length == 2);
        CHECK_POINTEE(0, obj->components()[0]);
        CHECK_POINTEE(1, obj->components()[1]);
    }

    SECTION("Vector")
    {
        // Set up object.
        Vector* obj = gc.alloc<Vector>();
        obj->length = 0x12345678; // not actually used by the GC
        obj->v_array = v_pointees[0];

        Value v_obj = Value::object(obj);
        single_root_collect(&v_obj);

        // Unpack and verify object.
        REQUIRE_NOTHROW(obj = v_obj.obj_vector());
        CHECK(obj->length == 0x12345678);
        CHECK_POINTEE(0, obj->v_array);
    }

    SECTION("Module")
    {
        // Set up object.
        Module* obj = gc.alloc<Module>();
        obj->v_base = v_pointees[0];
        obj->length = 0x12345678; // not actually used by the GC
        obj->v_array = v_pointees[1];

        Value v_obj = Value::object(obj);
        single_root_collect(&v_obj);

        // Unpack and verify object.
        REQUIRE_NOTHROW(obj = v_obj.obj_module());
        CHECK_POINTEE(0, obj->v_base);
        CHECK(obj->length == 0x12345678);
        CHECK_POINTEE(1, obj->v_array);
    }

    // Kind of already tested in other sections implicitly...
    SECTION("String")
    {
        // Set up object.
        std::string expected = "expected string value";
        String* obj = gc.alloc<String>(expected.size());
        obj->length = expected.size();
        memcpy(obj->contents(), expected.c_str(), expected.size());

        Value v_obj = Value::object(obj);
        single_root_collect(&v_obj);

        // Unpack and verify object.
        REQUIRE_NOTHROW(obj = v_obj.obj_string());
        CHECK(obj->length == expected.size());
        CHECK(memcmp(obj->contents(), expected.c_str(), expected.size()) == 0);
    }

    SECTION("Code")
    {
        // Set up object.
        Code* obj = gc.alloc<Code>();
        obj->v_module = v_pointees[0];
        obj->num_regs = 0x12345678; // not used by the GC
        obj->num_data = 0xAABBCCDD; // not used by the GC
        obj->v_upreg_map = v_pointees[1];
        obj->v_insts = v_pointees[2];
        obj->v_args = v_pointees[3];

        Value v_obj = Value::object(obj);
        single_root_collect(&v_obj);

        // Unpack and verify object.
        REQUIRE_NOTHROW(obj = v_obj.obj_code());
        CHECK_POINTEE(0, obj->v_module);
        CHECK(obj->num_regs == 0x12345678);
        CHECK(obj->num_data == 0xAABBCCDD);
        CHECK_POINTEE(1, obj->v_upreg_map);
        CHECK_POINTEE(2, obj->v_insts);
        CHECK_POINTEE(3, obj->v_args);
    }

    SECTION("Closure")
    {
        // Set up object.
        Closure* obj = gc.alloc<Closure>();
        obj->v_code = v_pointees[0];
        obj->v_upregs = v_pointees[1];

        Value v_obj = Value::object(obj);
        single_root_collect(&v_obj);

        // Unpack and verify object.
        REQUIRE_NOTHROW(obj = v_obj.obj_closure());
        CHECK_POINTEE(0, obj->v_code);
        CHECK_POINTEE(1, obj->v_upregs);
    }

    SECTION("Method")
    {
        // Set up object.
        Method* obj = gc.alloc<Method>();
        obj->v_param_matchers = v_pointees[0];
        obj->v_return_type = v_pointees[1];
        obj->v_code = v_pointees[2];
        obj->v_attributes = v_pointees[3];
        obj->native_handler = reinterpret_cast<NativeHandler>(0x12345678);       // not used by GC
        obj->intrinsic_handler = reinterpret_cast<IntrinsicHandler>(0x87654321); // not used by GC

        Value v_obj = Value::object(obj);
        single_root_collect(&v_obj);

        // Unpack and verify object.
        REQUIRE_NOTHROW(obj = v_obj.obj_method());
        CHECK_POINTEE(0, obj->v_param_matchers);
        CHECK_POINTEE(1, obj->v_return_type);
        CHECK_POINTEE(2, obj->v_code);
        CHECK_POINTEE(3, obj->v_attributes);
        CHECK(reinterpret_cast<uint64_t>(obj->native_handler) == 0x12345678);
        CHECK(reinterpret_cast<uint64_t>(obj->intrinsic_handler) == 0x87654321);
    }

    SECTION("MultiMethod")
    {
        // Set up object.
        MultiMethod* obj = gc.alloc<MultiMethod>();
        obj->v_name = v_pointees[0];
        obj->v_methods = v_pointees[1];
        obj->v_attributes = v_pointees[2];

        Value v_obj = Value::object(obj);
        single_root_collect(&v_obj);

        // Unpack and verify object.
        REQUIRE_NOTHROW(obj = v_obj.obj_multimethod());
        CHECK_POINTEE(0, obj->v_name);
        CHECK_POINTEE(1, obj->v_methods);
        CHECK_POINTEE(2, obj->v_attributes);
    }

    SECTION("Type")
    {
        // Set up object.
        Type* obj = gc.alloc<Type>();
        obj->v_name = v_pointees[0];
        obj->v_bases = v_pointees[1];
        obj->sealed = true; // not used by GC
        obj->v_linearization = v_pointees[2];
        obj->v_subtypes = v_pointees[3];
        obj->kind = Type::Kind::DATACLASS; // not used by GC
        obj->v_slots = v_pointees[4];

        Value v_obj = Value::object(obj);
        single_root_collect(&v_obj);

        // Unpack and verify object.
        REQUIRE_NOTHROW(obj = v_obj.obj_type());
        CHECK_POINTEE(0, obj->v_name);
        CHECK_POINTEE(1, obj->v_bases);
        CHECK(obj->sealed == true);
        CHECK_POINTEE(2, obj->v_linearization);
        CHECK_POINTEE(3, obj->v_subtypes);
        CHECK(obj->kind == Type::Kind::DATACLASS);
        CHECK_POINTEE(4, obj->v_slots);
    }

    SECTION("DataclassInstance")
    {
        // Set up object. (There's more setup to do here than in other SECTIONS, since dataclass
        // instances have extra complexity to determine number of slots; it's not directly stored
        // inline.)
        Vector* slots = gc.alloc<Vector>();
        // The actual backing array doesn't matter; just use a stand-in. The GC just cares about
        // slot count.
        slots->length = 2;
        slots->v_array = v_pointees[0];
        Value v_slots = Value::object(slots);
        gc.roots.push_back(&v_slots);

        Type* type = gc.alloc<Type>();
        type->v_name = v_pointees[1];
        type->v_bases = v_pointees[2];
        type->sealed = true; // not used by GC
        type->v_linearization = v_pointees[3];
        type->v_subtypes = v_pointees[4];
        type->kind = Type::Kind::DATACLASS; // not used by GC
        type->v_slots = v_slots;
        Value v_type = Value::object(type);
        gc.roots.push_back(&v_type);

        DataclassInstance* obj = gc.alloc<DataclassInstance>(/* num_slots */ 2);
        obj->v_type = v_type;
        REQUIRE(obj->num_slots() == 2);
        obj->slots()[0] = v_pointees[5];
        obj->slots()[1] = v_pointees[6];

        Value v_obj = Value::object(obj);
        single_root_collect(&v_obj);

        // Unpack and verify object.
        REQUIRE_NOTHROW(obj = v_obj.obj_instance());
        REQUIRE(obj->num_slots() == 2);
        // Check the type (at least, any Values within).
        {
            REQUIRE_NOTHROW(type = obj->v_type.obj_type());
            CHECK_POINTEE(1, type->v_name);
            CHECK_POINTEE(2, type->v_bases);
            CHECK_POINTEE(3, type->v_linearization);
            CHECK_POINTEE(4, type->v_subtypes);
            REQUIRE_NOTHROW(slots = type->v_slots.obj_vector());
            REQUIRE(slots->length == 2);
            CHECK_POINTEE(0, slots->v_array);
        }
        CHECK_POINTEE(5, obj->slots()[0]);
        CHECK_POINTEE(6, obj->slots()[1]);
    }
}

// TODO: test ValueRoot more (e.g. move semantics, destructor)
// TODO: test Root<T> more
// TODO: test OptionalRoot<T> more

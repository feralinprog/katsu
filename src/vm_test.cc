#include <catch2/catch_test_macros.hpp>

#include "vm.h"

#include "value_utils.h"
#include <cstring>

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

    // Perform a simple LOAD_VALUE.

    Module* module = make_module(gc, /* base */ nullptr, /* capacity */ 0);
    Root r_module(gc, Value::object(module));

    Root r_upreg_map(gc, Value::null());

    Array* insts = make_array_nofill(gc, /* length */ 1);
    insts->components()[0] = Value::fixnum(OpCode::LOAD_VALUE);
    Root r_insts(gc, Value::object(insts));

    Array* args = make_array(gc, /* length */ 1);
    args->components()[0] = Value::fixnum(1234);
    Root r_args(gc, Value::object(args));

    Code* code = make_code(gc,
                           /* r_module */ r_module,
                           /* num_regs */ 1,
                           /* num_data */ 1,
                           /* r_upreg_map */ r_upreg_map,
                           /* r_insts */ r_insts,
                           /* r_args */ r_args);
    Root r_code(gc, Value::object(code));

    Value v_result = vm.eval_toplevel(Value::object(code));
    CHECK(v_result == Value::fixnum(1234));

    // Evaluate again -- VM should be able to handle this easily.
    v_result = vm.eval_toplevel(Value::object(code));
    CHECK(v_result == Value::fixnum(1234));
}

Value test__fixnum_add(VM& vm, int64_t num_args, Value* args)
{
    REQUIRE(num_args == 2);
    REQUIRE(args[0].is_fixnum());
    REQUIRE(args[1].is_fixnum());
    return Value::fixnum(args[0].fixnum() + args[1].fixnum());
}

TEST_CASE("VM executes a native invocation", "[vm]")
{
    GC gc(1024 * 1024);
    VM vm(gc, 10 * 1024);

    // Perform an INVOKE op to add two fixnums.

    String* method_name = make_string(gc, "+:");
    Root r_method_name(gc, Value::object(method_name));

    Root r_param_matchers(gc, Value::null()); // TODO: not null

    Root r_return_type(gc, Value::null());

    Root r_method_code(gc, Value::null()); // native!

    Vector* method_attributes = make_vector(gc, /* capacity */ 0);
    Root r_method_attributes(gc, Value::object(method_attributes));

    Method* method = make_method(gc,
                                 /* r_param_matchers */ r_param_matchers,
                                 /* r_return_type */ r_return_type,
                                 /* r_code */ r_method_code,
                                 /* r_attributes */ r_method_attributes,
                                 /* native_handler */ &test__fixnum_add);
    Root r_method(gc, Value::object(method));

    Vector* methods = make_vector(gc, /* capacity */ 1);
    methods->length = 1;
    {
        Array* array = methods->v_array.obj_array();
        array->components()[0] = r_method.get();
    }
    Root r_methods(gc, Value::object(methods));

    Vector* multimethod_attributes = make_vector(gc, /* capacity */ 0);
    Root r_multimethod_attributes(gc, Value::object(multimethod_attributes));

    MultiMethod* multimethod = make_multimethod(gc,
                                                /* r_name */ r_method_name,
                                                /* r_methods */ r_methods,
                                                /* r_attributes */ r_multimethod_attributes);
    Root r_multimethod(gc, Value::object(multimethod));

    Module* module = make_module(gc, /* base */ nullptr, /* capacity */ 1);
    Root r_module(gc, Value::object(module));
    module->length = 1;
    module->entries()[0].v_key = r_method_name.get();
    module->entries()[0].v_value = r_multimethod.get();

    Root r_upreg_map(gc, Value::null());

    Array* insts = make_array(gc, /* length */ 3);
    insts->components()[0] = Value::fixnum(OpCode::LOAD_VALUE);
    insts->components()[1] = Value::fixnum(OpCode::LOAD_VALUE);
    insts->components()[2] = Value::fixnum(OpCode::INVOKE);
    Root r_insts(gc, Value::object(insts));

    Array* args = make_array(gc, /* length */ 4);
    // LOAD_VALUE: 5
    args->components()[0] = Value::fixnum(5);
    // LOAD_VALUE: 10
    args->components()[1] = Value::fixnum(10);
    // INVOKE: +: with two args
    args->components()[2] = r_method_name.get();
    args->components()[3] = Value::fixnum(2);
    Root r_args(gc, Value::object(args));

    Code* code = make_code(gc,
                           /* r_module */ r_module,
                           /* num_regs */ 1,
                           /* num_data */ 1,
                           /* r_upreg_map */ r_upreg_map,
                           /* r_insts */ r_insts,
                           /* r_args */ r_args);
    Root r_code(gc, Value::object(code));

    Value v_result = vm.eval_toplevel(Value::object(code));
    CHECK(v_result == Value::fixnum(15));
}

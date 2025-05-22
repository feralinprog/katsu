#include <catch2/catch_test_macros.hpp>

#include "value_utils.h"
#include "vm.h"
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

    Module* module = make_module(gc, /* v_base */ Value::null(), /* capacity */ 0);
    Root r_module(gc, Value::object(module));

    Vector* insts = make_vector(gc, /* capacity */ 1, /* length */ 1);
    Root r_insts(gc, Value::object(insts));
    insts->components()[0] = Value::fixnum(OpCode::LOAD_VALUE);

    Vector* args = make_vector(gc, /* capacity */ 1, /* length */ 1);
    Root r_args(gc, Value::object(args));
    args->components()[0] = Value::fixnum(1234);

    Code* code = make_code(gc,
                           /* v_module */ r_module.get(),
                           /* num_regs */ 1,
                           /* num_data */ 1,
                           /* v_upreg_map */ Value::null(),
                           /* v_insts */ r_insts.get(),
                           /* v_args */ r_args.get());
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

    Vector* method_attributes = make_vector(gc, /* capacity */ 0, /* length */ 0);
    Root r_method_attributes(gc, Value::object(method_attributes));

    Method* method = make_method(gc,
                                 /* v_param_matchers */ Value::null(), // TODO
                                 /* v_return_type */ Value::null(),
                                 /* v_code */ Value::null(), // native!
                                 /* v_attributes */ r_method_attributes.get(),
                                 /* native_handler */ &test__fixnum_add);
    Root r_method(gc, Value::object(method));

    Vector* methods = make_vector(gc, /* capacity */ 1, /* length */ 1);
    Root r_methods(gc, Value::object(methods));
    methods->components()[0] = r_method.get();

    Vector* multimethod_attributes = make_vector(gc, /* capacity */ 0, /* length */ 0);
    Root r_multimethod_attributes(gc, Value::object(multimethod_attributes));

    MultiMethod* multimethod = make_multimethod(gc,
                                                /* v_name */ r_method_name.get(),
                                                /* v_methods */ r_methods.get(),
                                                /* v_attributes */ r_multimethod_attributes.get());
    Root r_multimethod(gc, Value::object(multimethod));

    Module* module = make_module(gc, /* v_base */ Value::null(), /* capacity */ 1);
    Root r_module(gc, Value::object(module));
    module->length = 1;
    module->entries()[0].v_key = r_method_name.get();
    module->entries()[0].v_value = r_multimethod.get();

    Vector* insts = make_vector(gc, /* capacity */ 3, /* length */ 3);
    Root r_insts(gc, Value::object(insts));
    insts->components()[0] = Value::fixnum(OpCode::LOAD_VALUE);
    insts->components()[1] = Value::fixnum(OpCode::LOAD_VALUE);
    insts->components()[2] = Value::fixnum(OpCode::INVOKE);

    Vector* args = make_vector(gc, /* capacity */ 4, /* length */ 4);
    Root r_args(gc, Value::object(args));
    // LOAD_VALUE: 5
    args->components()[0] = Value::fixnum(5);
    // LOAD_VALUE: 10
    args->components()[1] = Value::fixnum(10);
    // INVOKE: +: with two args
    args->components()[2] = r_method_name.get();
    args->components()[3] = Value::fixnum(2);

    Code* code = make_code(gc,
                           /* v_module */ r_module.get(),
                           /* num_regs */ 1,
                           /* num_data */ 1,
                           /* v_upreg_map */ Value::null(),
                           /* v_insts */ r_insts.get(),
                           /* v_args */ r_args.get());
    Root r_code(gc, Value::object(code));

    Value v_result = vm.eval_toplevel(Value::object(code));
    CHECK(v_result == Value::fixnum(15));
}

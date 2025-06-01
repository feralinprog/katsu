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

    OptionalRoot<Module> r_module_base(gc, nullptr);
    Root<Module> r_module(gc, make_module(gc, /* base */ r_module_base, /* capacity */ 0));

    OptionalRoot<Array> r_upreg_map(gc, nullptr);

    Array* insts = make_array_nofill(gc, /* length */ 1);
    insts->components()[0] = Value::fixnum(OpCode::LOAD_VALUE);
    Root<Array> r_insts(gc, std::move(insts));

    Array* args = make_array(gc, /* length */ 1);
    args->components()[0] = Value::fixnum(1234);
    Root<Array> r_args(gc, std::move(args));

    Root<Code> r_code(gc,
                      make_code(gc,
                                /* r_module */ r_module,
                                /* num_regs */ 1,
                                /* num_data */ 1,
                                /* r_upreg_map */ r_upreg_map,
                                /* r_insts */ r_insts,
                                /* r_args */ r_args));

    Value v_result = vm.eval_toplevel(r_code);
    CHECK(v_result == Value::fixnum(1234));

    // Evaluate again -- VM should be able to handle this easily.
    Value v_result_2 = vm.eval_toplevel(r_code);
    CHECK(v_result_2 == Value::fixnum(1234));
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

    Root<String> r_method_name(gc, make_string(gc, "+:"));

    ValueRoot r_param_matchers(gc, Value::null()); // TODO: not null

    OptionalRoot<Type> r_return_type(gc, nullptr);

    OptionalRoot<Code> r_method_code(gc, nullptr); // native!

    Root<Vector> r_method_attributes(gc, make_vector(gc, /* capacity */ 0));

    Root<Method> r_method(gc,
                          make_method(gc,
                                      /* r_param_matchers */ r_param_matchers,
                                      /* r_return_type */ r_return_type,
                                      /* r_code */ r_method_code,
                                      /* r_attributes */ r_method_attributes,
                                      /* native_handler */ &test__fixnum_add));

    Vector* methods = make_vector(gc, /* capacity */ 1);
    methods->length = 1;
    {
        Array* array = methods->v_array.obj_array();
        array->components()[0] = r_method.value();
    }
    Root<Vector> r_methods(gc, std::move(methods));

    Root<Vector> r_multimethod_attributes(gc, make_vector(gc, /* capacity */ 0));

    Root<MultiMethod> r_multimethod(gc,
                                    make_multimethod(gc,
                                                     /* r_name */ r_method_name,
                                                     /* r_methods */ r_methods,
                                                     /* r_attributes */ r_multimethod_attributes));

    OptionalRoot<Module> r_module_base(gc, nullptr);
    Root<Module> r_module(gc, make_module(gc, /* base */ r_module_base, /* capacity */ 1));
    r_module->length = 1;
    r_module->entries()[0].v_key = r_method_name.value();
    r_module->entries()[0].v_value = r_multimethod.value();

    OptionalRoot<Array> r_upreg_map(gc, nullptr);

    Array* insts = make_array(gc, /* length */ 3);
    insts->components()[0] = Value::fixnum(OpCode::LOAD_VALUE);
    insts->components()[1] = Value::fixnum(OpCode::LOAD_VALUE);
    insts->components()[2] = Value::fixnum(OpCode::INVOKE);
    Root<Array> r_insts(gc, std::move(insts));

    Array* args = make_array(gc, /* length */ 4);
    // LOAD_VALUE: 5
    args->components()[0] = Value::fixnum(5);
    // LOAD_VALUE: 10
    args->components()[1] = Value::fixnum(10);
    // INVOKE: +: with two args
    args->components()[2] = r_method_name.value();
    args->components()[3] = Value::fixnum(2);
    Root<Array> r_args(gc, std::move(args));

    Root<Code> r_code(gc,
                      make_code(gc,
                                /* r_module */ r_module,
                                /* num_regs */ 1,
                                /* num_data */ 2,
                                /* r_upreg_map */ r_upreg_map,
                                /* r_insts */ r_insts,
                                /* r_args */ r_args));

    Value v_result = vm.eval_toplevel(r_code);
    CHECK(v_result == Value::fixnum(15));
}

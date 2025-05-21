#include <catch2/catch_test_macros.hpp>

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

    Module* module = gc.alloc<Module>(0);
    Root r_module(gc, Value::object(module));
    module->v_base = Value::null();
    module->length = 0;

    Vector* insts = gc.alloc<Vector>(1);
    Root r_insts(gc, Value::object(insts));
    insts->capacity = 1;
    insts->length = 1;
    insts->components()[0] = Value::fixnum(OpCode::LOAD_VALUE);

    Vector* args = gc.alloc<Vector>(1);
    Root r_args(gc, Value::object(args));
    args->capacity = 1;
    args->length = 1;
    args->components()[0] = Value::fixnum(1234);

    Code* code = gc.alloc<Code>();
    Root r_code(gc, Value::object(code));
    code->v_module = r_module.get();
    code->num_regs = 1;
    code->num_data = 1;
    code->v_upreg_map = Value::null();
    code->v_insts = r_insts.get();
    code->v_args = r_args.get();

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

    String* method_name = gc.alloc<String>(strlen("+:"));
    Root r_method_name(gc, Value::object(method_name));
    method_name->length = strlen("+:");
    memcpy(method_name->contents(), "+:", strlen("+:"));

    Method* method = gc.alloc<Method>();
    Root r_method(gc, Value::object(method));
    method->v_param_matchers = Value::null(); // TODO
    method->v_return_type = Value::null();
    method->v_code = Value::null();       // native!
    method->v_attributes = Value::null(); // TODO should be vector
    method->native_handler = &test__fixnum_add;

    Vector* methods = gc.alloc<Vector>(1);
    Root r_methods(gc, Value::object(methods));
    methods->capacity = 1;
    methods->length = 1;
    methods->components()[0] = r_method.get();

    MultiMethod* multimethod = gc.alloc<MultiMethod>();
    Root r_multimethod(gc, Value::object(multimethod));
    multimethod->v_name = r_method_name.get();
    multimethod->v_methods = r_methods.get();
    multimethod->v_attributes = Value::null(); // TODO should be vector

    Module* module = gc.alloc<Module>(1);
    Root r_module(gc, Value::object(module));
    module->v_base = Value::null();
    module->length = 1;
    module->entries()[0].v_key = r_method_name.get();
    module->entries()[0].v_value = r_multimethod.get();

    Vector* insts = gc.alloc<Vector>(3);
    Root r_insts(gc, Value::object(insts));
    insts->capacity = 3;
    insts->length = 3;
    insts->components()[0] = Value::fixnum(OpCode::LOAD_VALUE);
    insts->components()[1] = Value::fixnum(OpCode::LOAD_VALUE);
    insts->components()[2] = Value::fixnum(OpCode::INVOKE);

    Vector* args = gc.alloc<Vector>(4);
    Root r_args(gc, Value::object(args));
    args->capacity = 4;
    args->length = 4;
    // LOAD_VALUE: 5
    args->components()[0] = Value::fixnum(5);
    // LOAD_VALUE: 10
    args->components()[1] = Value::fixnum(10);
    // INVOKE: +: with two args
    args->components()[2] = r_method_name.get();
    args->components()[3] = Value::fixnum(2);

    Code* code = gc.alloc<Code>();
    Root r_code(gc, Value::object(code));
    code->v_module = r_module.get();
    code->num_regs = 1;
    code->num_data = 1;
    code->v_upreg_map = Value::null();
    code->v_insts = r_insts.get();
    code->v_args = r_args.get();

    Value v_result = vm.eval_toplevel(Value::object(code));
    CHECK(v_result == Value::fixnum(15));
}

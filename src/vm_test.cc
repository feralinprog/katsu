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

    // Perform a simple LOAD_VALUE.
    Module* module = gc.alloc<Module>(0);
    Root r_module(gc, Value::object(module));
    module->v_base = Value::null();
    module->v_length = Value::fixnum(0);

    Vector* insts = gc.alloc<Vector>(1);
    Root r_insts(gc, Value::object(insts));
    insts->v_length = Value::fixnum(1);
    insts->components()[0] = Value::fixnum(BytecodeOp::LOAD_VALUE);

    Vector* args = gc.alloc<Vector>(1);
    Root r_args(gc, Value::object(args));
    args->v_length = Value::fixnum(1);
    args->components()[0] = Value::fixnum(1234);

    Code* code = gc.alloc<Code>();
    Root r_code(gc, Value::object(code));
    code->v_module = r_module.get();
    code->v_num_regs = Value::fixnum(1);
    code->v_num_data = Value::fixnum(1);
    code->v_upreg_map = Value::null();
    code->v_insts = r_insts.get();
    code->v_args = r_args.get();

    Value result = vm.eval_toplevel(Value::object(code));
    CHECK(result == Value::fixnum(1234));

    // Evaluate again -- VM should be able to handle this easily.
    result = vm.eval_toplevel(Value::object(code));
    CHECK(result == Value::fixnum(1234));
}

TEST_CASE("VM executes a native invocation", "[vm]")
{
    GC gc(1024 * 1024);
    VM vm(gc, 10 * 1024);
    // TODO
}

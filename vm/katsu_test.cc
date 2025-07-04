#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <catch2/matchers/catch_matchers_predicate.hpp>
#include <catch2/matchers/catch_matchers_templated.hpp>

#include "katsu.h"

#include "builtin.h"
#include "compile.h"
#include "gc.h"
#include "lexer.h"
#include "parser.h"
#include "value.h"
#include "value_utils.h"
#include "vm.h"

#include <iostream>
#include <sstream>
#include <variant>

using namespace Katsu;
using namespace Catch::Matchers;

std::ostream& operator<<(std::ostream& s, const SourceSpan& span);

// Modified from https://stackoverflow.com/a/5419388.
struct cout_capture
{
    cout_capture()
        : capture()
        , old(std::cout.rdbuf(this->capture.rdbuf()))
    {}

    ~cout_capture()
    {
        std::cout.rdbuf(old);
    }

    std::string str()
    {
        return this->capture.str();
    }

private:
    std::stringstream capture;
    std::streambuf* old;
};

namespace Katsu
{
    std::ostream& operator<<(std::ostream& s, const Value& value)
    {
        // TODO: make this incredibly less hacky
        cout_capture capture;
        pprint(value);
        s << capture.str();
        return s;
    }
};

Value testonly_handle_condition(VM& vm, int64_t nargs, Value* args)
{
    ASSERT(nargs == 2);
    ASSERT(args[0].is_obj_string());
    ASSERT(args[1].is_obj_string());
    std::string message =
        native_str(args[0].obj_string()) + ": " + native_str(args[1].obj_string());
    throw std::runtime_error(message);
}

TEST_CASE("integration - single top level expression", "[katsu]")
{
    // 100 KiB GC-managed memory.
    GC gc(100 * 1024);

    SourceFile source;

    auto input = [&source](const std::string& source_str) {
        source = SourceFile{
            .path = std::make_shared<std::string>("fake.path"),
            .source = std::make_shared<std::string>(source_str),
        };
    };

    auto run = [&source, &gc]() {
        Lexer lexer(source);
        TokenStream stream(lexer);
        std::unique_ptr<PrattParser> parser = make_default_parser();
        // 10 KiB call stack size.
        VM vm(gc, 10 * 1024);
        Root<Assoc> r_module(gc, make_assoc(gc, /* capacity */ 0));
        Root<Vector> r_imports(gc, make_vector(gc, /* capacity */ 0));

        // Just throw everything in the same module.
        register_builtins(vm, r_module, r_module);

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = vm.builtin(BuiltinId::_String);
            matchers2->components()[1] = vm.builtin(BuiltinId::_String);
            add_native(vm.gc,
                       r_module,
                       "handle-raw-condition-with-message:",
                       2,
                       matchers2,
                       &testonly_handle_condition);

            String* name = make_string(vm.gc, "handle-raw-condition-with-message:");
            Value* handler = assoc_lookup(*r_module, name);
            ASSERT(handler);
            vm.v_condition_handler = *handler;
        }

        std::unique_ptr<Expr> top_level_expr =
            parser->parse(stream, 0 /* precedence */, true /* is_toplevel */);

        std::vector<std::unique_ptr<Expr>> top_level_exprs;
        top_level_exprs.emplace_back(std::move(top_level_expr));
        Root<Code> code(gc,
                        compile_into_module(vm,
                                            r_module,
                                            r_imports,
                                            top_level_exprs[0]->span,
                                            top_level_exprs));
        return vm.eval_toplevel(code);
    };

    // auto check_identical = [&gc, &run](Value expected) {
    //     ValueRoot r_expected(gc, std::move(expected));
    //     CHECK(run() == *r_expected);
    // };

    // Structural check (based on prettyprinting).
    auto check = [&run](Value expected) {
        // Don't need to set up a root -- just prettyprint before calling run().
        std::stringstream ss_expected;
        ss_expected << expected;
        std::stringstream ss_actual;
        ss_actual << run();
        CHECK(ss_actual.str() == ss_expected.str());
    };

    // Check pprint directly against an expected string. Kind of hacky, but useful if it's annoying
    // to create an actual expected Value.
    auto check_pprint = [&run](const std::string& expected) {
        std::stringstream ss_actual;
        ss_actual << run();
        CHECK(ss_actual.str() == expected);
    };

    auto check_that = [&run](Catch::Matchers::MatcherBase<Value>&& matcher) {
        CHECK_THAT(run(), matcher);
    };

    // ========================================================================

    SECTION("fixnum")
    {
        input("1234");
        check(Value::fixnum(1234));
    }

    // TODO: negation
    // SECTION("negative fixnum") {
    //     input("-1234");
    //     check(Value::fixnum(-1234));
    // }

    // TODO: floats

    // TODO: bignums

    SECTION("null")
    {
        input("null");
        check(Value::null());
    }

    SECTION("true")
    {
        input("t");
        check(Value::_bool(true));
    }

    SECTION("false")
    {
        input("f");
        check(Value::_bool(false));
    }

    // TODO: ref

    SECTION("tuple")
    {
        input("1, 2, 3");
        Tuple* tuple = make_tuple(gc, 3);
        tuple->components()[0] = Value::fixnum(1);
        tuple->components()[1] = Value::fixnum(2);
        tuple->components()[2] = Value::fixnum(3);
        check(Value::object(tuple));
    }

    SECTION("tuple - trailing comma")
    {
        input("1, 2, 3,");
        Tuple* tuple = make_tuple(gc, 3);
        tuple->components()[0] = Value::fixnum(1);
        tuple->components()[1] = Value::fixnum(2);
        tuple->components()[2] = Value::fixnum(3);
        check(Value::object(tuple));
    }

    SECTION("tuple - empty")
    {
        input("()");
        Tuple* tuple = make_tuple(gc, 0);
        check(Value::object(tuple));
    }

    SECTION("tuple - empty with newlines")
    {
        input("(\n)");
        Tuple* tuple = make_tuple(gc, 0);
        check(Value::object(tuple));
    }

    // TODO: array

    SECTION("vector")
    {
        input("{ 1; 2; 3 }");
        Root<Vector> r_vector(gc, make_vector(gc, 3));
        ValueRoot v1(gc, Value::fixnum(1));
        ValueRoot v2(gc, Value::fixnum(2));
        ValueRoot v3(gc, Value::fixnum(3));
        append(gc, r_vector, v1);
        append(gc, r_vector, v2);
        append(gc, r_vector, v3);
        check(r_vector.value());
    }

    SECTION("vector - trailing semicolon")
    {
        input("{ 1; 2; 3; }");
        Root<Vector> r_vector(gc, make_vector(gc, 3));
        ValueRoot v1(gc, Value::fixnum(1));
        ValueRoot v2(gc, Value::fixnum(2));
        ValueRoot v3(gc, Value::fixnum(3));
        append(gc, r_vector, v1);
        append(gc, r_vector, v2);
        append(gc, r_vector, v3);
        check(r_vector.value());
    }

    SECTION("vector - newlines")
    {
        input(R"({
            1
            2
            3
        })");
        Root<Vector> r_vector(gc, make_vector(gc, 3));
        ValueRoot v1(gc, Value::fixnum(1));
        ValueRoot v2(gc, Value::fixnum(2));
        ValueRoot v3(gc, Value::fixnum(3));
        append(gc, r_vector, v1);
        append(gc, r_vector, v2);
        append(gc, r_vector, v3);
        check(r_vector.value());
    }

    SECTION("vector - empty")
    {
        input("{}");
        check(Value::object(make_vector(gc, 0)));
    }

    SECTION("vector - empty with newlines")
    {
        input("{\n}");
        check(Value::object(make_vector(gc, 0)));
    }

    // TODO: assoc

    SECTION("string")
    {
        input(R"("abc")");
        check(Value::object(make_string(gc, "abc")));
    }

    // TODO: code

    SECTION("closure")
    {
        input("[ it + 1 ]");
        check_pprint(R"(*closure
  v_code = *code
    num_params = 1
    num_regs = 1
    num_data = 2
    v_upreg_map = *array: length=0
    bytecode:
    [0]: load_reg @0
    [1]: load_value: fixnum 1
    [2]: invoke #2 *string: "+:"
  v_upregs = *array: length=0
)");
    }

    // TODO: method

    // TODO: multimethod

    // TODO: type

    // TODO: instance

    SECTION("Fixnum")
    {
        input("Fixnum");
        check_that(Predicate<Value>([](Value v) {
            Type* t = v.obj_type();
            return string_eq(t->v_name.obj_string(), "Fixnum") &&
                   t->kind == Type::Kind::PRIMITIVE && t->sealed;
        }));
    }

    // TODO: other builtin types

    SECTION("string ~:")
    {
        input(R"("hello," ~ " " ~ "world!")");
        check(Value::object(make_string(gc, "hello, world!")));
    }

    SECTION("fixnum +:")
    {
        input("3 + 4");
        check(Value::fixnum(7));
    }

    SECTION("fixnum -:")
    {
        input("7 - 4");
        check(Value::fixnum(3));
    }

    SECTION("fixnum +")
    {
        input("+ 4");
        check(Value::fixnum(4));
    }

    SECTION("fixnum -")
    {
        input("- 4");
        check(Value::fixnum(-4));
    }

    SECTION("fixnum *:")
    {
        input("3 * 4");
        check(Value::fixnum(12));
    }

    SECTION("fixnum /:")
    {
        input("12 / 4");
        check(Value::fixnum(3));
    }

    SECTION("fixnum /: - divide by zero")
    {
        input("1 / 0");
        CHECK_THROWS_MATCHES(run(),
                             std::runtime_error,
                             Message("divide-by-zero: cannot divide by integer 0"));
    }

    // TODO: add id!= as an operator
    SECTION("fixnum id=: positive case")
    {
        input("1 id=: 1");
        check(Value::_bool(true));
    }

    SECTION("fixnum id=: negative case")
    {
        input("1 id=: 2");
        check(Value::_bool(false));
    }

    SECTION("object id=: positive case")
    {
        input("Fixnum id=: Fixnum");
        check(Value::_bool(true));
    }

    SECTION("object id=: negative case")
    {
        input(R"("abc" id=: "abc")");
        check(Value::_bool(false));
    }

    SECTION("fixnum =: positive case")
    {
        input("1 = 1");
        check(Value::_bool(true));
    }

    SECTION("fixnum =: negative case")
    {
        input("1 = 2");
        check(Value::_bool(false));
    }

    SECTION("object =: positive case")
    {
        input("Fixnum = Fixnum");
        check(Value::_bool(true));
    }

    SECTION("object =: negative case")
    {
        input(R"({} = {})");
        check(Value::_bool(false));
    }

    // TODO: add id!= as an operator
    SECTION("fixnum id!=: negative case")
    {
        input("1 id!=: 1");
        check(Value::_bool(false));
    }

    SECTION("fixnum id!=: positive case")
    {
        input("1 id!=: 2");
        check(Value::_bool(true));
    }

    SECTION("object id!=: negative case")
    {
        input("Fixnum id!=: Fixnum");
        check(Value::_bool(false));
    }

    SECTION("object id!=: positive case")
    {
        input(R"("abc" id!=: "abc")");
        check(Value::_bool(true));
    }

    SECTION("fixnum !=: negative case")
    {
        input("1 != 1");
        check(Value::_bool(false));
    }

    SECTION("fixnum !=: positive case")
    {
        input("1 != 2");
        check(Value::_bool(true));
    }

    SECTION("object !=: negative case")
    {
        input("Fixnum != Fixnum");
        check(Value::_bool(false));
    }

    SECTION("object !=: positive case")
    {
        input(R"({} != {})");
        check(Value::_bool(true));
    }

    SECTION("string =: positive case")
    {
        input(R"(("a" ~ "b") = "ab")");
        check(Value::_bool(true));
    }

    SECTION("string =: negative case")
    {
        input(R"(("a" ~ "b") = "ac")");
        check(Value::_bool(false));
    }

    SECTION("string !=: negative case")
    {
        input(R"(("a" ~ "b") != "ab")");
        check(Value::_bool(false));
    }

    SECTION("string !=: positive case")
    {
        input(R"(("a" ~ "b") != "ac")");
        check(Value::_bool(true));
    }

    SECTION("fixnum >: positive case")
    {
        input("2 > 1");
        check(Value::_bool(true));
    }

    SECTION("fixnum >: negative case")
    {
        input("1 > 1");
        check(Value::_bool(false));
    }

    SECTION("fixnum >=: positive case")
    {
        input("1 >= 1");
        check(Value::_bool(true));
    }

    SECTION("fixnum >=: negative case")
    {
        input("1 >= 2");
        check(Value::_bool(false));
    }

    SECTION("fixnum <: positive case")
    {
        input("1 < 2");
        check(Value::_bool(true));
    }

    SECTION("fixnum <: negative case")
    {
        input("1 < 1");
        check(Value::_bool(false));
    }

    SECTION("fixnum <=: positive case")
    {
        input("1 <= 1");
        check(Value::_bool(true));
    }

    SECTION("fixnum <=: negative case")
    {
        input("2 <= 1");
        check(Value::_bool(false));
    }

    SECTION("bool and: false/false case")
    {
        input("f and f");
        check(Value::_bool(false));
    }

    SECTION("bool and: false/true case")
    {
        input("f and t");
        check(Value::_bool(false));
    }

    SECTION("bool and: true/false case")
    {
        input("t and f");
        check(Value::_bool(false));
    }

    SECTION("bool and: true/true case")
    {
        input("t and t");
        check(Value::_bool(true));
    }

    SECTION("bool or: false/false case")
    {
        input("f or f");
        check(Value::_bool(false));
    }

    SECTION("bool or: false/true case")
    {
        input("f or t");
        check(Value::_bool(true));
    }

    SECTION("bool or: true/false case")
    {
        input("t or f");
        check(Value::_bool(true));
    }

    SECTION("bool or: true/true case")
    {
        input("t or t");
        check(Value::_bool(true));
    }

    SECTION("bool not - false case")
    {
        input("not f");
        check(Value::_bool(true));
    }

    SECTION("bool not - true case")
    {
        input("not t");
        check(Value::_bool(false));
    }

    SECTION("print:")
    {
        cout_capture capture;
        input(R"(print: "hello, world")");
        check(Value::null());
        CHECK(capture.str() == "hello, world\n");
    }

    SECTION("pretty-print:")
    {
        cout_capture capture;
        input("pretty-print: 1234");
        check(Value::null());
        CHECK(capture.str() == "fixnum 1234\n");
    }

    SECTION("then:else: - true condition")
    {
        input(R"(t then: ["true result"] else: ["false result"])");
        check(Value::object(make_string(gc, "true result")));
    }
    SECTION("then:else: - false condition")
    {
        input(R"(f then: ["true result"] else: ["false result"])");
        check(Value::object(make_string(gc, "false result")));
    }
    SECTION("then:else: - non-bool condition")
    {
        input(R"(1234 then: ["true result"] else: ["false result"])");
        check(Value::object(make_string(gc, "false result")));
    }
    SECTION("then:else: - body with parameters")
    {
        input(R"(t then: \a b [a + b] else: ["false result"])");
        CHECK_THROWS_MATCHES(
            run(),
            std::runtime_error,
            Message("argument-count-mismatch: called a closure with wrong number of arguments"));
    }

    SECTION("call - closure")
    {
        input(R"(["closure result"] call)");
        check(Value::object(make_string(gc, "closure result")));
    }
    SECTION("call - closure with default 'it' parameter")
    {
        input(R"([it] call)");
        check(Value::null());
    }
    SECTION("call - closure with explicit single parameter")
    {
        input(R"(\x [x] call)");
        check(Value::null());
    }
    SECTION("call - non-closure")
    {
        input(R"(1234 call)");
        check(Value::fixnum(1234));
    }
    SECTION("call - closure but wrong parameter count")
    {
        input(R"(\a b ["closure result"] call)");
        CHECK_THROWS_MATCHES(
            run(),
            std::runtime_error,
            Message("argument-count-mismatch: called a closure with wrong number of arguments"));
    }

    SECTION("call: - closure")
    {
        input(R"(\x [x + 5] call: 10)");
        check(Value::fixnum(15));
    }
    SECTION("call: - closure with default 'it' parameter")
    {
        input(R"([it + 5] call: 10)");
        check(Value::fixnum(15));
    }
    SECTION("call: - non-closure")
    {
        input(R"(1234 call: 10)");
        check(Value::fixnum(1234));
    }
    SECTION("call: - closure but wrong parameter count")
    {
        input(R"(\a b [a + b] call: 10)");
        CHECK_THROWS_MATCHES(
            run(),
            std::runtime_error,
            Message("argument-count-mismatch: called a closure with wrong number of arguments"));
    }

    SECTION("call*: - closure")
    {
        input(R"(\x [x + 5] call*: (10,))");
        check(Value::fixnum(15));
    }
    SECTION("call*: - closure with empty tuple")
    {
        input(R"(\x [x + 5] call*: ())");
        CHECK_THROWS_MATCHES(run(),
                             std::runtime_error,
                             Message("invalid-argument: arguments must be non-empty"));
    }
    SECTION("call*: - closure with multiple parameters")
    {
        input(R"(\a b c [a + b + c] call*: (1, 2, 3))");
        check(Value::fixnum(6));
    }
    SECTION("call*: - closure with default 'it' parameter")
    {
        input(R"([it + 5] call*: (10,))");
        check(Value::fixnum(15));
    }
    SECTION("call*: - closure with default 'it' parameter and empty tuple")
    {
        input(R"([it + 5] call*: ())");
        CHECK_THROWS_MATCHES(run(),
                             std::runtime_error,
                             Message("invalid-argument: arguments must be non-empty"));
    }
    SECTION("call*: - closure but not enough arguments")
    {
        input(R"(\a b [a + b] call*: (10,))");
        CHECK_THROWS_MATCHES(
            run(),
            std::runtime_error,
            Message("argument-count-mismatch: called a closure with wrong number of arguments"));
    }
    SECTION("call*: - closure but too many arguments")
    {
        input(R"(\a b [a + b] call*: (10, 20, 30))");
        CHECK_THROWS_MATCHES(
            run(),
            std::runtime_error,
            Message("argument-count-mismatch: called a closure with wrong number of arguments"));
    }
    SECTION("call*: - arguments not a tuple")
    {
        input(R"(\a b [a + b] call*: { 10; 20; 30 })");
        CHECK_THROWS_MATCHES(
            run(),
            std::runtime_error,
            Message("no-matching-method: multimethod has no methods matching the given arguments"));
    }
    SECTION("call*: - callable not a closure")
    {
        input(R"("not callable" call*: (10, 20))");
        check(Value::object(make_string(gc, "not callable")));
    }

    // TODO: type
    // TODO: subtype?:
    // TODO: instance?:

    SECTION("TEST-ASSERT: - assertion passing")
    {
        input("TEST-ASSERT: t");
        check(Value::null());
    }

    SECTION("TEST-ASSERT: - assertion failing")
    {
        input("TEST-ASSERT: f");
        CHECK_THROWS_MATCHES(run(),
                             std::logic_error,
                             MessageMatches(ContainsSubstring("TEST-ASSERT: failed assertion")));
    }
}

TEST_CASE("integration - whole file", "[katsu]")
{
    // 1 MiB GC-managed memory.
    GC gc(1024 * 1024);

    SourceFile source;

    auto input = [&source](const std::string& source_str) {
        source = SourceFile{
            .path = std::make_shared<std::string>("fake.path"),
            .source = std::make_shared<std::string>(source_str),
        };
    };

    // Default to a reasonably sized 10 KiB stack.
    uint64_t call_stack_size = 10 * 1024;

    auto run = [&source, &gc, &call_stack_size]() {
        return bootstrap_and_run_source(source, "test.integration", gc, call_stack_size);
    };

    // Structural check (based on prettyprinting).
    auto check = [&run](Value expected) {
        // Don't need to set up a root -- just prettyprint before calling run().
        std::stringstream ss_expected;
        ss_expected << expected;
        std::stringstream ss_actual;
        ss_actual << run();
        CHECK(ss_actual.str() == ss_expected.str());
    };

    // // Check pprint directly against an expected string. Kind of hacky, but useful if it's
    // annoying
    // // to create an actual expected Value.
    // auto check_pprint = [&run](const std::string& expected) {
    //     std::stringstream ss_actual;
    //     ss_actual << run();
    //     CHECK(ss_actual.str() == expected);
    // };

    // auto check_that = [&run](Catch::Matchers::MatcherBase<Value>&& matcher) {
    //     CHECK_THAT(run(), matcher);
    // };

    // ========================================================================

    SECTION("core smoketest")
    {
        input("1 + 2");
        check(Value::fixnum(3));
    }

    SECTION("newlines")
    {
        input(R"(

let: a = 3

let: b = 5

a + b

            )");
        check(Value::fixnum(8));
    }

    SECTION("basic method, closure, mutable bindings")
    {
        input(R"(
let: (adder: n0) do: [
    mut: n = n0
    let: delta = n0 + 30
    \x [
        let: new-n = n + delta + x
        n: new-n
        n
    ]
]

let: a = (adder: 3)
(a call: 0) # 3 + 33 + 0 = 36
(a call: 2) # 36 + 33 + 2 = 71
        )");
        check(Value::fixnum(71));
    }

    SECTION("method definition - non block")
    {
        input(R"(
let: n add-one do: [ n + 1 ]
5 add-one
        )");
        check(Value::fixnum(6));
    }

    SECTION("duplicate immutable bindings")
    {
        input(R"(
let: (test: n) do: [
    "thisisatest"
    let: x = n
    let: x = x + 10
    x
]
test: 5
        )");
        check(Value::fixnum(15));
    }

    SECTION("recursion")
    {
        input(R"(
let: (n triangular-num: result) do: [
    (n = 0) then: result else: [
        (n - 1) triangular-num: (n + result)
    ]
]

let: (n triangular-num) do: [n triangular-num: 0]

100 triangular-num
        )");
        // Bump up stack limit for this.
        call_stack_size = 100 * 1024;
        check(Value::fixnum(100 * (100 + 1) / 2));
    }

    SECTION("tail recursion")
    {
        input(R"(
let: (n triangular-num: result) do: [
    # TODO: then:else: should probably assume tail-call if in tail position, even if not requested
    TAIL-CALL: ((n = 0) then: result else: [
        TAIL-CALL: ((n - 1) triangular-num: (n + result))
    ])
]

let: (n triangular-num) do: [n triangular-num: 0]

2000 triangular-num
        )");
        // No need to bump stack limit!
        check(Value::fixnum(2000 * (2000 + 1) / 2));
    }

    SECTION("nontail position - cannot tail call")
    {
        input(R"(
let: testing do: [
    TAIL-CALL: testing
    "but does something afterwards"
]
        )");
        CHECK_THROWS_MATCHES(run(),
                             std::runtime_error,
                             Message("TAIL-CALL: invoked not in tail position"));
    }

    SECTION("TAIL-CALL: at top level")
    {
        input(R"(
TAIL-CALL: (1 + 2)
        )");
        CHECK_THROWS_MATCHES(run(),
                             std::runtime_error,
                             Message("TAIL-CALL: invoked not in tail position"));
    }

    SECTION("multimethod smoketest")
    {
        SECTION("multimethod accepts stated argument types")
        {
            input(R"(
let: ((a: Fixnum) mm-test: (b: Fixnum)) do: [ "Fixnum - Fixnum" ]

5 mm-test: 10
            )");
            check(Value::object(make_string(gc, "Fixnum - Fixnum")));
        }

        SECTION("multimethod rejects undeclared argument types - case 1")
        {
            input(R"(
IMPORT-EXISTING-MODULE: "core.builtin.extra" # for TEST-ASSERT, set-condition-handler-from-module
let: (c handle-raw-condition-with-message: m) do: [
    TEST-ASSERT: (c ~ ": " ~ m) = "no-matching-method: multimethod has no methods matching the given arguments"
    12345
]
set-condition-handler-from-module
let: ((a: Fixnum) mm-test: (b: Fixnum)) do: [ "Fixnum - Fixnum" ]

"abc" mm-test: 10
            )");
            check(Value::fixnum(12345));
        }

        SECTION("multimethod rejects undeclared argument types - case 2")
        {
            input(R"(
IMPORT-EXISTING-MODULE: "core.builtin.extra" # for TEST-ASSERT, set-condition-handler-from-module
let: (c handle-raw-condition-with-message: m) do: [
    TEST-ASSERT: (c ~ ": " ~ m) = "no-matching-method: multimethod has no methods matching the given arguments"
    12345
]
set-condition-handler-from-module
let: ((a: Fixnum) mm-test: (b: Fixnum)) do: [ "Fixnum - Fixnum" ]

5 mm-test: "def"
            )");
            check(Value::fixnum(12345));
        }

        SECTION("multimethod downselection - case 1")
        {
            input(R"(
let: ((a: Fixnum) mm-test:  b         ) do: [ "Fixnum - any" ]
let: ( a          mm-test: (b: Fixnum)) do: [ "any - Fixnum" ]
let: ( a          mm-test:  b         ) do: [ "any - any"    ]

5 mm-test: "def"
            )");
            check(Value::object(make_string(gc, "Fixnum - any")));
        }

        SECTION("multimethod downselection - case 2")
        {
            input(R"(
let: ((a: Fixnum) mm-test:  b         ) do: [ "Fixnum - any" ]
let: ( a          mm-test: (b: Fixnum)) do: [ "any - Fixnum" ]
let: ( a          mm-test:  b         ) do: [ "any - any"    ]

"abc" mm-test: 10
            )");
            check(Value::object(make_string(gc, "any - Fixnum")));
        }

        SECTION("multimethod downselection - case 3")
        {
            input(R"(
let: ((a: Fixnum) mm-test:  b         ) do: [ "Fixnum - any" ]
let: ( a          mm-test: (b: Fixnum)) do: [ "any - Fixnum" ]
let: ( a          mm-test:  b         ) do: [ "any - any"    ]

"abc" mm-test: "def"
            )");
            check(Value::object(make_string(gc, "any - any")));
        }

        SECTION("multimethod downselection - case 3 (ambiguous)")
        {
            input(R"(
IMPORT-EXISTING-MODULE: "core.builtin.extra" # for TEST-ASSERT, set-condition-handler-from-module
let: (c handle-raw-condition-with-message: m) do: [
    TEST-ASSERT: (c ~ ": " ~ m) = "ambiguous-method-resolution: multimethod has multiple best methods matching the given arguments"
    12345
]
set-condition-handler-from-module
let: ((a: Fixnum) mm-test:  b         ) do: [ "Fixnum - any" ]
let: ( a          mm-test: (b: Fixnum)) do: [ "any - Fixnum" ]
let: ( a          mm-test:  b         ) do: [ "any - any"    ]

5 mm-test: 10
            )");
            check(Value::fixnum(12345));
        }
    }

    SECTION("multimethod parameter matchers evaluated at runtime")
    {
        input(R"(
# TODO: allow mut:s at module level, so we don't need this workaround
let: init hacky-make-mut do: [
    mut: boxed = init
    \new-value [
        boxed # TODO: delete this required workaround
        new-value = null then: [
            boxed
        ] else: [
            boxed # TODO: delete this required workaround
            boxed: new-value
        ]
    ]
]

let: verify-matcher = ("matcher not evaluated" hacky-make-mut)
let: give-me-Fixnum do: [ verify-matcher call: "matcher evaluated"; Fixnum ]

let: verify-multimethod = ("multimethod not evaluated" hacky-make-mut)
let: (a: (null give-me-Fixnum)) mm-test do: [ verify-multimethod call: "multimethod evaluated" ]

5 mm-test

(verify-matcher call: null) ~ ", " ~ (verify-multimethod call: null)
        )");
        check(Value::object(make_string(gc, "matcher evaluated, multimethod evaluated")));
    }

    SECTION("dataclass smoketest")
    {
        input(R"(
IMPORT-EXISTING-MODULE: "core.builtin.extra" # for TEST-ASSERT:
data: Thing has: { slot-a; slot-b; slot-c }

TEST-ASSERT: not ("abc" Thing?)

let: thing = (Thing slot-a: t slot-b: 5 slot-c: "c")
TEST-ASSERT: thing Thing?
TEST-ASSERT: (thing .slot-a = t)
TEST-ASSERT: (thing .slot-b = 5)
TEST-ASSERT: (thing .slot-c = "c")

thing slot-a: 10
TEST-ASSERT: (thing .slot-a = 10)
TEST-ASSERT: (thing .slot-b = 5)
TEST-ASSERT: (thing .slot-c = "c")

thing slot-b: 20
TEST-ASSERT: (thing .slot-a = 10)
TEST-ASSERT: (thing .slot-b = 20)
TEST-ASSERT: (thing .slot-c = "c")

thing slot-c: 30
TEST-ASSERT: (thing .slot-a = 10)
TEST-ASSERT: (thing .slot-b = 20)
TEST-ASSERT: (thing .slot-c = 30)
        )");
        check(Value::null());
    }

    // TODO: test
    // - dataclass with no fields
    // - dataclass extending from dataclass base
    // - dataclass with field matching some base
    // - dataclass extending from sealed base
    // - dataclass extending from multiple dataclass bases
    // - dataclass extending from dataclass base and another type (mixin)
    // - dataclass needing to share an existing multimethod (with conflict)

    SECTION("unary method")
    {
        input(R"(
let: unary do: [ 1234 ]
unary
        )");
        check(Value::fixnum(1234));
    }

    SECTION("delimited continuation - example")
    {
        cout_capture capture;
        input(R"CODE(
IMPORT-EXISTING-MODULE: "core.builtin.extra" # for delimited continuations
print: "aaaaa"
[
    print: "  bbbbb"
    let: input = (\k [
        print: "    ccccc"
        print: "    result of k('abcdef'): " ~ (k call: "abcdef")
        print: "    result of k('123456'): " ~ (k call: "123456")
        print: "    ddddd"
    ] call/dc: t)
    print: "  eeeee"
    # Pretend to do some calculations, and return the result:
    "calcs(" ~ input ~ ")"
] call/marked: t
print: "zzzzz"
        )CODE");
        check(Value::null());
        CHECK(capture.str() == R"(aaaaa
  bbbbb
    ccccc
  eeeee
    result of k('abcdef'): calcs(abcdef)
  eeeee
    result of k('123456'): calcs(123456)
    ddddd
zzzzz
)");
    }

    SECTION("delimited continuation - wrong marker")
    {
        input(R"CODE(
IMPORT-EXISTING-MODULE: "core.builtin.extra" # for delimited continuations, TEST-ASSERT:, and set-condition-handler-from-module
let: (c handle-raw-condition-with-message: m) do: [
    TEST-ASSERT: (c ~ ": " ~ m) = "marker-not-found: did not find marker in call stack"
    12345
]
set-condition-handler-from-module
[
    [null] call/dc: f
] call/marked: t
        )CODE");
        check(Value::fixnum(12345));
    }

    SECTION("delimited continuation - multiple markers - outer")
    {
        SECTION("outer")
        {
            cout_capture capture;
            input(R"CODE(
IMPORT-EXISTING-MODULE: "core.builtin.extra" # for delimited continuations
let: m1 = "marker 1"
let: m2 = "marker 2"
[
    [
        [
            print: "escaping to marker"
        ] call/dc: m1
        print: "after call/dc: m1"
    ] call/marked: m2
    print: "after call/marked: m2"
] call/marked: m1
print: "after call/marked: m1"
            )CODE");
            check(Value::null());
            CHECK(capture.str() == R"(escaping to marker
after call/marked: m1
)");
        }

        SECTION("inner")
        {
            cout_capture capture;
            input(R"CODE(
IMPORT-EXISTING-MODULE: "core.builtin.extra" # for delimited continuations
let: m1 = "marker 1"
let: m2 = "marker 2"
[
    [
        [
            print: "escaping to marker"
        ] call/dc: m2
        print: "after call/dc: m2"
    ] call/marked: m2
    print: "after call/marked: m2"
] call/marked: m1
print: "after call/marked: m1"
        )CODE");
            check(Value::null());
            CHECK(capture.str() == R"(escaping to marker
after call/marked: m2
after call/marked: m1
)");
        }
    }
}

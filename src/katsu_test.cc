#include <catch2/catch_test_macros.hpp>
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
        OptionalRoot<Module> r_module_base(gc, nullptr);
        Root<Module> r_module(gc, make_module(gc, r_module_base, /* capacity */ 0));

        Builtins builtins(gc);
        builtins.register_builtins(r_module);

        std::unique_ptr<Expr> top_level_expr =
            parser->parse(stream, 0 /* precedence */, true /* is_toplevel */);

        std::vector<std::unique_ptr<Expr>> top_level_exprs;
        top_level_exprs.emplace_back(std::move(top_level_expr));
        Root<Code> code(gc, compile_into_module(gc, r_module, top_level_exprs));
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

    // TODO: module

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

    SECTION("fixnum +:")
    {
        input("3 + 4");
        check(Value::fixnum(7));
    }

    SECTION("pretty-print:")
    {
        cout_capture capture;
        input("pretty-print: 1234");
        check(Value::null());
        CHECK(capture.str() == "fixnum 1234\n");
    }
}

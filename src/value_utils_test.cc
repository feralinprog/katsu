#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <catch2/matchers/catch_matchers_templated.hpp>

#include "value_utils.h"

using namespace Katsu;
using namespace Catch::Matchers;

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

TEST_CASE("native_str", "[value-utils]")
{
    GC gc(1024 * 1024);

    CHECK(native_str(make_string(gc, "test string")) == "test string");
}

TEST_CASE("concat(String, String)", "[value-utils]")
{
    GC gc(1024 * 1024);

    Root<String> r_a(gc, make_string(gc, "left "));
    Root<String> r_b(gc, make_string(gc, "right"));
    CHECK(native_str(concat(gc, r_a, r_b)) == "left right");
}

TEST_CASE("concat(String, string)", "[value-utils]")
{
    GC gc(1024 * 1024);

    Root<String> r_a(gc, make_string(gc, "left "));
    CHECK(native_str(concat(gc, r_a, "right")) == "left right");
}

TEST_CASE("concat(string, String)", "[value-utils]")
{
    GC gc(1024 * 1024);

    Root<String> r_b(gc, make_string(gc, "right"));
    CHECK(native_str(concat(gc, "left ", r_b)) == "left right");
}

TEST_CASE("concat native strings", "[value-utils]")
{
    GC gc(1024 * 1024);

    std::vector<std::string> parts = {"abc", "def", "ghi"};
    CHECK(native_str(concat(gc, parts)) == "abcdefghi");
}

TEST_CASE("concat_with_suffix - native string", "[value-utils]")
{
    GC gc(1024 * 1024);

    std::vector<std::string> parts = {"abc", "def", "ghi"};
    CHECK(native_str(concat_with_suffix(gc, parts, ":")) == "abc:def:ghi:");
}

TEST_CASE("concat_with_suffix - String", "[value-utils]")
{
    GC gc(1024 * 1024);

    Root<Vector> r_strings(gc, make_vector(gc, 3));
    {
        ValueRoot r_v(gc, Value::object(make_string(gc, "abc")));
        append(gc, r_strings, r_v);
    }
    {
        ValueRoot r_v(gc, Value::object(make_string(gc, "def")));
        append(gc, r_strings, r_v);
    }
    {
        ValueRoot r_v(gc, Value::object(make_string(gc, "ghi")));
        append(gc, r_strings, r_v);
    }

    CHECK(native_str(concat_with_suffix(gc, r_strings, ":")) == "abc:def:ghi:");
}

TEST_CASE("c3 merge", "[value-utils]")
{
    GC gc(1024 * 1024);

    // Example taken from https://www.python.org/download/releases/2.3/mro/ ("First example").

    ValueRoot A(gc, Value::object(make_string(gc, "A")));
    ValueRoot B(gc, Value::object(make_string(gc, "B")));
    ValueRoot C(gc, Value::object(make_string(gc, "C")));
    ValueRoot D(gc, Value::object(make_string(gc, "D")));
    ValueRoot E(gc, Value::object(make_string(gc, "E")));
    ValueRoot F(gc, Value::object(make_string(gc, "F")));
    ValueRoot O(gc, Value::object(make_string(gc, "O")));

    Root<Array> bases_A(gc, make_array(gc, 2));
    bases_A->components()[0] = *B;
    bases_A->components()[1] = *C;
    Root<Array> bases_B(gc, make_array(gc, 2));
    bases_B->components()[0] = *D;
    bases_B->components()[1] = *E;
    Root<Array> bases_C(gc, make_array(gc, 2));
    bases_C->components()[0] = *D;
    bases_C->components()[1] = *F;
    Root<Array> bases_D(gc, make_array(gc, 1));
    bases_D->components()[0] = *O;
    Root<Array> bases_E(gc, make_array(gc, 1));
    bases_E->components()[0] = *O;
    Root<Array> bases_F(gc, make_array(gc, 1));
    bases_F->components()[0] = *O;
    Root<Array> bases_O(gc, make_array(gc, 0));

    Root<Vector> L_A(gc, make_vector(gc, 0));
    Root<Vector> L_B(gc, make_vector(gc, 0));
    Root<Vector> L_C(gc, make_vector(gc, 0));
    Root<Vector> L_D(gc, make_vector(gc, 0));
    Root<Vector> L_E(gc, make_vector(gc, 0));
    Root<Vector> L_F(gc, make_vector(gc, 0));
    Root<Vector> L_O(gc, make_vector(gc, 0));

    // Start calculating linearizations in topological order:
    //   O -> D -> E -> F -> B -> C -> A
    // This doesn't necessarily match the final C3 linearization.
    // For each 'type', calculate the linearization by:
    // - start with the type
    // - c3_merge to extend with <the linearizations of the type's bases, and the bases themselves>

    // O
    {
        Root<Array> r_linearizations(gc, make_array(gc, 1));
        r_linearizations->components()[0] = bases_O.value();
        append(gc, L_O, O);
        REQUIRE(c3_merge(gc, r_linearizations, L_O));
    }
    {
        REQUIRE(L_O->length == 1);
        CHECK(L_O->v_array.obj_array()->components()[0] == *O);
    }
    Root<Array> arr_L_O(gc, vector_to_array(gc, L_O));

    // D (O)
    {
        Root<Array> r_linearizations(gc, make_array(gc, 2));
        r_linearizations->components()[0] = arr_L_O.value();
        r_linearizations->components()[1] = bases_D.value();
        append(gc, L_D, D);
        REQUIRE(c3_merge(gc, r_linearizations, L_D));
    }
    {
        REQUIRE(L_D->length == 2);
        CHECK(L_D->v_array.obj_array()->components()[0] == *D);
        CHECK(L_D->v_array.obj_array()->components()[1] == *O);
    }
    Root<Array> arr_L_D(gc, vector_to_array(gc, L_D));

    // E (O)
    {
        Root<Array> r_linearizations(gc, make_array(gc, 2));
        r_linearizations->components()[0] = arr_L_O.value();
        r_linearizations->components()[1] = bases_E.value();
        append(gc, L_E, E);
        REQUIRE(c3_merge(gc, r_linearizations, L_E));
    }
    {
        REQUIRE(L_E->length == 2);
        CHECK(L_E->v_array.obj_array()->components()[0] == *E);
        CHECK(L_E->v_array.obj_array()->components()[1] == *O);
    }
    Root<Array> arr_L_E(gc, vector_to_array(gc, L_E));

    // F (O)
    {
        Root<Array> r_linearizations(gc, make_array(gc, 2));
        r_linearizations->components()[0] = arr_L_O.value();
        r_linearizations->components()[1] = bases_F.value();
        append(gc, L_F, F);
        REQUIRE(c3_merge(gc, r_linearizations, L_F));
    }
    {
        REQUIRE(L_F->length == 2);
        CHECK(L_F->v_array.obj_array()->components()[0] == *F);
        CHECK(L_F->v_array.obj_array()->components()[1] == *O);
    }
    Root<Array> arr_L_F(gc, vector_to_array(gc, L_F));

    // B (D, E)
    {
        Root<Array> r_linearizations(gc, make_array(gc, 3));
        r_linearizations->components()[0] = arr_L_D.value();
        r_linearizations->components()[1] = arr_L_E.value();
        r_linearizations->components()[2] = bases_B.value();
        append(gc, L_B, B);
        REQUIRE(c3_merge(gc, r_linearizations, L_B));
    }
    {
        REQUIRE(L_B->length == 4);
        CHECK(L_B->v_array.obj_array()->components()[0] == *B);
        CHECK(L_B->v_array.obj_array()->components()[1] == *D);
        CHECK(L_B->v_array.obj_array()->components()[2] == *E);
        CHECK(L_B->v_array.obj_array()->components()[3] == *O);
    }
    Root<Array> arr_L_B(gc, vector_to_array(gc, L_B));

    // C (D, F)
    {
        Root<Array> r_linearizations(gc, make_array(gc, 3));
        r_linearizations->components()[0] = arr_L_D.value();
        r_linearizations->components()[1] = arr_L_F.value();
        r_linearizations->components()[2] = bases_C.value();
        append(gc, L_C, C);
        REQUIRE(c3_merge(gc, r_linearizations, L_C));
    }
    {
        REQUIRE(L_C->length == 4);
        CHECK(L_C->v_array.obj_array()->components()[0] == *C);
        CHECK(L_C->v_array.obj_array()->components()[1] == *D);
        CHECK(L_C->v_array.obj_array()->components()[2] == *F);
        CHECK(L_C->v_array.obj_array()->components()[3] == *O);
    }
    Root<Array> arr_L_C(gc, vector_to_array(gc, L_C));

    // A (B, C)
    {
        Root<Array> r_linearizations(gc, make_array(gc, 3));
        r_linearizations->components()[0] = arr_L_B.value();
        r_linearizations->components()[1] = arr_L_C.value();
        r_linearizations->components()[2] = bases_A.value();
        append(gc, L_A, A);
        REQUIRE(c3_merge(gc, r_linearizations, L_A));
    }
    {
        REQUIRE(L_A->length == 7);
        CHECK(L_A->v_array.obj_array()->components()[0] == *A);
        CHECK(L_A->v_array.obj_array()->components()[1] == *B);
        CHECK(L_A->v_array.obj_array()->components()[2] == *C);
        CHECK(L_A->v_array.obj_array()->components()[3] == *D);
        CHECK(L_A->v_array.obj_array()->components()[4] == *E);
        CHECK(L_A->v_array.obj_array()->components()[5] == *F);
        CHECK(L_A->v_array.obj_array()->components()[6] == *O);
    }
}

TEST_CASE("c3 linearization", "[value-utils]")
{
    GC gc(1024 * 1024);

    auto make = [&gc](const std::string& name, Root<Array>& r_bases) {
        Root<String> r_name(gc, make_string(gc, name));
        OptionalRoot<Array> r_slots(gc, nullptr);
        return make_type(gc,
                         r_name,
                         r_bases,
                         /* sealed */ false,
                         /* kind */ Type::Kind::PRIMITIVE,
                         r_slots,
                         /* num_total_slots */ std::nullopt);
    };

    SECTION("successful linearization")
    {
        // This is essentially the same test setup as above in the "c3 merge" testcase.
        // >>> O = object
        // >>> class F(O): pass
        // >>> class E(O): pass
        // >>> class D(O): pass
        // >>> class C(D,F): pass
        // >>> class B(D,E): pass
        // >>> class A(B,C): pass

        Root<Array> bases_O(gc, make_array(gc, 0));
        Root<Type> O(gc, make("O", bases_O));

        Root<Array> bases_F(gc, make_array(gc, 1));
        bases_F->components()[0] = O.value();
        Root<Type> F(gc, make("F", bases_F));

        Root<Array> bases_E(gc, make_array(gc, 1));
        bases_E->components()[0] = O.value();
        Root<Type> E(gc, make("E", bases_E));

        Root<Array> bases_D(gc, make_array(gc, 1));
        bases_D->components()[0] = O.value();
        Root<Type> D(gc, make("D", bases_D));

        Root<Array> bases_C(gc, make_array(gc, 2));
        bases_C->components()[0] = D.value();
        bases_C->components()[1] = F.value();
        Root<Type> C(gc, make("C", bases_C));

        Root<Array> bases_B(gc, make_array(gc, 2));
        bases_B->components()[0] = D.value();
        bases_B->components()[1] = E.value();
        Root<Type> B(gc, make("B", bases_B));

        Root<Array> bases_A(gc, make_array(gc, 2));
        bases_A->components()[0] = B.value();
        bases_A->components()[1] = C.value();
        Root<Type> A(gc, make("A", bases_A));

        Array* L_A = A->v_linearization.obj_array();
        {
            REQUIRE(L_A->length == 7);
            CHECK(L_A->components()[0] == A.value());
            CHECK(L_A->components()[1] == B.value());
            CHECK(L_A->components()[2] == C.value());
            CHECK(L_A->components()[3] == D.value());
            CHECK(L_A->components()[4] == E.value());
            CHECK(L_A->components()[5] == F.value());
            CHECK(L_A->components()[6] == O.value());
        }
    }

    SECTION("failed linearization")
    {
        // Look for "therefore the method resolution order would be ambiguous in C" in
        // https://www.python.org/download/releases/2.3/mro/.
        // >>> O = object
        // >>> class X(O): pass
        // >>> class Y(O): pass
        // >>> class A(X,Y): pass
        // >>> class B(Y,X): pass
        // (Then trying to create `class C(A,B): pass` should fail.)

        Root<Array> bases_O(gc, make_array(gc, 0));
        Root<Type> O(gc, make("O", bases_O));

        Root<Array> bases_X(gc, make_array(gc, 1));
        bases_X->components()[0] = O.value();
        Root<Type> X(gc, make("X", bases_X));

        Root<Array> bases_Y(gc, make_array(gc, 1));
        bases_Y->components()[0] = O.value();
        Root<Type> Y(gc, make("Y", bases_Y));

        Root<Array> bases_A(gc, make_array(gc, 2));
        bases_A->components()[0] = X.value();
        bases_A->components()[1] = Y.value();
        Root<Type> A(gc, make("A", bases_A));

        Root<Array> bases_B(gc, make_array(gc, 2));
        bases_B->components()[0] = Y.value();
        bases_B->components()[1] = X.value();
        Root<Type> B(gc, make("B", bases_B));

        Root<Array> bases_C(gc, make_array(gc, 2));
        bases_C->components()[0] = A.value();
        bases_C->components()[1] = B.value();
        CHECK_THROWS_MATCHES(make("C", bases_C),
                             std::runtime_error,
                             Message("could not determine linearization of {type}"));
    }

    // TODO: check for recursive bases
    // TODO: check that subtypes are properly determined
}

// TODO: test the rest

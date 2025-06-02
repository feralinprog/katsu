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

    Root<Vector> bases_A(gc, make_vector(gc, 0));
    append(gc, bases_A, B);
    append(gc, bases_A, C);
    Root<Vector> bases_B(gc, make_vector(gc, 0));
    append(gc, bases_B, D);
    append(gc, bases_B, E);
    Root<Vector> bases_C(gc, make_vector(gc, 0));
    append(gc, bases_C, D);
    append(gc, bases_C, F);
    Root<Vector> bases_D(gc, make_vector(gc, 0));
    append(gc, bases_D, O);
    Root<Vector> bases_E(gc, make_vector(gc, 0));
    append(gc, bases_E, O);
    Root<Vector> bases_F(gc, make_vector(gc, 0));
    append(gc, bases_F, O);
    Root<Vector> bases_O(gc, make_vector(gc, 0));

    ValueRoot v_bases_A(gc, bases_A.value());
    ValueRoot v_bases_B(gc, bases_B.value());
    ValueRoot v_bases_C(gc, bases_C.value());
    ValueRoot v_bases_D(gc, bases_D.value());
    ValueRoot v_bases_E(gc, bases_E.value());
    ValueRoot v_bases_F(gc, bases_F.value());
    ValueRoot v_bases_O(gc, bases_O.value());

    Root<Vector> L_A(gc, make_vector(gc, 0));
    Root<Vector> L_B(gc, make_vector(gc, 0));
    Root<Vector> L_C(gc, make_vector(gc, 0));
    Root<Vector> L_D(gc, make_vector(gc, 0));
    Root<Vector> L_E(gc, make_vector(gc, 0));
    Root<Vector> L_F(gc, make_vector(gc, 0));
    Root<Vector> L_O(gc, make_vector(gc, 0));

    ValueRoot v_L_A(gc, L_A.value());
    ValueRoot v_L_B(gc, L_B.value());
    ValueRoot v_L_C(gc, L_C.value());
    ValueRoot v_L_D(gc, L_D.value());
    ValueRoot v_L_E(gc, L_E.value());
    ValueRoot v_L_F(gc, L_F.value());
    ValueRoot v_L_O(gc, L_O.value());

    // Start calculating linearizations in topological order:
    //   O -> D -> E -> F -> B -> C -> A
    // This doesn't necessarily match the final C3 linearization.
    // For each 'type', calculate the linearization by:
    // - start with the type
    // - c3_merge to extend with <the linearizations of the type's bases, and the bases themselves>

    // O
    {
        Root<Vector> r_linearizations(gc, make_vector(gc, 0));
        append(gc, r_linearizations, v_bases_O);
        append(gc, L_O, O);
        REQUIRE(c3_merge(gc, r_linearizations, L_O));
    }
    {
        REQUIRE(L_O->length == 1);
        CHECK(L_O->v_array.obj_array()->components()[0] == *O);
    }

    // D (O)
    {
        Root<Vector> r_linearizations(gc, make_vector(gc, 0));
        append(gc, r_linearizations, v_L_O);
        append(gc, r_linearizations, v_bases_D);
        append(gc, L_D, D);
        REQUIRE(c3_merge(gc, r_linearizations, L_D));
    }
    {
        REQUIRE(L_D->length == 2);
        CHECK(L_D->v_array.obj_array()->components()[0] == *D);
        CHECK(L_D->v_array.obj_array()->components()[1] == *O);
    }

    // E (O)
    {
        Root<Vector> r_linearizations(gc, make_vector(gc, 0));
        append(gc, r_linearizations, v_L_O);
        append(gc, r_linearizations, v_bases_E);
        append(gc, L_E, E);
        REQUIRE(c3_merge(gc, r_linearizations, L_E));
    }
    {
        REQUIRE(L_E->length == 2);
        CHECK(L_E->v_array.obj_array()->components()[0] == *E);
        CHECK(L_E->v_array.obj_array()->components()[1] == *O);
    }

    // F (O)
    {
        Root<Vector> r_linearizations(gc, make_vector(gc, 0));
        append(gc, r_linearizations, v_L_O);
        append(gc, r_linearizations, v_bases_F);
        append(gc, L_F, F);
        REQUIRE(c3_merge(gc, r_linearizations, L_F));
    }
    {
        REQUIRE(L_F->length == 2);
        CHECK(L_F->v_array.obj_array()->components()[0] == *F);
        CHECK(L_F->v_array.obj_array()->components()[1] == *O);
    }

    // B (D, E)
    {
        Root<Vector> r_linearizations(gc, make_vector(gc, 0));
        append(gc, r_linearizations, v_L_D);
        append(gc, r_linearizations, v_L_E);
        append(gc, r_linearizations, v_bases_B);
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

    // C (D, F)
    {
        Root<Vector> r_linearizations(gc, make_vector(gc, 0));
        append(gc, r_linearizations, v_L_D);
        append(gc, r_linearizations, v_L_F);
        append(gc, r_linearizations, v_bases_C);
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

    // A (B, C)
    {
        Root<Vector> r_linearizations(gc, make_vector(gc, 0));
        append(gc, r_linearizations, v_L_B);
        append(gc, r_linearizations, v_L_C);
        append(gc, r_linearizations, v_bases_A);
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

    auto make = [&gc](const std::string& name, Root<Vector>& r_bases) {
        Root<String> r_name(gc, make_string(gc, name));
        OptionalRoot<Vector> r_slots(gc, nullptr);
        return make_type(gc,
                         r_name,
                         r_bases,
                         /* sealed */ false,
                         /* kind */ Type::Kind::PRIMITIVE,
                         r_slots);
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

        Root<Vector> bases_O(gc, make_vector(gc, 0));
        Root<Type> O(gc, make("O", bases_O));
        ValueRoot v_O(gc, O.value());

        Root<Vector> bases_F(gc, make_vector(gc, 1));
        append(gc, bases_F, v_O);
        Root<Type> F(gc, make("F", bases_F));
        ValueRoot v_F(gc, F.value());

        Root<Vector> bases_E(gc, make_vector(gc, 1));
        append(gc, bases_E, v_O);
        Root<Type> E(gc, make("E", bases_E));
        ValueRoot v_E(gc, E.value());

        Root<Vector> bases_D(gc, make_vector(gc, 1));
        append(gc, bases_D, v_O);
        Root<Type> D(gc, make("D", bases_D));
        ValueRoot v_D(gc, D.value());

        Root<Vector> bases_C(gc, make_vector(gc, 2));
        append(gc, bases_C, v_D);
        append(gc, bases_C, v_F);
        Root<Type> C(gc, make("C", bases_C));
        ValueRoot v_C(gc, C.value());

        Root<Vector> bases_B(gc, make_vector(gc, 2));
        append(gc, bases_B, v_D);
        append(gc, bases_B, v_E);
        Root<Type> B(gc, make("B", bases_B));
        ValueRoot v_B(gc, B.value());

        Root<Vector> bases_A(gc, make_vector(gc, 2));
        append(gc, bases_A, v_B);
        append(gc, bases_A, v_C);
        Root<Type> A(gc, make("A", bases_A));

        Vector* L_A = A->v_linearization.obj_vector();
        {
            REQUIRE(L_A->length == 7);
            CHECK(L_A->v_array.obj_array()->components()[0] == A.value());
            CHECK(L_A->v_array.obj_array()->components()[1] == B.value());
            CHECK(L_A->v_array.obj_array()->components()[2] == C.value());
            CHECK(L_A->v_array.obj_array()->components()[3] == D.value());
            CHECK(L_A->v_array.obj_array()->components()[4] == E.value());
            CHECK(L_A->v_array.obj_array()->components()[5] == F.value());
            CHECK(L_A->v_array.obj_array()->components()[6] == O.value());
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

        Root<Vector> bases_O(gc, make_vector(gc, 0));
        Root<Type> O(gc, make("O", bases_O));
        ValueRoot v_O(gc, O.value());

        Root<Vector> bases_X(gc, make_vector(gc, 1));
        append(gc, bases_X, v_O);
        Root<Type> X(gc, make("X", bases_X));
        ValueRoot v_X(gc, X.value());

        Root<Vector> bases_Y(gc, make_vector(gc, 1));
        append(gc, bases_Y, v_O);
        Root<Type> Y(gc, make("Y", bases_Y));
        ValueRoot v_Y(gc, Y.value());

        Root<Vector> bases_A(gc, make_vector(gc, 2));
        append(gc, bases_A, v_X);
        append(gc, bases_A, v_Y);
        Root<Type> A(gc, make("A", bases_A));
        ValueRoot v_A(gc, A.value());

        Root<Vector> bases_B(gc, make_vector(gc, 2));
        append(gc, bases_B, v_Y);
        append(gc, bases_B, v_X);
        Root<Type> B(gc, make("B", bases_B));
        ValueRoot v_B(gc, B.value());

        Root<Vector> bases_C(gc, make_vector(gc, 2));
        append(gc, bases_C, v_A);
        append(gc, bases_C, v_B);
        CHECK_THROWS_MATCHES(make("C", bases_C),
                             std::runtime_error,
                             Message("could not determine linearization of {type}"));
    }

    // TODO: check for recursive bases
    // TODO: check that subtypes are properly determined
}

// TODO: test the rest

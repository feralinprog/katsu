#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

#include "value.h"

using namespace Catch::Matchers;
using namespace Katsu;

TEST_CASE("fixnum tagging/untagging", "[value]")
{
    // Simple cases:
    CHECK(Value::fixnum(0).value<int64_t>() == 0);
    CHECK(Value::fixnum(-1).value<int64_t>() == -1);
    CHECK(Value::fixnum(1).value<int64_t>() == 1);
    CHECK(Value::fixnum(10).value<int64_t>() == 10);
    CHECK(Value::fixnum(-10).value<int64_t>() == -10);

    // Edge cases:
    CHECK(Value::fixnum(FIXNUM_MAX).value<int64_t>() == FIXNUM_MAX);
    CHECK(Value::fixnum(FIXNUM_MIN).value<int64_t>() == FIXNUM_MIN);
    CHECK(Value::fixnum(FIXNUM_MAX - 1).value<int64_t>() == FIXNUM_MAX - 1);
    CHECK(Value::fixnum(FIXNUM_MIN + 1).value<int64_t>() == FIXNUM_MIN + 1);
    CHECK_THROWS_AS(Value::fixnum(FIXNUM_MAX + 1), std::out_of_range);
    CHECK_THROWS_AS(Value::fixnum(FIXNUM_MIN - 1), std::out_of_range);
    CHECK_THROWS_AS(Value::fixnum(INT64_MAX), std::out_of_range);
    CHECK_THROWS_AS(Value::fixnum(INT64_MIN), std::out_of_range);

    // Check the exception message once:
    CHECK_THROWS_MATCHES(Value::fixnum(FIXNUM_MAX + 1),
                         std::out_of_range,
                         Message("input is too large an integer to be represented as a fixnum"));
}

TEST_CASE("bool tagging/untagging", "[value]")
{
    CHECK(Value::_bool(true).value<bool>() == true);
    CHECK(Value::_bool(false).value<bool>() == false);
}

TEST_CASE("null tagging/untagging", "[value]")
{
    CHECK_NOTHROW(Value::null().value<Null>());
}

TEST_CASE("tuple tagging/untagging", "[value]")
{
    // Note: already aligned due to definition of ValueContents.
    Tuple aligned{.v_length = Value::fixnum(0)};
    CHECK(Value::tuple(&aligned).value<Tuple*>() == &aligned);

    // Test all possible misalignments:
    for (uint64_t offset = 1; offset < (1 << TAG_BITS); offset++) {
        Tuple* unaligned = reinterpret_cast<Tuple*>(reinterpret_cast<uint8_t*>(&aligned) + offset);
        CHECK_THROWS_MATCHES(Value::tuple(unaligned),
                             std::invalid_argument,
                             Message("tuple pointer is not TAG_BITS-aligned"));
    }
}

// TODO: other value kinds: Vector, String, Closure, Type, DataclassInstance

// Generic helper to create an example Value of any given tag.
template <typename T> Value make();
template <> Value make<int64_t>()
{
    return Value::fixnum(0);
}
template <> Value make<bool>()
{
    return Value::_bool(false);
}
template <> Value make<Null>()
{
    return Value::null();
}
template <> Value make<Tuple*>()
{
    return Value::tuple(static_cast<Tuple*>(nullptr));
}
template <> Value make<Vector*>()
{
    return Value::vector(static_cast<Vector*>(nullptr));
}
template <> Value make<String*>()
{
    return Value::string(static_cast<String*>(nullptr));
}
template <> Value make<Closure*>()
{
    return Value::closure(static_cast<Closure*>(nullptr));
}
template <> Value make<Type*>()
{
    return Value::type(static_cast<Type*>(nullptr));
}
template <> Value make<DataclassInstance*>()
{
    return Value::data_inst(static_cast<DataclassInstance*>(nullptr));
}

#define EACH_TYPE(F) \
    (F(int64_t),     \
     F(bool),        \
     F(Null),        \
     F(Tuple*),      \
     F(Vector*),     \
     F(String*),     \
     F(Closure*),    \
     F(Type*),       \
     F(DataclassInstance*))

// Curried pair type.
template <typename A> struct Pair
{
    template <typename B> struct And
    {
        using T1 = A;
        using T2 = B;
    };
};

#define PAIR_LEFT(T) Pair<T>::And
#define PAIR_RIGHT(T) T

TEMPLATE_PRODUCT_TEST_CASE("tags keep track of underlying value type", "[value]",
                           EACH_TYPE(PAIR_LEFT), EACH_TYPE(PAIR_RIGHT))
{
    using T1 = typename TestType::T1;
    using T2 = typename TestType::T2;

    Value v = make<T1>();

    if (std::is_same<T1, T2>::value) {
        CHECK_NOTHROW(v.value<T2>());
    } else {
        CHECK_THROWS_MATCHES(v.value<T2>(),
                             std::runtime_error,
                             MessageMatches(StartsWith("expected ")));
    }
}

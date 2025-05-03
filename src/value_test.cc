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

TEST_CASE("float tagging/untagging", "[value]")
{
    CHECK(Value::_float(0.0f).value<float>() == 0.0f);
    CHECK(Value::_float(1.234f).value<float>() == 1.234f);
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

TEST_CASE("object tagging/untagging", "[value]")
{
    // Note: already aligned due to definition of Object.
    Object* aligned = reinterpret_cast<Object*>(aligned_alloc(1 << TAG_BITS, sizeof(Object)));
    CHECK(Value::object(aligned).value<Object*>() == aligned);

    // Test all possible misalignments:
    for (uint64_t offset = 1; offset < (1 << TAG_BITS); offset++) {
        Object* unaligned = reinterpret_cast<Object*>(reinterpret_cast<uint8_t*>(aligned) + offset);
        CHECK_THROWS_MATCHES(Value::object(unaligned),
                             std::invalid_argument,
                             Message("object pointer is not TAG_BITS-aligned"));
    }

    free(aligned);
}

// Generic helper to create an example Value of any given tag.
template <typename T> Value make();
template <> Value make<int64_t>()
{
    return Value::fixnum(0);
}
template <> Value make<float>()
{
    return Value::_float(0.0);
}
template <> Value make<bool>()
{
    return Value::_bool(false);
}
template <> Value make<Null>()
{
    return Value::null();
}
template <> Value make<Object*>()
{
    return Value::object(static_cast<Object*>(nullptr));
}

#define EACH_TYPE(F) (F(int64_t), F(bool), F(Null), F(Object*))

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

TEST_CASE("inline vs. non-inline tags", "[value]")
{
    CHECK(make<int64_t>().is_inline());
    CHECK(make<float>().is_inline());
    CHECK(make<bool>().is_inline());
    CHECK(make<Null>().is_inline());
    CHECK_FALSE(make<Object*>().is_inline());

    CHECK_FALSE(make<int64_t>().is_ref());
    CHECK_FALSE(make<float>().is_ref());
    CHECK_FALSE(make<bool>().is_ref());
    CHECK_FALSE(make<Null>().is_ref());
    CHECK(make<Object*>().is_ref());
}

TEST_CASE("object header distinguishes forwarding vs. reference types", "[object]")
{
    Object& obj = *reinterpret_cast<Object*>(malloc(sizeof(Object)));

    REQUIRE_NOTHROW(obj.set_forwarding(reinterpret_cast<void*>(0x1234)));
    CHECK(obj.is_forwarding());
    CHECK_FALSE(obj.is_object());
    CHECK(obj.forwarding() == reinterpret_cast<void*>(0x1234));
    CHECK_THROWS_MATCHES(obj.tag(), std::logic_error, Message("not an object"));

    // VECTOR is arbitrary.
    REQUIRE_NOTHROW(obj.set_object(ObjectTag::VECTOR));
    CHECK_FALSE(obj.is_forwarding());
    CHECK(obj.is_object());
    CHECK_THROWS_MATCHES(obj.forwarding(), std::logic_error, Message("not a forwarding pointer"));
    CHECK(obj.tag() == ObjectTag::VECTOR);

    free(&obj);
}

TEST_CASE("object() helper checks object tags", "[object]")
{
    Object& obj = *reinterpret_cast<Object*>(malloc(sizeof(Object)));

    {
        REQUIRE_NOTHROW(obj.set_object(ObjectTag::TUPLE));
        CHECK(obj.object<Tuple*>() == reinterpret_cast<Tuple*>(&obj));
        CHECK_THROWS_MATCHES(obj.object<Vector*>(), std::runtime_error, Message("expected vector"));
        CHECK_THROWS_MATCHES(obj.object<String*>(), std::runtime_error, Message("expected string"));
        CHECK_THROWS_MATCHES(obj.object<Closure*>(),
                             std::runtime_error,
                             Message("expected closure"));
        CHECK_THROWS_MATCHES(obj.object<Type*>(), std::runtime_error, Message("expected type"));
        CHECK_THROWS_MATCHES(obj.object<DataclassInstance*>(),
                             std::runtime_error,
                             Message("expected dataclass instance"));
    }

    {
        REQUIRE_NOTHROW(obj.set_object(ObjectTag::VECTOR));
        CHECK_THROWS_MATCHES(obj.object<Tuple*>(), std::runtime_error, Message("expected tuple"));
        CHECK(obj.object<Vector*>() == reinterpret_cast<Vector*>(&obj));
        CHECK_THROWS_MATCHES(obj.object<String*>(), std::runtime_error, Message("expected string"));
        CHECK_THROWS_MATCHES(obj.object<Closure*>(),
                             std::runtime_error,
                             Message("expected closure"));
        CHECK_THROWS_MATCHES(obj.object<Type*>(), std::runtime_error, Message("expected type"));
        CHECK_THROWS_MATCHES(obj.object<DataclassInstance*>(),
                             std::runtime_error,
                             Message("expected dataclass instance"));
    }

    {
        REQUIRE_NOTHROW(obj.set_object(ObjectTag::STRING));
        CHECK_THROWS_MATCHES(obj.object<Tuple*>(), std::runtime_error, Message("expected tuple"));
        CHECK_THROWS_MATCHES(obj.object<Vector*>(), std::runtime_error, Message("expected vector"));
        CHECK(obj.object<String*>() == reinterpret_cast<String*>(&obj));
        CHECK_THROWS_MATCHES(obj.object<Closure*>(),
                             std::runtime_error,
                             Message("expected closure"));
        CHECK_THROWS_MATCHES(obj.object<Type*>(), std::runtime_error, Message("expected type"));
        CHECK_THROWS_MATCHES(obj.object<DataclassInstance*>(),
                             std::runtime_error,
                             Message("expected dataclass instance"));
    }

    {
        REQUIRE_NOTHROW(obj.set_object(ObjectTag::CLOSURE));
        CHECK_THROWS_MATCHES(obj.object<Tuple*>(), std::runtime_error, Message("expected tuple"));
        CHECK_THROWS_MATCHES(obj.object<Vector*>(), std::runtime_error, Message("expected vector"));
        CHECK_THROWS_MATCHES(obj.object<String*>(), std::runtime_error, Message("expected string"));
        CHECK(obj.object<Closure*>() == reinterpret_cast<Closure*>(&obj));
        CHECK_THROWS_MATCHES(obj.object<Type*>(), std::runtime_error, Message("expected type"));
        CHECK_THROWS_MATCHES(obj.object<DataclassInstance*>(),
                             std::runtime_error,
                             Message("expected dataclass instance"));
    }

    {
        REQUIRE_NOTHROW(obj.set_object(ObjectTag::TYPE));
        CHECK_THROWS_MATCHES(obj.object<Tuple*>(), std::runtime_error, Message("expected tuple"));
        CHECK_THROWS_MATCHES(obj.object<Vector*>(), std::runtime_error, Message("expected vector"));
        CHECK_THROWS_MATCHES(obj.object<String*>(), std::runtime_error, Message("expected string"));
        CHECK_THROWS_MATCHES(obj.object<Closure*>(),
                             std::runtime_error,
                             Message("expected closure"));
        CHECK(obj.object<Type*>() == reinterpret_cast<Type*>(&obj));
        CHECK_THROWS_MATCHES(obj.object<DataclassInstance*>(),
                             std::runtime_error,
                             Message("expected dataclass instance"));
    }

    {
        REQUIRE_NOTHROW(obj.set_object(ObjectTag::INSTANCE));
        CHECK_THROWS_MATCHES(obj.object<Tuple*>(), std::runtime_error, Message("expected tuple"));
        CHECK_THROWS_MATCHES(obj.object<Vector*>(), std::runtime_error, Message("expected vector"));
        CHECK_THROWS_MATCHES(obj.object<String*>(), std::runtime_error, Message("expected string"));
        CHECK_THROWS_MATCHES(obj.object<Closure*>(),
                             std::runtime_error,
                             Message("expected closure"));
        CHECK_THROWS_MATCHES(obj.object<Type*>(), std::runtime_error, Message("expected type"));
        CHECK(obj.object<DataclassInstance*>() == reinterpret_cast<DataclassInstance*>(&obj));
    }

    free(&obj);
}

// TODO: test functions of Tuple
// TODO: test functions of Vector
// TODO: test functions of String
// TODO: test functions of Closure
// TODO: test functions of Type
// TODO: test functions of DataclassInstance

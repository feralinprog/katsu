#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

#include "value.h"

using namespace Catch::Matchers;
using namespace Katsu;

TEST_CASE("fixnum tagging/untagging", "[value]")
{
    // Simple cases:
    CHECK(Value::fixnum(0).fixnum() == 0);
    CHECK(Value::fixnum(-1).fixnum() == -1);
    CHECK(Value::fixnum(1).fixnum() == 1);
    CHECK(Value::fixnum(10).fixnum() == 10);
    CHECK(Value::fixnum(-10).fixnum() == -10);

    // Edge cases:
    CHECK(Value::fixnum(FIXNUM_MAX).fixnum() == FIXNUM_MAX);
    CHECK(Value::fixnum(FIXNUM_MIN).fixnum() == FIXNUM_MIN);
    CHECK(Value::fixnum(FIXNUM_MAX - 1).fixnum() == FIXNUM_MAX - 1);
    CHECK(Value::fixnum(FIXNUM_MIN + 1).fixnum() == FIXNUM_MIN + 1);
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
    CHECK(Value::_float(0.0f)._float() == 0.0f);
    CHECK(Value::_float(1.234f)._float() == 1.234f);
}

TEST_CASE("bool tagging/untagging", "[value]")
{
    CHECK(Value::_bool(true)._bool() == true);
    CHECK(Value::_bool(false)._bool() == false);
}

TEST_CASE("null tagging/untagging", "[value]")
{
    CHECK_NOTHROW(Value::null().value<Null>());
}

TEST_CASE("object tagging/untagging", "[value]")
{
    // Note: already aligned due to definition of Object.
    Object* aligned = reinterpret_cast<Object*>(aligned_alloc(1 << TAG_BITS, sizeof(Object)));
    CHECK(Value::object(aligned).object() == aligned);

    // Test all possible misalignments:
    for (uint64_t offset = 1; offset < (1 << TAG_BITS); offset++) {
        Object* unaligned = reinterpret_cast<Object*>(reinterpret_cast<uint8_t*>(aligned) + offset);
        CHECK_THROWS_MATCHES(Value::object(unaligned),
                             std::invalid_argument,
                             Message("object pointer is not TAG_BITS-aligned"));
    }

    free(aligned);
}

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

TEMPLATE_PRODUCT_TEST_CASE("tags keep track of underlying value type", "[value]",
                           EACH_TYPE(PAIR_LEFT), EACH_TYPE(PAIR_RIGHT))
{
    using T1 = typename TestType::T1;
    using T2 = typename TestType::T2;

    Value v = make<T1>();

    if (std::is_same<T1, T2>::value) {
        CHECK_NOTHROW(v.value<T2>());
    } else {
        std::stringstream ss;
        ss << "expected " << tag_str(make<T2>().tag());
        CHECK_THROWS_MATCHES(v.value<T2>(), std::runtime_error, Message(ss.str()));
    }
}

TEST_CASE("inline vs. non-inline tags", "[value]")
{
    CHECK(make<int64_t>().is_inline());
    CHECK(make<float>().is_inline());
    CHECK(make<bool>().is_inline());
    CHECK(make<Null>().is_inline());
    CHECK_FALSE(make<Object*>().is_inline());
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

#define EACH_OBJECT(F) \
    (F(Ref),           \
     F(Tuple),         \
     F(Vector),        \
     F(Module),        \
     F(String),        \
     F(Code),          \
     F(Closure),       \
     F(Method),        \
     F(MultiMethod),   \
     F(Type),          \
     F(DataclassInstance))

TEMPLATE_PRODUCT_TEST_CASE("object() helper checks object tags", "[object]", EACH_OBJECT(PAIR_LEFT),
                           EACH_OBJECT(PAIR_RIGHT))
{
    using T1 = typename TestType::T1;
    using T2 = typename TestType::T2;

    Object& obj = *reinterpret_cast<Object*>(malloc(sizeof(Object)));
    REQUIRE_NOTHROW(obj.set_object(T1::CLASS_TAG));

    if (std::is_same<T1, T2>::value) {
        CHECK(obj.object<T2*>() == reinterpret_cast<T2*>(&obj));
    } else {
        // TODO: check full error message
        std::stringstream ss;
        ss << "expected " << object_tag_str(T2::CLASS_TAG);
        CHECK_THROWS_MATCHES(obj.object<T2*>(), std::runtime_error, Message(ss.str()));
    }

    free(&obj);
}

// TODO: test functions of Ref
// TODO: test functions of Tuple
// TODO: test functions of Vector
// TODO: test functions of Module
// TODO: test functions of String
// TODO: test functions of Code
// TODO: test functions of Closure
// TODO: test functions of Method
// TODO: test functions of MultiMethod
// TODO: test functions of Type
// TODO: test functions of DataclassInstance

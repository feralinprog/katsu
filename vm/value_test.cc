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
    CHECK_THROWS_MATCHES(
        Value::fixnum(FIXNUM_MAX + 1),
        std::out_of_range,
        MessageMatches(EndsWith("input is too large an integer to be represented as a fixnum")));
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
                             MessageMatches(EndsWith("object pointer is not TAG_BITS-aligned")));
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

#define EACH_TYPE_INNER(F, LEFT) \
    F(LEFT, int64_t)             \
    F(LEFT, bool)                \
    F(LEFT, Null)                \
    F(LEFT, Object*)

#define EACH_TYPE_OUTER(INNER, F) \
    INNER(F, int64_t)             \
    INNER(F, bool)                \
    INNER(F, Null)                \
    INNER(F, Object*)

#define EACH_TYPE_PAIR(F) EACH_TYPE_OUTER(EACH_TYPE_INNER, F)

TEST_CASE("tags keep track of underlying value type", "[value]")
{
#define TAG_SECTION(T1, T2)                                                          \
    SECTION(#T1 " and " #T2)                                                         \
    {                                                                                \
        Value v = make<T1>();                                                        \
                                                                                     \
        if (std::is_same<T1, T2>::value) {                                           \
            CHECK_NOTHROW(v.value<T2>());                                            \
        } else {                                                                     \
            std::stringstream ss;                                                    \
            ss << "ASSERT(value.tag() == Tag::" << TAG_STR(make<T2>().tag()) << ")"; \
            CHECK_THROWS_MATCHES(v.value<T2>(),                                      \
                                 std::logic_error,                                   \
                                 MessageMatches(EndsWith(ss.str())));                \
        }                                                                            \
    }

    EACH_TYPE_PAIR(TAG_SECTION);
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
    CHECK_THROWS_MATCHES(obj.tag(),
                         std::logic_error,
                         MessageMatches(EndsWith("ASSERT(this->is_object())")));

    // VECTOR is arbitrary.
    REQUIRE_NOTHROW(obj.set_object(ObjectTag::VECTOR));
    CHECK_FALSE(obj.is_forwarding());
    CHECK(obj.is_object());
    CHECK_THROWS_MATCHES(obj.forwarding(),
                         std::logic_error,
                         MessageMatches(EndsWith("ASSERT(this->is_forwarding())")));
    CHECK(obj.tag() == ObjectTag::VECTOR);

    free(&obj);
}

#define EACH_OBJECT_INNER(F, LEFT) \
    F(LEFT, Ref)                   \
    F(LEFT, Tuple)                 \
    F(LEFT, Array)                 \
    F(LEFT, Vector)                \
    F(LEFT, Assoc)                 \
    F(LEFT, String)                \
    F(LEFT, Code)                  \
    F(LEFT, Closure)               \
    F(LEFT, Method)                \
    F(LEFT, MultiMethod)           \
    F(LEFT, Type)                  \
    F(LEFT, DataclassInstance)     \
    F(LEFT, CallSegment)           \
    F(LEFT, ForeignValue)          \
    F(LEFT, ByteArray)

#define EACH_OBJECT_OUTER(INNER, F) \
    INNER(F, Ref)                   \
    INNER(F, Tuple)                 \
    INNER(F, Array)                 \
    INNER(F, Vector)                \
    INNER(F, Assoc)                 \
    INNER(F, String)                \
    INNER(F, Code)                  \
    INNER(F, Closure)               \
    INNER(F, Method)                \
    INNER(F, MultiMethod)           \
    INNER(F, Type)                  \
    INNER(F, DataclassInstance)     \
    INNER(F, CallSegment)           \
    INNER(F, ForeignValue)          \
    INNER(F, ByteArray)

#define EACH_OBJECT_PAIR(F) EACH_OBJECT_OUTER(EACH_OBJECT_INNER, F)

TEST_CASE("object() helper checks object tags", "[object]")
{
#define OBJECT_TAG_SECTION(T1, T2)                                                              \
    SECTION(#T1 " and " #T2)                                                                    \
    {                                                                                           \
        Object& obj = *reinterpret_cast<Object*>(malloc(sizeof(Object)));                       \
        REQUIRE_NOTHROW(obj.set_object(T1::CLASS_TAG));                                         \
                                                                                                \
        if (std::is_same<T1, T2>::value) {                                                      \
            CHECK(obj.object<T2*>() == reinterpret_cast<T2*>(&obj));                            \
        } else {                                                                                \
            std::stringstream ss;                                                               \
            ss << "ASSERT(object.tag() == ObjectTag::" << OBJECT_TAG_STR(T2::CLASS_TAG) << ")"; \
            CHECK_THROWS_MATCHES(obj.object<T2*>(),                                             \
                                 std::logic_error,                                              \
                                 MessageMatches(EndsWith(ss.str())));                           \
        }                                                                                       \
                                                                                                \
        free(&obj);                                                                             \
    }

    EACH_OBJECT_PAIR(OBJECT_TAG_SECTION);
}

// TODO: test functions of Ref
// TODO: test functions of Tuple
// TODO: test functions of Array
// TODO: test functions of Vector
// TODO: test functions of Assoc
// TODO: test functions of String
// TODO: test functions of Code
// TODO: test functions of Closure
// TODO: test functions of Method
// TODO: test functions of MultiMethod
// TODO: test functions of Type
// TODO: test functions of DataclassInstance
// TODO: test functions of CallSegment
// TODO: test functions of ForeignValue
// TODO: test functions of ByteArray

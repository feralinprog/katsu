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

#define EACH_TYPE_PAIR(F) \
    F(int64_t, int64_t)   \
    F(int64_t, bool)      \
    F(int64_t, Null)      \
    F(int64_t, Object*)   \
    F(bool, int64_t)      \
    F(bool, bool)         \
    F(bool, Null)         \
    F(bool, Object*)      \
    F(Null, int64_t)      \
    F(Null, bool)         \
    F(Null, Null)         \
    F(Null, Object*)      \
    F(Object*, int64_t)   \
    F(Object*, bool)      \
    F(Object*, Null)      \
    F(Object*, Object*)

#define TAG_TESTCASE(T1, T2)                                                            \
    TEST_CASE("tags keep track of underlying value type - " #T1 " and " #T2, "[value]") \
    {                                                                                   \
        Value v = make<T1>();                                                           \
                                                                                        \
        if (std::is_same<T1, T2>::value) {                                              \
            CHECK_NOTHROW(v.value<T2>());                                               \
        } else {                                                                        \
            std::stringstream ss;                                                       \
            ss << "expected " << tag_str(make<T2>().tag());                             \
            CHECK_THROWS_MATCHES(v.value<T2>(), std::runtime_error, Message(ss.str())); \
        }                                                                               \
    }

EACH_TYPE_PAIR(TAG_TESTCASE)

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

#define EACH_OBJECT_PAIR(F)           \
    F(Ref, Ref)                       \
    F(Ref, Tuple)                     \
    F(Ref, Array)                     \
    F(Ref, Vector)                    \
    F(Ref, Module)                    \
    F(Ref, String)                    \
    F(Ref, Code)                      \
    F(Ref, Closure)                   \
    F(Ref, Method)                    \
    F(Ref, MultiMethod)               \
    F(Ref, Type)                      \
    F(Ref, DataclassInstance)         \
    F(Tuple, Ref)                     \
    F(Tuple, Tuple)                   \
    F(Tuple, Array)                   \
    F(Tuple, Vector)                  \
    F(Tuple, Module)                  \
    F(Tuple, String)                  \
    F(Tuple, Code)                    \
    F(Tuple, Closure)                 \
    F(Tuple, Method)                  \
    F(Tuple, MultiMethod)             \
    F(Tuple, Type)                    \
    F(Tuple, DataclassInstance)       \
    F(Array, Ref)                     \
    F(Array, Tuple)                   \
    F(Array, Array)                   \
    F(Array, Vector)                  \
    F(Array, Module)                  \
    F(Array, String)                  \
    F(Array, Code)                    \
    F(Array, Closure)                 \
    F(Array, Method)                  \
    F(Array, MultiMethod)             \
    F(Array, Type)                    \
    F(Array, DataclassInstance)       \
    F(Vector, Ref)                    \
    F(Vector, Tuple)                  \
    F(Vector, Array)                  \
    F(Vector, Vector)                 \
    F(Vector, Module)                 \
    F(Vector, String)                 \
    F(Vector, Code)                   \
    F(Vector, Closure)                \
    F(Vector, Method)                 \
    F(Vector, MultiMethod)            \
    F(Vector, Type)                   \
    F(Vector, DataclassInstance)      \
    F(Module, Ref)                    \
    F(Module, Tuple)                  \
    F(Module, Array)                  \
    F(Module, Vector)                 \
    F(Module, Module)                 \
    F(Module, String)                 \
    F(Module, Code)                   \
    F(Module, Closure)                \
    F(Module, Method)                 \
    F(Module, MultiMethod)            \
    F(Module, Type)                   \
    F(Module, DataclassInstance)      \
    F(String, Ref)                    \
    F(String, Tuple)                  \
    F(String, Array)                  \
    F(String, Vector)                 \
    F(String, Module)                 \
    F(String, String)                 \
    F(String, Code)                   \
    F(String, Closure)                \
    F(String, Method)                 \
    F(String, MultiMethod)            \
    F(String, Type)                   \
    F(String, DataclassInstance)      \
    F(Code, Ref)                      \
    F(Code, Tuple)                    \
    F(Code, Array)                    \
    F(Code, Vector)                   \
    F(Code, Module)                   \
    F(Code, String)                   \
    F(Code, Code)                     \
    F(Code, Closure)                  \
    F(Code, Method)                   \
    F(Code, MultiMethod)              \
    F(Code, Type)                     \
    F(Code, DataclassInstance)        \
    F(Closure, Ref)                   \
    F(Closure, Tuple)                 \
    F(Closure, Array)                 \
    F(Closure, Vector)                \
    F(Closure, Module)                \
    F(Closure, String)                \
    F(Closure, Code)                  \
    F(Closure, Closure)               \
    F(Closure, Method)                \
    F(Closure, MultiMethod)           \
    F(Closure, Type)                  \
    F(Closure, DataclassInstance)     \
    F(Method, Ref)                    \
    F(Method, Tuple)                  \
    F(Method, Array)                  \
    F(Method, Vector)                 \
    F(Method, Module)                 \
    F(Method, String)                 \
    F(Method, Code)                   \
    F(Method, Closure)                \
    F(Method, Method)                 \
    F(Method, MultiMethod)            \
    F(Method, Type)                   \
    F(Method, DataclassInstance)      \
    F(MultiMethod, Ref)               \
    F(MultiMethod, Tuple)             \
    F(MultiMethod, Array)             \
    F(MultiMethod, Vector)            \
    F(MultiMethod, Module)            \
    F(MultiMethod, String)            \
    F(MultiMethod, Code)              \
    F(MultiMethod, Closure)           \
    F(MultiMethod, Method)            \
    F(MultiMethod, MultiMethod)       \
    F(MultiMethod, Type)              \
    F(MultiMethod, DataclassInstance) \
    F(Type, Ref)                      \
    F(Type, Tuple)                    \
    F(Type, Array)                    \
    F(Type, Vector)                   \
    F(Type, Module)                   \
    F(Type, String)                   \
    F(Type, Code)                     \
    F(Type, Closure)                  \
    F(Type, Method)                   \
    F(Type, MultiMethod)              \
    F(Type, Type)                     \
    F(Type, DataclassInstance)        \
    F(DataclassInstance, Ref)         \
    F(DataclassInstance, Tuple)       \
    F(DataclassInstance, Array)       \
    F(DataclassInstance, Vector)      \
    F(DataclassInstance, Module)      \
    F(DataclassInstance, String)      \
    F(DataclassInstance, Code)        \
    F(DataclassInstance, Closure)     \
    F(DataclassInstance, Method)      \
    F(DataclassInstance, MultiMethod) \
    F(DataclassInstance, Type)        \
    F(DataclassInstance, DataclassInstance)

#define OBJECT_TAG_TESTCASE(T1, T2)                                                         \
    TEST_CASE("object() helper checks object tags - " #T1 " and " #T2, "[object]")          \
    {                                                                                       \
        Object& obj = *reinterpret_cast<Object*>(malloc(sizeof(Object)));                   \
        REQUIRE_NOTHROW(obj.set_object(T1::CLASS_TAG));                                     \
                                                                                            \
        if (std::is_same<T1, T2>::value) {                                                  \
            CHECK(obj.object<T2*>() == reinterpret_cast<T2*>(&obj));                        \
        } else {                                                                            \
            std::stringstream ss;                                                           \
            ss << "expected " << object_tag_str(T2::CLASS_TAG);                             \
            CHECK_THROWS_MATCHES(obj.object<T2*>(), std::runtime_error, Message(ss.str())); \
        }                                                                                   \
                                                                                            \
        free(&obj);                                                                         \
    }

EACH_OBJECT_PAIR(OBJECT_TAG_TESTCASE)

// TODO: test functions of Ref
// TODO: test functions of Tuple
// TODO: test functions of Array
// TODO: test functions of Vector
// TODO: test functions of Module
// TODO: test functions of String
// TODO: test functions of Code
// TODO: test functions of Closure
// TODO: test functions of Method
// TODO: test functions of MultiMethod
// TODO: test functions of Type
// TODO: test functions of DataclassInstance

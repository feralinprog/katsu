#pragma once

#include <cstdint>
#include <stdexcept>

namespace Katsu
{
    /*
     * Values are 64-bit tagged representations of various objects in Katsu:
     * - small integers (fixnums)
     * - booleans
     * - tuples
     * - vectors
     * - strings
     * - closures
     * - type values
     * - dataclass instances
     *
     * Some small objects (e.g. fixnums, bools, null) are stored inline; others are represented as
     * tagged pointers to the actual contents (subclasses of ValueContents) elsewhere in memory that
     * is managed by the garbage collector.
     *
     * Functions / methods are mostly implemented in the header here to support inlining.
     */

    enum class Tag
    {
        FIXNUM,
        BOOL,
        _NULL,
        TUPLE,
        VECTOR,
        STRING,
        CLOSURE,
        TYPE,
        INSTANCE,

        NUM_TAGS
    };

    const std::size_t TAG_BITS = 4;
    const std::size_t INLINE_BITS = 64 - TAG_BITS;
    const uint64_t TAG_MASK = (1LL << TAG_BITS) - 1;
    static_assert(static_cast<int>(Tag::NUM_TAGS) <= (1 << TAG_BITS));

    const int64_t FIXNUM_MAX = (1LL << (INLINE_BITS - 1)) - 1;
    const int64_t FIXNUM_MIN = -(1LL << (INLINE_BITS - 1));
    // Masks out the upper INLINE_BITS number of bits.
    const uint64_t FIXNUM_MASK = ~(TAG_MASK << INLINE_BITS);
    static_assert((FIXNUM_MASK << TAG_BITS) + TAG_MASK == UINT64_MAX);

    // Template declaration here, since it's used in `value()` below. Specializations are defined
    // later.
    struct Value;
    template <typename T> T static_value(Value value);

    // Forward declarations for helper functions:
    struct Tuple;
    struct Vector;
    struct String;
    struct Closure;
    struct Type;
    struct DataclassInstance;

    // Singleton as effectively a named replacement for std::monostate.
    struct Null
    {
    };

    struct Value
    {
    private:
        uint64_t tagged;

        // Does not perform any bounds checks on `tag` or `value`.
        Value(Tag tag, uint64_t value)
            : tagged((value << TAG_BITS) | static_cast<uint64_t>(tag))
        {}

    public:
        // Calculate the tag from the tagged representation.
        Tag tag() const
        {
            return static_cast<Tag>(tagged & TAG_MASK);
        }
        // Calculate the primary value in a raw form from the tagged representation.
        uint64_t raw_value() const
        {
            return tagged >> TAG_BITS;
        }
        // Calculate the primary value from the tagged representation.
        // T must be (a pointer to) one of the ValueContents subclasses below, or else
        // * int64_t for fixnum
        // * bool for bool
        // * Null for null
        // Throws if the desired value type (T) does not match the tag.
        template <typename T> T value() const
        {
            return static_value<T>(*this);
        }

        static Value fixnum(int64_t num)
        {
            if (num < FIXNUM_MIN || num > FIXNUM_MAX) {
                throw std::out_of_range(
                    "input is too large an integer to be represented as a fixnum");
            }
            // The input is encoded in 64-bit 2s-complement; to produce INLINE_BITS-bit
            // 2s-complement instead, mask off the upper INLINE_BITS number of bits as an unsigned
            // 64-bit integer.
            return Value(Tag::FIXNUM, static_cast<uint64_t>(num) & FIXNUM_MASK);
        }
        static Value _bool(bool val)
        {
            return Value(Tag::BOOL, val ? 1 : 0);
        }
        static Value null()
        {
            return Value(Tag::_NULL, 0);
        }
        static Value tuple(Tuple* tuple)
        {
            uint64_t raw = reinterpret_cast<uint64_t>(tuple);
            if ((raw & TAG_MASK) != 0) {
                throw std::invalid_argument("tuple pointer is not TAG_BITS-aligned");
            }
            return Value(Tag::TUPLE, raw >> TAG_BITS);
        }
        static Value vector(Vector* vector)
        {
            uint64_t raw = reinterpret_cast<uint64_t>(vector);
            if (raw & TAG_MASK != 0) {
                throw std::invalid_argument("vector pointer is not TAG_BITS-aligned");
            }
            return Value(Tag::VECTOR, raw >> TAG_BITS);
        }
        static Value string(String* string)
        {
            uint64_t raw = reinterpret_cast<uint64_t>(string);
            if (raw & TAG_MASK != 0) {
                throw std::invalid_argument("string pointer is not TAG_BITS-aligned");
            }
            return Value(Tag::STRING, raw >> TAG_BITS);
        }
        static Value closure(Closure* closure)
        {
            uint64_t raw = reinterpret_cast<uint64_t>(closure);
            if (raw & TAG_MASK != 0) {
                throw std::invalid_argument("closure pointer is not TAG_BITS-aligned");
            }
            return Value(Tag::CLOSURE, raw >> TAG_BITS);
        }
        static Value type(Type* type)
        {
            uint64_t raw = reinterpret_cast<uint64_t>(type);
            if (raw & TAG_MASK != 0) {
                throw std::invalid_argument("type pointer is not TAG_BITS-aligned");
            }
            return Value(Tag::TYPE, raw >> TAG_BITS);
        }
        static Value data_inst(DataclassInstance* inst)
        {
            uint64_t raw = reinterpret_cast<uint64_t>(inst);
            if (raw & TAG_MASK != 0) {
                throw std::invalid_argument("dataclass instance pointer is not TAG_BITS-aligned");
            }
            return Value(Tag::INSTANCE, raw >> TAG_BITS);
        }
    };

    class alignas(1 << TAG_BITS) ValueContents
    {
        ValueContents(const ValueContents&) = delete;
        ValueContents(const ValueContents&&) = delete;
    };

    struct Tuple : public ValueContents
    {
        Value v_length; // fixnum
        Value* components()
        {
            return &v_length + 1;
        }
    };

    struct Vector : public ValueContents
    {
        Value v_length; // fixnum
        Value* components()
        {
            return &v_length + 1;
        }
    };

    struct String : public ValueContents
    {
        Value v_length; // fixnum
        uint8_t* contents()
        {
            return reinterpret_cast<uint8_t*>(&v_length + 1);
        }
    };

    struct Closure : public ValueContents
    {
        // TODO
    };

    struct Type : public ValueContents
    {
        Value v_name;  // string
        Value v_bases; // vector (of types)
        // Can user-defined types inherit from this?
        Value v_sealed; // bool
        // C3 linearization.
        Value v_linearization; // vector (of types)
        Value v_subtypes;      // vector (of types)
        // 0 -> mixin, 1 -> dataclass
        Value v_kind; // fixnum
        // If dataclass type (else null):
        Value v_slots; // vector (of strings)
    };

    struct DataclassInstance : public ValueContents
    {
        // The number of slots is determined by the dataclass, e.g. via
        // v_type.value<Type*>()->v_slots.value<Vector*>->v_length.value<uint64_t>().

        Value v_type; // type (must be dataclass)
        Value* slots()
        {
            return &v_type + 1;
        }
    };

    // Specializations for static_value():
    template <> int64_t static_value<int64_t>(Value value)
    {
        if (value.tag() != Tag::FIXNUM) {
            throw std::runtime_error("expected fixnum");
        }
        // The fixnum value is encoded as INLINE_BITS-bit 2s-complement, so sign extend back to
        // 64-bit 2s-complement.
        uint64_t raw = value.raw_value();
        uint64_t extended = (raw >> (INLINE_BITS - 1)) ? (raw | ~FIXNUM_MASK) : raw;
        return static_cast<int64_t>(extended);
    }
    template <> bool static_value<bool>(Value value)
    {
        if (value.tag() != Tag::BOOL) {
            throw std::runtime_error("expected bool");
        }
        return value.raw_value() != 0;
    }
    template <> Null static_value<Null>(Value value)
    {
        if (value.tag() != Tag::_NULL) {
            throw std::runtime_error("expected null");
        }
        return Null{};
    }
    template <> Tuple* static_value<Tuple*>(Value value)
    {
        if (value.tag() != Tag::TUPLE) {
            throw std::runtime_error("expected tuple");
        }
        return reinterpret_cast<Tuple*>(value.raw_value() << TAG_BITS);
    }
    template <> Vector* static_value<Vector*>(Value value)
    {
        if (value.tag() != Tag::VECTOR) {
            throw std::runtime_error("expected vector");
        }
        return reinterpret_cast<Vector*>(value.raw_value() << TAG_BITS);
    }
    template <> String* static_value<String*>(Value value)
    {
        if (value.tag() != Tag::STRING) {
            throw std::runtime_error("expected string");
        }
        return reinterpret_cast<String*>(value.raw_value() << TAG_BITS);
    }
    template <> Closure* static_value<Closure*>(Value value)
    {
        if (value.tag() != Tag::CLOSURE) {
            throw std::runtime_error("expected closure");
        }
        return reinterpret_cast<Closure*>(value.raw_value() << TAG_BITS);
    }
    template <> Type* static_value<Type*>(Value value)
    {
        if (value.tag() != Tag::TYPE) {
            throw std::runtime_error("expected type");
        }
        return reinterpret_cast<Type*>(value.raw_value() << TAG_BITS);
    }
    template <> DataclassInstance* static_value<DataclassInstance*>(Value value)
    {
        if (value.tag() != Tag::INSTANCE) {
            throw std::runtime_error("expected dataclass instance");
        }
        return reinterpret_cast<DataclassInstance*>(value.raw_value() << TAG_BITS);
    }
};

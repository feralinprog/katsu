#pragma once

#include <bit>
#include <cstdint>
#include <stdexcept>

namespace Katsu
{
    /*
     * Values are 64-bit tagged representations of various objects in Katsu:
     * (inline:)
     * - small integers (fixnums)
     * - small floating-pointing numbers (float32)
     * - booleans
     * - null singleton
     * (aggregate:)
     * - objects
     *   - tuples
     *   - vectors
     *   - strings
     *   - closures
     *   - type values
     *   - dataclass instances
     *
     * Some small objects (e.g. fixnums, bools, etc.) are stored inline; others are represented as
     * tagged pointers to the actual contents (subclasses of Object) elsewhere in memory that
     * is managed by the garbage collector.
     *
     * Functions / methods are mostly implemented in the header here to support inlining.
     */

    enum class Tag
    {
        // inline:
        FIXNUM,
        FLOAT,
        BOOL,
        _NULL,

        // aggregate:
        OBJECT,

        NUM_TAGS
    };

    static const char* tag_str(Tag tag)
    {
        switch (tag) {
            case Tag::FIXNUM: return "fixnum";
            case Tag::FLOAT: return "float";
            case Tag::BOOL: return "bool";
            case Tag::_NULL: return "null";
            case Tag::OBJECT: return "object";
            default: return "!unknown!";
        }
    }

    const std::size_t TAG_BITS = 3;
    const std::size_t INLINE_BITS = 64 - TAG_BITS;
    const uint64_t TAG_MASK = (1LL << TAG_BITS) - 1;
    static_assert(static_cast<int>(Tag::NUM_TAGS) <= (1 << TAG_BITS));

    const int64_t FIXNUM_MAX = (1LL << (INLINE_BITS - 1)) - 1;
    const int64_t FIXNUM_MIN = -(1LL << (INLINE_BITS - 1));
    // Masks out the upper INLINE_BITS number of bits.
    const uint64_t FIXNUM_MASK = ~(TAG_MASK << INLINE_BITS);
    static_assert((FIXNUM_MASK << TAG_BITS) + TAG_MASK == UINT64_MAX);

    // Number of bits of alignment for a Value* to be on a Value boundary,
    // i.e. log2(sizeof(Value)).
    const size_t VALUE_PTR_BITS = 3;

    // Template declaration here, since it's used in `value()` below. Specializations are defined
    // later.
    struct Value;
    template <typename T> T static_value(Value value);

    // Forward declarations for helper functions:
    struct Object;

    // Singleton as effectively a named replacement for std::monostate.
    struct Null
    {
    };


    // TODO: convenience function for the pattern .value<Object*>()->object<*>();
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
        // T must be one of:
        // * int64_t for fixnum
        // * float for float
        // * bool for bool
        // * Null for null
        // * Object* for object
        // Throws if the desired value type (T) does not match the tag.
        template <typename T> T value() const
        {
            return static_value<T>(*this);
        }

        inline bool is_inline() const
        {
            return this->tag() <= Tag::_NULL;
        }
        inline bool is_ref() const
        {
            return !this->is_inline();
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
        static Value _float(float val)
        {
            return Value(Tag::FLOAT, std::bit_cast<uint32_t>(val));
        }
        static Value _bool(bool val)
        {
            return Value(Tag::BOOL, val ? 1 : 0);
        }
        static Value null()
        {
            return Value(Tag::_NULL, 0);
        }
        static Value object(Object* object)
        {
            uint64_t raw = reinterpret_cast<uint64_t>(object);
            if ((raw & TAG_MASK) != 0) {
                throw std::invalid_argument("object pointer is not TAG_BITS-aligned");
            }
            return Value(Tag::OBJECT, raw >> TAG_BITS);
        }
    };
    static_assert(sizeof(Value*) <= sizeof(Value));
    static_assert((1 << VALUE_PTR_BITS) == sizeof(Value*));

    enum class ObjectTag
    {
        TUPLE,
        VECTOR,
        STRING,
        CLOSURE,
        TYPE,
        INSTANCE,
    };

    static const char* object_tag_str(ObjectTag tag)
    {
        switch (tag) {
            case ObjectTag::TUPLE: return "tuple";
            case ObjectTag::VECTOR: return "vector";
            case ObjectTag::STRING: return "string";
            case ObjectTag::CLOSURE: return "closure";
            case ObjectTag::TYPE: return "type";
            case ObjectTag::INSTANCE: return "instance";
            default: return "!unknown!";
        }
    }

    // Template declaration here, since it's used in `object()` below. Specializations are defined
    // later.
    template <typename T> T static_object(Object& object);

    struct alignas(1 << TAG_BITS) Object
    {
        Object(const Object&) = delete;
        Object(const Object&&) = delete;

        // Every aggregate object has a header to record information that the GC requires even
        // without possession of an OBJECT Value pointing to this object.
        // Header format:
        // - bit 0: forwarding pointer (1) or not (0)
        // - if a forwarding pointer:
        //   - bits 1-63: forwarding pointer (shifted 1)
        // - if normal object:
        //   - bits 1-63: ObjectTag   (TODO: lots of unused space here...)
        uint64_t header;

        inline uint64_t raw_header() const
        {
            return this->header;
        }
        inline void set_forwarding(void* p)
        {
            const uint64_t raw = reinterpret_cast<uint64_t>(p);
            if ((raw & 0x1) != 0) {
                throw std::logic_error("forwarding pointer is not aligned");
            }
            this->header = raw | 0x1LL;
        }
        inline void set_object(ObjectTag tag)
        {
            this->header = static_cast<uint64_t>(tag) << 1;
        }

        inline bool is_forwarding() const
        {
            return (this->header & 0x1) != 0;
        }
        inline bool is_object() const
        {
            return !this->is_forwarding();
        }

        inline void* forwarding() const
        {
            if (!this->is_forwarding()) {
                throw std::logic_error("not a forwarding pointer");
            }
            return reinterpret_cast<void*>(this->header & ~0x1LL);
        }
        inline ObjectTag tag() const
        {
            if (!this->is_object()) {
                throw std::logic_error("not an object");
            }
            return static_cast<ObjectTag>(this->header >> 1);
        }

        template <typename T> T object() const
        {
            return static_object<T>(*const_cast<Object*>(this));
        }

        // TODO: do another templated fn for size()
    };

    struct Tuple : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::TUPLE;

        Value v_length; // fixnum
        inline Value* components()
        {
            return &this->v_length + 1;
        }

        // Size in bytes.
        inline uint64_t size() const
        {
            int64_t length = this->v_length.value<int64_t>();
            if (length < 0) {
                throw std::runtime_error("tuple has negative length");
            }
            return (2 + length) * sizeof(Value);
        }
    };
    static_assert(sizeof(Tuple) == 2 * sizeof(Value));

    struct Vector : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::VECTOR;

        Value v_length; // fixnum
        inline Value* components()
        {
            return &this->v_length + 1;
        }

        // Size in bytes.
        inline uint64_t size() const
        {
            int64_t length = this->v_length.value<int64_t>();
            if (length < 0) {
                throw std::runtime_error("vector has negative length");
            }
            return (2 + length) * sizeof(Value);
        }
    };
    static_assert(sizeof(Vector) == 2 * sizeof(Value));

    struct String : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::STRING;

        Value v_length; // fixnum
        inline uint8_t* contents()
        {
            return reinterpret_cast<uint8_t*>(&this->v_length + 1);
        }

        // Size in bytes.
        inline uint64_t size() const
        {
            int64_t length = this->v_length.value<int64_t>();
            if (length < 0) {
                throw std::runtime_error("string has negative length");
            }
            return 2 * sizeof(Value) + length;
        }
    };
    static_assert(sizeof(String) == 2 * sizeof(Value));

    struct Closure : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::CLOSURE;

        // TODO

        // Size in bytes.
        uint64_t size() const
        {
            return sizeof(Value);
        }
    };
    static_assert(sizeof(Closure) == sizeof(Value));

    struct Type : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::TYPE;

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

        // Size in bytes.
        uint64_t size() const
        {
            return 8 * sizeof(Value);
        }
    };
    static_assert(sizeof(Type) == 8 * sizeof(Value));

    struct DataclassInstance : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::INSTANCE;

        // The number of slots is determined by the referenced dataclass.

        Value v_type; // type (must be dataclass)
        Value* slots()
        {
            return &v_type + 1;
        }

        inline Type* _class() const
        {
            return this->v_type.value<Object*>()->object<Type*>();
        }

        inline int64_t num_slots() const
        {
            // TODO: this can totally be cached in object header.
            int64_t n = this->_class()
                            ->v_slots.value<Object*>()
                            ->object<Vector*>()
                            ->v_length.value<int64_t>();
            if (n < 0) {
                throw std::runtime_error("dataclass has negative slot count");
            }
            return n;
        }

        // Size in bytes.
        uint64_t size() const
        {
            return (2 + this->num_slots()) * sizeof(Value);
        }
    };
    static_assert(sizeof(DataclassInstance) == 2 * sizeof(Value));

    // Specializations for static_value():
    template <> inline int64_t static_value<int64_t>(Value value)
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
    template <> inline float static_value<float>(Value value)
    {
        if (value.tag() != Tag::FLOAT) {
            throw std::runtime_error("expected float");
        }
        uint64_t raw_value = value.raw_value();
        // Narrowing is ok here; raw_value should have upper 32 bits zeroized.
        uint32_t raw_float = raw_value;
        return std::bit_cast<float>(raw_float);
    }
    template <> inline bool static_value<bool>(Value value)
    {
        if (value.tag() != Tag::BOOL) {
            throw std::runtime_error("expected bool");
        }
        return value.raw_value() != 0;
    }
    template <> inline Null static_value<Null>(Value value)
    {
        if (value.tag() != Tag::_NULL) {
            throw std::runtime_error("expected null");
        }
        return Null{};
    }
    template <> inline Object* static_value<Object*>(Value value)
    {
        if (value.tag() != Tag::OBJECT) {
            throw std::runtime_error("expected object");
        }
        return reinterpret_cast<Object*>(value.raw_value() << TAG_BITS);
    }

    // Specializations for static_object():
    template <> inline Tuple* static_object<Tuple*>(Object& object)
    {
        if (object.tag() != ObjectTag::TUPLE) {
            throw std::runtime_error("expected tuple");
        }
        return reinterpret_cast<Tuple*>(&object);
    }
    template <> inline Vector* static_object<Vector*>(Object& object)
    {
        if (object.tag() != ObjectTag::VECTOR) {
            throw std::runtime_error("expected vector");
        }
        return reinterpret_cast<Vector*>(&object);
    }
    template <> inline String* static_object<String*>(Object& object)
    {
        if (object.tag() != ObjectTag::STRING) {
            throw std::runtime_error("expected string");
        }
        return reinterpret_cast<String*>(&object);
    }
    template <> inline Closure* static_object<Closure*>(Object& object)
    {
        if (object.tag() != ObjectTag::CLOSURE) {
            throw std::runtime_error("expected closure");
        }
        return reinterpret_cast<Closure*>(&object);
    }
    template <> inline Type* static_object<Type*>(Object& object)
    {
        if (object.tag() != ObjectTag::TYPE) {
            throw std::runtime_error("expected type");
        }
        return reinterpret_cast<Type*>(&object);
    }
    template <> inline DataclassInstance* static_object<DataclassInstance*>(Object& object)
    {
        if (object.tag() != ObjectTag::INSTANCE) {
            throw std::runtime_error("expected dataclass instance");
        }
        return reinterpret_cast<DataclassInstance*>(&object);
    }
};

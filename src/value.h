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
     *   - mutable object references
     *   - tuples (fixed-length) (TODO: fold into arrays..?)
     *   - arrays (fixed-length)
     *   - vectors (growable)
     *   - modules (environments)
     *   - strings
     *   - code templates / definitions
     *   - closures
     *   - methods
     *   - multimethods
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

    // Singleton as effectively a named replacement for std::monostate.
    struct Null
    {
    };

    enum class ObjectTag
    {
        REF,
        TUPLE,
        ARRAY,
        VECTOR,
        MODULE,
        STRING,
        CODE,
        CLOSURE,
        METHOD,
        MULTIMETHOD,
        TYPE,
        INSTANCE,
    };

    static const char* object_tag_str(ObjectTag tag)
    {
        switch (tag) {
            case ObjectTag::REF: return "ref";
            case ObjectTag::TUPLE: return "tuple";
            case ObjectTag::ARRAY: return "array";
            case ObjectTag::VECTOR: return "vector";
            case ObjectTag::MODULE: return "module";
            case ObjectTag::STRING: return "string";
            case ObjectTag::CODE: return "code";
            case ObjectTag::CLOSURE: return "closure";
            case ObjectTag::METHOD: return "method";
            case ObjectTag::MULTIMETHOD: return "multimethod";
            case ObjectTag::TYPE: return "type";
            case ObjectTag::INSTANCE: return "instance";
            default: return "!unknown!";
        }
    }

    // Template declaration here, since it's used in `object()` below. Specializations are defined
    // later.
    struct Object;
    template <typename T> T static_object(Object& object);

    // TODO: add functions to verify basic invariants of objects (for instance, v_length >= 0),
    // and call this from GC when in a debug mode, on every object traced.

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

    // Forward declarations for helper functions:
    struct Ref;
    struct Tuple;
    struct Array;
    struct Vector;
    struct Module;
    struct String;
    struct Code;
    struct Closure;
    struct Method;
    struct MultiMethod;
    struct Type;
    struct DataclassInstance;

    // TODO: create related generic types which are guaranteed to have the right tag?
    // Like TaggedValue<int64_t>, guaranteed to be a fixnum.

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

        bool is_fixnum() const
        {
            return this->tag() == Tag::FIXNUM;
        }
        bool is_float() const
        {
            return this->tag() == Tag::FLOAT;
        }
        bool is_bool() const
        {
            return this->tag() == Tag::BOOL;
        }
        bool is_null() const
        {
            return this->tag() == Tag::_NULL;
        }
        bool is_object() const
        {
            return this->tag() == Tag::OBJECT;
        }
        bool is_obj_ref() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::REF;
        }
        bool is_obj_tuple() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::TUPLE;
        }
        bool is_obj_array() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::ARRAY;
        }
        bool is_obj_vector() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::VECTOR;
        }
        bool is_obj_module() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::MODULE;
        }
        bool is_obj_string() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::STRING;
        }
        bool is_obj_code() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::CODE;
        }
        bool is_obj_closure() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::CLOSURE;
        }
        bool is_obj_method() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::METHOD;
        }
        bool is_obj_multimethod() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::MULTIMETHOD;
        }
        bool is_obj_type() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::TYPE;
        }
        bool is_obj_instance() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::INSTANCE;
        }

        int64_t fixnum() const
        {
            return this->value<int64_t>();
        }
        float _float() const
        {
            return this->value<float>();
        }
        bool _bool() const
        {
            return this->value<bool>();
        }
        // No point to a _null() function... just use is_null().
        Object* object() const
        {
            return this->value<Object*>();
        }
        Ref* obj_ref() const
        {
            return this->object()->object<Ref*>();
        }
        Tuple* obj_tuple() const
        {
            return this->object()->object<Tuple*>();
        }
        Array* obj_array() const
        {
            return this->object()->object<Array*>();
        }
        Vector* obj_vector() const
        {
            return this->object()->object<Vector*>();
        }
        Module* obj_module() const
        {
            return this->object()->object<Module*>();
        }
        String* obj_string() const
        {
            return this->object()->object<String*>();
        }
        Code* obj_code() const
        {
            return this->object()->object<Code*>();
        }
        Closure* obj_closure() const
        {
            return this->object()->object<Closure*>();
        }
        Method* obj_method() const
        {
            return this->object()->object<Method*>();
        }
        MultiMethod* obj_multimethod() const
        {
            return this->object()->object<MultiMethod*>();
        }
        Type* obj_type() const
        {
            return this->object()->object<Type*>();
        }
        DataclassInstance* obj_instance() const
        {
            return this->object()->object<DataclassInstance*>();
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

        inline bool operator==(const Value& other) const
        {
            return other.tagged == this->tagged;
        }
        inline bool operator!=(const Value& other) const
        {
            return !(*this == other);
        }
    };
    static_assert(sizeof(Value*) <= sizeof(Value));
    static_assert((1 << VALUE_PTR_BITS) == sizeof(Value*));

    struct Ref : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::REF;

        Value v_ref; // should be Object, could be anything really

        // Size in bytes.
        static inline uint64_t size()
        {
            return sizeof(Ref);
        }
    };

    struct Tuple : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::TUPLE;

        uint64_t length;
        inline Value* components()
        {
            return reinterpret_cast<Value*>(&this->length + 1);
        }

        // Size in bytes.
        static inline uint64_t size(uint64_t length)
        {
            return sizeof(Tuple) + length * sizeof(Value);
        }
        inline uint64_t size() const
        {
            return Tuple::size(this->length);
        }
    };

    struct Array : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::ARRAY;

        uint64_t length;
        inline Value* components()
        {
            return reinterpret_cast<Value*>(&this->length + 1);
        }

        // Size in bytes.
        static inline uint64_t size(uint64_t length)
        {
            return sizeof(Array) + length * sizeof(Value);
        }
        inline uint64_t size() const
        {
            return Array::size(this->length);
        }
    };

    struct Vector : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::VECTOR;

        // Number of in-use entries from the backing array.
        uint64_t length;
        // Backing array.
        Value array; // Array

        // Length of the backing array.
        uint64_t capacity()
        {
            return this->array.obj_array()->length;
        }

        // Size in bytes.
        static inline uint64_t size()
        {
            return sizeof(Vector);
        }
    };

    struct Module : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::MODULE;

        // Currently implemented as associative array.
        // TODO: use a more optimal data structure.
        struct Entry
        {
            Value v_key;   // String
            Value v_value; // anything
        };
        static_assert(sizeof(Entry) == 2 * sizeof(Value));

        Value v_base; // Module or Null
        uint64_t capacity;
        uint64_t length;
        inline Entry* entries()
        {
            return reinterpret_cast<Entry*>(&this->length + 1);
        }

        // Size in bytes.
        static inline uint64_t size(uint64_t capacity)
        {
            return sizeof(Module) + capacity * sizeof(Entry);
        }
        inline uint64_t size() const
        {
            return Module::size(this->capacity);
        }
    };

    struct String : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::STRING;

        uint64_t length;
        inline uint8_t* contents()
        {
            return reinterpret_cast<uint8_t*>(&this->length + 1);
        }

        // Size in bytes.
        static inline uint64_t size(uint64_t length)
        {
            return sizeof(String) + length /* * sizeof(uint8_t) */;
        }
        inline uint64_t size() const
        {
            return String::size(this->length);
        }
    };

    struct Code : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::CODE;

        Value v_module; // Module
        uint32_t num_regs;
        uint32_t num_data;
        Value v_upreg_map; // Null for methods; Array (of fixnum) for closures
        // TODO: byte array inline?
        Value v_insts; // Array of fixnums
        // TODO: arg array inline?
        Value v_args; // Array (of arbitrary values)
        // TODO: source span for the source of the bytecode (e.g. closure or method definition)
        // TODO: source span per bytecode

        // Size in bytes.
        static inline uint64_t size()
        {
            return sizeof(Code);
        }
    };

    struct Closure : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::CLOSURE;

        Value v_code;   // Code
        Value v_upregs; // Array

        // Size in bytes.
        static inline uint64_t size()
        {
            return sizeof(Closure);
        }
    };

    // Pointer to a function which takes an array of Values, calculates a result, and returns it.
    // The input values may be temporary locations in a call frame; copy them before calling into
    // any VM functionality. Furthermore, the input values may not be GC roots; add them as roots
    // before using any GC functionality (which may induce a collection).
    class VM;
    typedef Value (*NativeHandler)(VM&, int64_t, Value*);
    // typedef Value *IntrinsicHandler(int64_t, Value*);

    struct Method : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::METHOD;

        Value v_param_matchers; // TODO how to represent this?? vector of any / type / value
        Value v_return_type;    // Type or Null
        Value v_code;           // Code, or Null if referring to a native method
        // Arbitrary extra values attached by user.
        Value v_attributes;           // Vector
        NativeHandler native_handler; // optional (nullptr)

        // Size in bytes.
        static inline uint64_t size()
        {
            return sizeof(Method);
        }
    };

    struct MultiMethod : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::MULTIMETHOD;

        // Just for debugging / logging.
        Value v_name;    // String
        Value v_methods; // Vector of Method
        // Arbitrary extra values attached by user.
        Value v_attributes; // Vector

        // Size in bytes.
        static inline uint64_t size()
        {
            return sizeof(MultiMethod);
        }
    };

    struct Type : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::TYPE;

        enum class Kind
        {
            MIXIN = 0,
            DATACLASS = 1,
        };

        Value v_name;  // String
        Value v_bases; // Vector (of Types)
        // Can user-defined types inherit from this?
        bool sealed;
        // C3 linearization.
        Value v_linearization; // Vector (of Types)
        Value v_subtypes;      // Vector (of Types)
        Kind kind;
        // If dataclass type (else null):
        Value v_slots; // Vector (of Strings)

        // Size in bytes.
        static inline uint64_t size()
        {
            return sizeof(Type);
        }
    };

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
            return this->v_type.obj_type();
        }

        inline int64_t num_slots() const
        {
            // TODO: this can totally be cached in object header.
            return this->_class()->v_slots.obj_vector()->length;
        }

        // Size in bytes.
        static inline uint64_t size(int64_t num_slots)
        {
            return sizeof(DataclassInstance) + num_slots * sizeof(Value);
        }
        static inline uint64_t size(Type* _class)
        {
            return DataclassInstance::size(_class->v_slots.obj_vector()->length);
        }
        inline uint64_t size() const
        {
            return DataclassInstance::size(this->_class());
        }
    };

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
    template <> inline Ref* static_object<Ref*>(Object& object)
    {
        if (object.tag() != ObjectTag::REF) {
            throw std::runtime_error("expected ref");
        }
        return reinterpret_cast<Ref*>(&object);
    }
    template <> inline Tuple* static_object<Tuple*>(Object& object)
    {
        if (object.tag() != ObjectTag::TUPLE) {
            throw std::runtime_error("expected tuple");
        }
        return reinterpret_cast<Tuple*>(&object);
    }
    template <> inline Array* static_object<Array*>(Object& object)
    {
        if (object.tag() != ObjectTag::ARRAY) {
            throw std::runtime_error("expected array");
        }
        return reinterpret_cast<Array*>(&object);
    }
    template <> inline Vector* static_object<Vector*>(Object& object)
    {
        if (object.tag() != ObjectTag::VECTOR) {
            throw std::runtime_error("expected vector");
        }
        return reinterpret_cast<Vector*>(&object);
    }
    template <> inline Module* static_object<Module*>(Object& object)
    {
        if (object.tag() != ObjectTag::MODULE) {
            throw std::runtime_error("expected module");
        }
        return reinterpret_cast<Module*>(&object);
    }
    template <> inline String* static_object<String*>(Object& object)
    {
        if (object.tag() != ObjectTag::STRING) {
            throw std::runtime_error("expected string");
        }
        return reinterpret_cast<String*>(&object);
    }
    template <> inline Code* static_object<Code*>(Object& object)
    {
        if (object.tag() != ObjectTag::CODE) {
            throw std::runtime_error("expected code");
        }
        return reinterpret_cast<Code*>(&object);
    }
    template <> inline Closure* static_object<Closure*>(Object& object)
    {
        if (object.tag() != ObjectTag::CLOSURE) {
            throw std::runtime_error("expected closure");
        }
        return reinterpret_cast<Closure*>(&object);
    }
    template <> inline Method* static_object<Method*>(Object& object)
    {
        if (object.tag() != ObjectTag::METHOD) {
            throw std::runtime_error("expected method");
        }
        return reinterpret_cast<Method*>(&object);
    }
    template <> inline MultiMethod* static_object<MultiMethod*>(Object& object)
    {
        if (object.tag() != ObjectTag::MULTIMETHOD) {
            throw std::runtime_error("expected multimethod");
        }
        return reinterpret_cast<MultiMethod*>(&object);
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
            throw std::runtime_error("expected instance");
        }
        return reinterpret_cast<DataclassInstance*>(&object);
    }
};

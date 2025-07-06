#pragma once

#include "assertions.h"

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
     *   - assocs (vector-style maps)
     *   - strings
     *   - code templates / definitions
     *   - closures
     *   - methods
     *   - multimethods
     *   - type values
     *   - dataclass instances
     *   - call stack segment (a group of call frames)
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
    static const char* TAG_STR(Tag tag)
    {
        switch (tag) {
            case Tag::FIXNUM: return "FIXNUM";
            case Tag::FLOAT: return "FLOAT";
            case Tag::BOOL: return "BOOL";
            case Tag::_NULL: return "_NULL";
            case Tag::OBJECT: return "OBJECT";
            default: return "!UNKNOWN!";
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
        ASSOC,
        STRING,
        CODE,
        CLOSURE,
        METHOD,
        MULTIMETHOD,
        TYPE,
        INSTANCE,
        CALL_SEGMENT,
    };

    static const char* object_tag_str(ObjectTag tag)
    {
        switch (tag) {
            case ObjectTag::REF: return "ref";
            case ObjectTag::TUPLE: return "tuple";
            case ObjectTag::ARRAY: return "array";
            case ObjectTag::VECTOR: return "vector";
            case ObjectTag::ASSOC: return "assoc";
            case ObjectTag::STRING: return "string";
            case ObjectTag::CODE: return "code";
            case ObjectTag::CLOSURE: return "closure";
            case ObjectTag::METHOD: return "method";
            case ObjectTag::MULTIMETHOD: return "multimethod";
            case ObjectTag::TYPE: return "type";
            case ObjectTag::INSTANCE: return "instance";
            case ObjectTag::CALL_SEGMENT: return "call-segment";
            default: return "!unknown!";
        }
    }
    static const char* OBJECT_TAG_STR(ObjectTag tag)
    {
        switch (tag) {
            case ObjectTag::REF: return "REF";
            case ObjectTag::TUPLE: return "TUPLE";
            case ObjectTag::ARRAY: return "ARRAY";
            case ObjectTag::VECTOR: return "VECTOR";
            case ObjectTag::ASSOC: return "ASSOC";
            case ObjectTag::STRING: return "STRING";
            case ObjectTag::CODE: return "CODE";
            case ObjectTag::CLOSURE: return "CLOSURE";
            case ObjectTag::METHOD: return "METHOD";
            case ObjectTag::MULTIMETHOD: return "MULTIMETHOD";
            case ObjectTag::TYPE: return "TYPE";
            case ObjectTag::INSTANCE: return "INSTANCE";
            case ObjectTag::CALL_SEGMENT: return "CALL_SEGMENT";
            default: return "!UNKNOWN!";
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
            ASSERT_MSG((raw & 0x1) == 0, "forwarding pointer is not aligned");
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
            ASSERT(this->is_forwarding());
            return reinterpret_cast<void*>(this->header & ~0x1LL);
        }
        inline ObjectTag tag() const
        {
            ASSERT(this->is_object());
            return static_cast<ObjectTag>(this->header >> 1);
        }

        template <typename T> T object() const
        {
            return static_object<T>(*const_cast<Object*>(this));
        }

        // TODO: do another templated fn for size()
    };
    // Keep in sync with katsu core.
    static_assert(sizeof(Object) == 8);

    // Forward declarations for helper functions:
    struct Ref;
    struct Tuple;
    struct Array;
    struct Vector;
    struct Assoc;
    struct String;
    struct Code;
    struct Closure;
    struct Method;
    struct MultiMethod;
    struct Type;
    struct DataclassInstance;
    struct CallSegment;

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
        // Default constructor: produce a null value.
        Value()
            : Value(Tag::_NULL, 0)
        {}

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
        bool is_obj_assoc() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::ASSOC;
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
        bool is_obj_call_segment() const
        {
            return this->tag() == Tag::OBJECT && this->object()->tag() == ObjectTag::CALL_SEGMENT;
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
        Assoc* obj_assoc() const
        {
            return this->object()->object<Assoc*>();
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
        CallSegment* obj_call_segment() const
        {
            return this->object()->object<CallSegment*>();
        }

        static Value fixnum(int64_t num)
        {
            ASSERT_EXC_MSG(num >= FIXNUM_MIN && num <= FIXNUM_MAX,
                           std::out_of_range,
                           "input is too large an integer to be represented as a fixnum");
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
            ASSERT_ARG_MSG((raw & TAG_MASK) == 0, "object pointer is not TAG_BITS-aligned");
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
        Value v_array; // Array

        // Length of the backing array.
        uint64_t capacity()
        {
            return this->v_array.obj_array()->length;
        }

        // Size in bytes.
        static inline uint64_t size()
        {
            return sizeof(Vector);
        }
    };

    struct Assoc : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::ASSOC;

        // Implemented as an associative array (more specifically, as an Array of length 2n,
        // where n is the number of key/value pairs).
        struct Entry
        {
            Value v_key;   // String
            Value v_value; // anything
        };
        static_assert(sizeof(Entry) == 2 * sizeof(Value));

        uint64_t length;
        Value v_array; // Array (of String/any pairs, stored consecutively)

        inline Entry* entries()
        {
            return reinterpret_cast<Entry*>(this->v_array.obj_array()->components());
        }

        // Size in bytes.
        static inline uint64_t size()
        {
            return sizeof(Assoc);
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

        Value v_module; // Assoc
        // Mostly for error checking -- not actually used for 'control' purposes.
        uint32_t num_params;
        uint32_t num_regs;
        uint32_t num_data;
        Value v_upreg_map; // Null for methods; Array (of fixnums) for closures
        // TODO: byte array inline?
        Value v_insts; // Array of fixnums
        // TODO: arg array inline?
        Value v_args; // Array (of arbitrary values)
        // TODO: better representation of source spans.
        // For now, just a Tuple of [filepath, start index, start line, start col, end index, end
        // line, end col].
        Value v_span;       // source span tuple
        Value v_inst_spans; // Array (of source span tuples)

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
    // any VM functionality. Furthermore, the input values may not be live; add them as GC roots
    // before using any GC functionality (which may induce a collection).
    // Native handlers should avoid using any eval-style functions; such functionality should
    // instead be implemented using intrinsic handlers so as to reify the Katsu call stack.
    class VM;
    typedef Value (*NativeHandler)(VM& vm, int64_t nargs, Value* args);
    // Pointer to a function which takes a VM and all its runtime state and is allowed to
    // arbitrarily modify that state, for instance by adding or removing call frames. The handler
    // must also update the top-of-call-stack instruction position as necessary so that the position
    // indicates the next instruction to execute. The input values are already popped from the top
    // call-frame's data stack, so copy them before calling into any VM functionality. Furthermore,
    // add them as GC roots before using any GC functionality (which may induce a collection). If
    // the handler raises an exception, it must ensure that the VM is left in a state where pushing
    // one more value to the data stack allows that value to be treated as the result of the handler
    // invocation. (For instance, not modifying the data stack at all meets this criterion.)
    class OpenVM;
    typedef void (*IntrinsicHandler)(OpenVM& vm, bool tail_call, int64_t nargs, Value* args);

    struct Method : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::METHOD;

        // This is a bit hacky. Each entry of the param matchers should be:
        // - null -> an any matcher
        // - a type -> a type matcher
        // - a ref -> a value matcher
        Value v_param_matchers; // Array
        Value v_return_type;    // Type or Null
        Value v_code;           // Code, or Null if referring to a native method
        // Arbitrary extra values attached by user.
        Value v_attributes;                 // Vector
        NativeHandler native_handler;       // optional (nullptr)
        IntrinsicHandler intrinsic_handler; // optional (nullptr)

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
        Value v_name; // String
        // Mostly for sanity checking.
        uint32_t num_params;
        Value v_methods; // Vector of Methods
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
            PRIMITIVE = 0,
            DATACLASS = 1,
            MIXIN = 2,
        };

        Value v_name;  // String
        Value v_bases; // Array (of Types)
        // Can user-defined types inherit from this?
        bool sealed;
        // C3 linearization.
        Value v_linearization; // Array (of Types)
        Value v_subtypes;      // Vector (of Types)
        Kind kind;
        // If dataclass type (else null):
        Value v_slots; // Array (of Strings)
        // If dataclass type, number of slots of this dataclass along with any recursive base
        // dataclasses. Otherwise unused.
        uint32_t num_total_slots;

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

        Value v_type; // Type (must be Kind::DATACLASS)
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
        static inline uint64_t size(uint64_t num_slots)
        {
            return sizeof(DataclassInstance) + num_slots * sizeof(Value);
        }
        // Non-static size() function is effectively implemented in the GC, since there is extra
        // complexity to determine size while some objects are forwarding pointers (as opposed to
        // actual object contents).
    };


    // From vm.h.
    struct Frame;
    struct CallSegment : public Object
    {
        static const ObjectTag CLASS_TAG = ObjectTag::CALL_SEGMENT;

        // Total number of bytes of frame content.
        uint64_t length;
        inline Frame* frames()
        {
            return reinterpret_cast<Frame*>(&this->length + 1);
        }

        // Size in bytes.
        static inline uint64_t size(uint64_t length)
        {
            return sizeof(CallSegment) + length;
        }
        inline uint64_t size() const
        {
            return CallSegment::size(this->length);
        }
    };

    // Specializations for static_value():
    template <> inline int64_t static_value<int64_t>(Value value)
    {
        ASSERT(value.tag() == Tag::FIXNUM);
        // The fixnum value is encoded as INLINE_BITS-bit 2s-complement, so sign extend back to
        // 64-bit 2s-complement.
        uint64_t raw = value.raw_value();
        uint64_t extended = (raw >> (INLINE_BITS - 1)) ? (raw | ~FIXNUM_MASK) : raw;
        return static_cast<int64_t>(extended);
    }
    template <> inline float static_value<float>(Value value)
    {
        ASSERT(value.tag() == Tag::FLOAT);
        uint64_t raw_value = value.raw_value();
        // Narrowing is ok here; raw_value should have upper 32 bits zeroized.
        uint32_t raw_float = raw_value;
        return std::bit_cast<float>(raw_float);
    }
    template <> inline bool static_value<bool>(Value value)
    {
        ASSERT(value.tag() == Tag::BOOL);
        return value.raw_value() != 0;
    }
    template <> inline Null static_value<Null>(Value value)
    {
        ASSERT(value.tag() == Tag::_NULL);
        return Null{};
    }
    template <> inline Object* static_value<Object*>(Value value)
    {
        ASSERT(value.tag() == Tag::OBJECT);
        return reinterpret_cast<Object*>(value.raw_value() << TAG_BITS);
    }

    // Specializations for static_object():
    template <> inline Ref* static_object<Ref*>(Object& object)
    {
        ASSERT(object.tag() == ObjectTag::REF);
        return reinterpret_cast<Ref*>(&object);
    }
    template <> inline Tuple* static_object<Tuple*>(Object& object)
    {
        ASSERT(object.tag() == ObjectTag::TUPLE);
        return reinterpret_cast<Tuple*>(&object);
    }
    template <> inline Array* static_object<Array*>(Object& object)
    {
        ASSERT(object.tag() == ObjectTag::ARRAY);
        return reinterpret_cast<Array*>(&object);
    }
    template <> inline Vector* static_object<Vector*>(Object& object)
    {
        ASSERT(object.tag() == ObjectTag::VECTOR);
        return reinterpret_cast<Vector*>(&object);
    }
    template <> inline Assoc* static_object<Assoc*>(Object& object)
    {
        ASSERT(object.tag() == ObjectTag::ASSOC);
        return reinterpret_cast<Assoc*>(&object);
    }
    template <> inline String* static_object<String*>(Object& object)
    {
        ASSERT(object.tag() == ObjectTag::STRING);
        return reinterpret_cast<String*>(&object);
    }
    template <> inline Code* static_object<Code*>(Object& object)
    {
        ASSERT(object.tag() == ObjectTag::CODE);
        return reinterpret_cast<Code*>(&object);
    }
    template <> inline Closure* static_object<Closure*>(Object& object)
    {
        ASSERT(object.tag() == ObjectTag::CLOSURE);
        return reinterpret_cast<Closure*>(&object);
    }
    template <> inline Method* static_object<Method*>(Object& object)
    {
        ASSERT(object.tag() == ObjectTag::METHOD);
        return reinterpret_cast<Method*>(&object);
    }
    template <> inline MultiMethod* static_object<MultiMethod*>(Object& object)
    {
        ASSERT(object.tag() == ObjectTag::MULTIMETHOD);
        return reinterpret_cast<MultiMethod*>(&object);
    }
    template <> inline Type* static_object<Type*>(Object& object)
    {
        ASSERT(object.tag() == ObjectTag::TYPE);
        return reinterpret_cast<Type*>(&object);
    }
    template <> inline DataclassInstance* static_object<DataclassInstance*>(Object& object)
    {
        ASSERT(object.tag() == ObjectTag::INSTANCE);
        return reinterpret_cast<DataclassInstance*>(&object);
    }
    template <> inline CallSegment* static_object<CallSegment*>(Object& object)
    {
        ASSERT(object.tag() == ObjectTag::CALL_SEGMENT);
        return reinterpret_cast<CallSegment*>(&object);
    }
};

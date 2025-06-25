#pragma once

#include "gc.h"

namespace Katsu
{
    // TODO: update this whole block!
    /*
     * Bytecodes:
     * - LOAD_REG: load local register by index onto stack
     * - STORE_REG: pop from stack to local register
     * - LOAD_REF: load local register by index, then dereference _that_, to push to stack
     * - STORE_REF: pop from stack into pointee of reference object
     * - LOAD_VALUE: load a Value (contents may be managed by GC!) onto stack
     * - INIT_REF: pop from stack, wrap in a Ref, and write to a local register
     * - LOAD_MODULE: load a value by name from the call frame's module onto stack
     * - STORE_MODULE: pop from stack to module variable
     * - INVOKE: look up (by name) a multimethod in the global multimethod store, pop arguments, and
     *      call the method (the lookup should probably be pre-calculated...)
     * - INVOKE_TAIL: do the same thing, but as a tail-call
     * - DROP: pop a value from the stack
     * - MAKE_TUPLE: pop some values from the stack, push new tuple with those values
     * - MAKE_VECTOR: pop some values from the stack, push new tuple with those values
     * - MAKE_CLOSURE: push a closure object (which refers to some closed-over variables) to the
     *      stack
     *
     * Bytecode format: there are separate instruction and value regions (which should be right next
     * to each other anyway to keep cache warm) -- and also an offside array of SourceSpan per
     * instruction
     * - instructions array: concatenated sequence of <1-byte opcode> <3-byte args offset>, where
     *   the offsets are in terms of 8-byte Values in the value/argument region
     * - value array: aligned 8-byte Values (could be inline or reference), which should be
     *   considered roots for the GC
     *
     * +----------------+--------+----------------------------------------------------------+
     * | Name           | Opcode | Arguments ...                                            |
     * +----------------+--------+----------------------------------------------------------+
     * | LOAD_REG       |  0x0   | (fixnum) local index                                     |
     * | STORE_REG      |  0x1   | (fixnum) local index                                     |
     * | LOAD_REF       |  0x2   | (fixnum) local index                                     |
     * | STORE_REF      |  0x3   | (fixnum) local index                                     |
     * | LOAD_VALUE     |  0x4   | value to load                                            |
     * | INIT_REF       |  0x5   | (fixnum) local index                                     |
     * | LOAD_MODULE    |  0x6   | (string) name                                            |
     * | STORE_MODULE   |  0x7   | (string) name                                            |
     * | INVOKE         |  0x8   | (string) name; (fixnum) num args                     (1) |
     * | INVOKE_TAIL    |  0x9   | (string) name; (fixnum) num args                     (1) |
     * | DROP           |  0xA   | none                                                     |
     * | MAKE_TUPLE     |  0xB   | (fixnum) num components                                  |
     * | MAKE_ARRAY     |  0xC   | (fixnum) num components                                  |
     * | MAKE_VECTOR    |  0xD   | (fixnum) num components                                  |
     * | MAKE_CLOSURE   |  0xE   | (closure) closure 'template'                         (2) |
     * | MAKE_INSTANCE  |  0xF   | (fixnum) num slots                                       |
     * | VERIFY_IS_TYPE |  0x10  | none                                                     |
     * | GET_SLOT       |  0x11  | (fixnum) slot index                                      |
     * | SET_SLOT       |  0x12  | (fixnum) slot index                                      |
     * +----------------+--------+----------------------------------------------------------+
     * Notes:
     * (1) This should probably refer to an actual multimethod object to avoid lookups...
     *     similarly load/store with module fields should be precomputed somehow.
     * (2) The closure template should host the closure's bytecode, upreg-mapping, and therefore
     *     also number of upregs. These are popped from the data stack, like making a vector.
     *
     * Stack Frame:
     * - array of 'registers' (arguments, 'let:' and 'mut:' bindings) ('mut:' variables are handled
     *   as effectively Ref<T>, i.e. one extra layer of boxing and unboxing, and handled with
     *   LOAD/STORE_REF instead of _REG)
     * - data stack (statically known max size; can be implemented as fixed-size array)
     * - cleanup value (value to invoke/call when unwinding frame)
     * - is_cleanup (whether or not this frame is the result of calling a cleanup value -- only the
     *   first frame in that chain, though)
     *   - this should just indicate whether return value should be pushed to lower frame's data
     *     stack or not
     * - optional pointer to a single return-continuation object (new GC object type..? or just like
     *   a fixnum UUID thing?)
     *   - when RC is invoked, just search through stack frames to find the frame pointing to that
     *     same RC object; if no such frame, RC is no longer valid (or maybe RC obj can have an
     *     invalidation flag, and when unwinding stack frame, RC is invalidated)
     * - module object
     * - oh, also some bytecode itself and an instruction index
     *
     * Ok, where is everything allocated?
     * - stack frames: in a malloc'ed (or mmap'ed?) region per VM, contiguous memory up to alignment
     * - bytecode: TODO. make a new object tag for this..? could be managed by GC.
     * - closure 'templates': TODO. make a new object tag for this... (or reuse prev?)?
     *   maybe CODE object has:
     *   - upreg-mapping map
     *   - instructions byte array
     *   - args Value array (just following prev field)
     *   - source-mapping SourceSpan array (just following prev field)
     *     hmm... has shared ptrs, need to be able to properly destruct. this can't be with GC.
     *     (or! put SourceSpans in the GC. need to set up some C++ <-> dataclass interop, though.)
     * - multimethods / methods? TODO
     *
     * Scopes:
     * - global scope (which usually should only have multimethods)
     * - then all modules derive from global scope or each other (in a tree)
     *   and can have multimethods, types, variables, anything
     * - then closures / methods refer to module, and have internal scope (compiled down to set of
     *   registers and access pattern via e.g. load_reg/load_ref)
     */

    enum OpCode
    {
        LOAD_REG,
        STORE_REG,
        LOAD_REF,
        STORE_REF,
        LOAD_VALUE,
        INIT_REF,
        LOAD_MODULE,
        STORE_MODULE,
        INVOKE,
        INVOKE_TAIL,
        DROP,
        MAKE_TUPLE,
        MAKE_ARRAY,
        MAKE_VECTOR,
        MAKE_CLOSURE,
        MAKE_INSTANCE,
        VERIFY_IS_TYPE,
        GET_SLOT,
        SET_SLOT,
    };

    struct Frame
    {
        // Frame which called this one, or nullptr if bottom of stack.
        Frame* caller;

        Value v_code; // Code

        // Current index in the code's insts array.
        uint32_t inst_spot;

        // Number of `regs()`.
        uint64_t num_regs;
        // (Maximum) number of `data()`.
        uint64_t num_data;
        // Current size of the data stack (up to `num_data`).
        uint64_t data_depth;

        Value v_module; // Assoc

        // Any value, used for delimiting continuations.
        Value v_marker;

        // Variable-length array of length `num_regs`.
        inline Value* regs()
        {
            return reinterpret_cast<Value*>(this + 1);
        }

        // Variable length array of length `num_data`.
        inline Value* data()
        {
            return this->regs() + this->num_regs;
        }

        // Number of bytes for the Frame and its trailing registers / data.
        static size_t size(uint32_t num_regs, uint32_t num_data)
        {
            return sizeof(Frame) + (num_regs + num_data) * sizeof(Value);
        }
        inline size_t size()
        {
            return Frame::size(this->num_regs, this->num_data);
        }

        // Pointer to the Frame that would follow the current one in the call stack.
        inline Frame* next()
        {
            return reinterpret_cast<Frame*>(
                align_up(reinterpret_cast<uint64_t>(this) + this->size(), TAG_BITS));
        }

        inline void push(Value value)
        {
            ASSERT_MSG(this->data_depth < this->num_data, "data stack overflow in frame");
            this->data()[this->data_depth++] = value;
        }
        inline Value peek()
        {
            ASSERT_MSG(this->data_depth > 0, "data stack underflow in frame");
            return this->data()[this->data_depth - 1];
        }
        inline Value pop()
        {
            ASSERT_MSG(this->data_depth > 0, "data stack underflow in frame");
            return this->data()[--this->data_depth];
        }
        inline Value* peek_many(uint32_t num_values)
        {
            ASSERT_MSG(this->data_depth >= num_values, "data stack underflow in frame");
            return this->data() + this->data_depth - num_values;
        }
        inline Value* pop_many(uint32_t num_values)
        {
            ASSERT_MSG(this->data_depth >= num_values, "data stack underflow in frame");
            this->data_depth -= num_values;
            return this->data() + this->data_depth;
        }
    };
    static_assert(sizeof(Frame) % sizeof(Value) == 0);

    enum BuiltinId
    {
        _null,
        _true,
        _false,

        _Fixnum,
        _Float,
        _Bool,
        _Null,
        _Ref,
        _Tuple,
        _Array,
        _Vector,
        _Assoc,
        _String,
        _Code,
        _Closure,
        _Method,
        _MultiMethod,
        _Type,
        _CallSegment,

        // Keep this last!
        NUM_BUILTINS,
    };

    class VM : public RootProvider
    {
    public:
        VM(GC& gc, uint64_t call_stack_size);
        ~VM();

        Value eval_toplevel(Root<Code>& r_code);

        void visit_roots(std::function<void(Value*)>& visitor) override;

        inline Value builtin(BuiltinId id)
        {
            ASSERT(id >= 0 && id < BuiltinId::NUM_BUILTINS);
            return this->builtin_values[id];
        }

        void register_builtin(BuiltinId id, Value value);

        // GC for values tracked by this VM.
        GC& gc;

    private:
        friend class OpenVM;

        void print_vm_state();

        void single_step();

        void unwind_frame(bool tail_call);

        // Look up the method_name in the module.
        static Value& module_lookup_or_fail(Value v_module, String* name);

        // Allocates a call frame. The caller must initialize the new frame's regs() and
        // data(), in particular before any GC operations. Returns the new frame, which the caller
        // must set as the current_frame if desired. Raises runtime_error on stack overflow.
        Frame* alloc_frame(uint32_t num_regs, uint32_t num_data, Value v_code, Value v_module,
                           Value v_marker);

        // Allocates a region for multiple call frames. The caller must initialize the entire region
        // before further VM or GC operations. Returns just past the top-most frame. Does not update
        // the VM's current_frame. Raises runtime_error on stack overflow.
        Frame* alloc_frames(uint64_t total_length);

        // Invoke a value (which could be a closure or multimethod) with some arguments. The
        // arguments may be just past the end of the current frame's data stack. This also takes
        // responsibility for updating the top call frame's instruction spot.
        void invoke(Value v_callable, bool tail_call, int64_t num_args, Value* args);

        // Memory region for the call stack.
        // Hosts contiguous `Frame`s.
        uint8_t* call_stack_mem;
        uint64_t call_stack_size;

        // null, or points into the call_stack_mem.
        Frame* current_frame;

        // Builtin values that we need convenient access to (and which are GC'ed).
        // Indexed by BuiltinId.
        Value builtin_values[BuiltinId::NUM_BUILTINS];
    };

    // This should only be used by intrinsic handlers.
    class OpenVM
    {
    public:
        OpenVM(VM& _vm)
            : vm(_vm)
            , gc(_vm.gc)
        {}

        // Get the bottom and top of the call stack.
        inline Frame* bottom_frame()
        {
            return reinterpret_cast<Frame*>(this->vm.call_stack_mem);
        }
        inline Frame* frame()
        {
            return this->vm.current_frame;
        }

        // Set the current top-of-stack call frame.
        inline void set_frame(Frame* current_frame)
        {
            this->vm.current_frame = current_frame;
        }

        // See VM::alloc_frame().
        inline Frame* alloc_frame(uint32_t num_regs, uint32_t num_data, Value v_code,
                                  Value v_module, Value v_marker)
        {
            return this->vm.alloc_frame(num_regs, num_data, v_code, v_module, v_marker);
        }

        // See VM::alloc_frames().
        inline Frame* alloc_frames(uint64_t total_length)
        {
            return this->vm.alloc_frames(total_length);
        }

        inline void unwind_frame(bool tail_call)
        {
            this->vm.unwind_frame(tail_call);
        }

        VM& vm;
        GC& gc;
    };
};

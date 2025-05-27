#pragma once

#include "gc.h"

namespace Katsu
{
    /*
     * Bytecodes:
     * - LOAD_REG: load local register by index onto stack
     * - STORE_REG: pop from stack to local register
     * - LOAD_REF: load local register by index, then dereference _that_, to push to stack
     * - STORE_REF: pop from stack into pointee of reference object
     * - LOAD_VALUE: load a Value (contents may be managed by GC!) onto stack
     * - LOAD_MODULE: load a value by name from the call frame's module onto stack
     * - STORE_MODULE: pop from stack to module variable
     * - INVOKE: look up (by name) a multimethod in the global multimethod store, pop arguments, and
     *   call the method (the lookup should probably be pre-calculated...)
     * - DROP: pop a value from the stack
     * - MAKE_TUPLE: pop some values from the stack, push new tuple with those values
     * - MAKE_VECTOR: pop some values from the stack, push new tuple with those values
     * - MAKE_CLOSURE: push a closure object (which refers to some closed-over variables) to the
     *   stack
     *
     * Bytecode format: there are separate instruction and value regions (which should be right next
     * to each other anyway to keep cache warm) -- and also an offside array of SourceSpan per
     * instruction
     * - instructions array: concatenated sequence of <1-byte opcode> <3-byte args offset>, where
     *   the offsets are in terms of 8-byte Values in the value/argument region
     * - value array: aligned 8-byte Values (could be inline or reference), which should be
     *   considered roots for the GC
     *
     * +--------------+--------+----------------------------------------------------------+
     * | Name         | Opcode | Arguments ...                                            |
     * +--------------+--------+----------------------------------------------------------+
     * | LOAD_REG     |  0x1   | (fixnum) local index                                     |
     * | STORE_REG    |  0x2   | (fixnum) local index                                     |
     * | LOAD_REF     |  0x3   | (fixnum) local index                                     |
     * | STORE_REF    |  0x4   | (fixnum) local index                                     |
     * | LOAD_VALUE   |  0x5   | value to load                                            |
     * | LOAD_MODULE  |  0x6   | (string) name                                            |
     * | STORE_MODULE |  0x7   | (string) name                                            |
     * | INVOKE       |  0x8   | (string) name; (fixnum) num args                     (1) |
     * | DROP         |  0x9   | none                                                     |
     * | MAKE_TUPLE   |  0xA   | (fixnum) num components                                  |
     * | MAKE_VECTOR  |  0xB   | (fixnum) num components                                  |
     * | MAKE_CLOSURE |  0xC   | TODO (represent closure 'template')                  (2) |
     * +--------------+--------+----------------------------------------------------------+
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
        LOAD_MODULE,
        STORE_MODULE,
        INVOKE,
        DROP,
        MAKE_TUPLE,
        MAKE_VECTOR,
        MAKE_CLOSURE,
    };

    struct Frame
    {
        Value v_code; // Code

        // Current index in the code's insts / args arrays.
        uint32_t inst_spot;
        uint32_t arg_spot;

        // Number of `regs()`.
        uint64_t num_regs;
        // (Maximum) number of `data()`.
        uint64_t num_data;
        // Current size of the data stack (up to `num_data`).
        uint64_t data_depth;

        // A callable to invoke while unwinding this frame, or null to indicate no cleanup action.
        Value v_cleanup;

        // Whether this frame is itself the first frame from invoking another frame's cleanup
        // action.
        bool is_cleanup;

        Value v_module; // Module

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
    };
    static_assert(sizeof(Frame) % sizeof(Value) == 0);

    class VM : public RootProvider
    {
    public:
        VM(GC& gc, uint64_t call_stack_size);
        ~VM();

        ValueRoot eval_toplevel(Root<Code>& r_code);

        void visit_roots(std::function<void(Value*)>& visitor) override;

    private:
        void single_step();

        // Look up the method_name in the module, following v_base until null.
        static Value& module_lookup_or_fail(Value v_module, String* name);

        // Invoke a value (which could be a closure or multimethod) with some arguments. The
        // arguments may be just past the end of the current frame's data stack.
        void invoke(Value v_callable, int64_t num_args, Value* args);

        GC& gc;
        // Memory region for the call stack.
        // Hosts contiguous `Frame`s.
        uint8_t* call_stack_mem;
        uint64_t call_stack_size;

        // null, or points into the call_stack_mem.
        Frame* current_frame;
    };
};

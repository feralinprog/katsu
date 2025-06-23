#include "vm.h"

#include "assertions.h"
#include "value_utils.h"

#include <algorithm>
#include <cstring>

#include <iostream>

namespace Katsu
{
    VM::VM(GC& _gc, uint64_t call_stack_size)
        : gc(_gc)
    {
        ASSERT_ARG_MSG((call_stack_size & TAG_MASK) == 0,
                       "call_stack_size must be TAG_BITS-aligned");

        this->call_stack_mem =
            reinterpret_cast<uint8_t*>(aligned_alloc(1 << TAG_BITS, call_stack_size));
        if (!this->call_stack_mem) {
            throw std::bad_alloc();
        }
        this->call_stack_size = call_stack_size;

        this->current_frame = nullptr;

        for (size_t i = 0; i < BuiltinId::NUM_BUILTINS; i++) {
            this->builtin_values[i] = Value::null();
        }

        this->gc.root_providers.push_back(this);
    }

    VM::~VM()
    {
        if (this->call_stack_mem) {
            free(this->call_stack_mem);
        }

        this->gc.root_providers.erase(
            std::find(this->gc.root_providers.begin(), this->gc.root_providers.end(), this));
    }

    void VM::visit_roots(std::function<void(Value*)>& visitor)
    {
        for (Value& builtin : this->builtin_values) {
            visitor(&builtin);
        }

        Frame* frame = reinterpret_cast<Frame*>(this->call_stack_mem);
        while (frame <= this->current_frame) {
            visitor(&frame->v_code);
            visitor(&frame->v_module);
            visitor(&frame->v_marker);

            for (uint32_t i = 0; i < frame->num_regs; i++) {
                visitor(&frame->regs()[i]);
            }

            // Note that we don't need to go visit all the way to num_data.
            // Only up to data_depth is guaranteed valid.
            for (uint32_t i = 0; i < frame->data_depth; i++) {
                visitor(&frame->data()[i]);
            }

            frame = frame->next();
        }
    }

    void VM::register_builtin(BuiltinId id, Value value)
    {
        ASSERT(id >= 0 && id < BuiltinId::NUM_BUILTINS);
        ASSERT(this->builtin_values[id] == Value::null());
        this->builtin_values[id] = value;
    }

    Value VM::eval_toplevel(Root<Code>& r_code)
    {
        ASSERT_MSG(!this->current_frame,
                   "shouldn't already have a call frame if eval-ing at top level");

        ASSERT_MSG(r_code->v_insts.obj_array()->length > 0, "code must not be empty");

        uint32_t code_num_regs = r_code->num_regs;
        uint32_t code_num_data = r_code->num_data;

        Frame* frame = this->alloc_frame(code_num_regs,
                                         code_num_data,
                                         r_code.value(),
                                         r_code->v_module,
                                         /* v_marker */ Value::null());
        for (uint32_t i = 0; i < code_num_regs; i++) {
            frame->regs()[i] = Value::null();
        }
        this->current_frame = frame;

        while (true) {
            // this->print_vm_state();

            Code* frame_code = this->current_frame->v_code.obj_code();
            Array* frame_insts = frame_code->v_insts.obj_array();
            if (reinterpret_cast<uint8_t*>(this->current_frame) == this->call_stack_mem) {
                // There is only a single frame in the call stack; check if we're done.
                bool finished_instructions = this->current_frame->inst_spot == frame_insts->length;
                if (finished_instructions) {
                    ASSERT(this->current_frame->data_depth == 1);
                    Value v_return_value = this->current_frame->data()[0];
                    this->current_frame = nullptr;
                    return v_return_value;
                }
            }
            single_step();
        }
    }

    void VM::print_vm_state()
    {
        Frame* frame = reinterpret_cast<Frame*>(this->call_stack_mem);
        std::cout << "=== CALL STACK (GROWING TOP TO BOTTOM) ===\n";
        while (frame <= this->current_frame) {
            std::cout << "--- CALL FRAME ---\n";

            std::cout << "v_code: ";
            pprint(frame->v_code, /* initial_indent */ false);
            std::cout << "inst_spot = " << frame->inst_spot << "\n";
            std::cout << "num_regs = " << frame->num_regs << "\n";
            std::cout << "num_data = " << frame->num_data << "\n";
            std::cout << "data_depth = " << frame->data_depth << "\n";
            // TODO: show v_module only if folding is possible, otherwise too noisy.
            std::cout << "v_marker: ";
            pprint(frame->v_marker, /* initial_indent */ false);

            std::cout << "regs:\n";
            for (uint32_t i = 0; i < frame->num_regs; i++) {
                std::cout << "- @" << i << " = ";
                pprint(frame->regs()[i], /* initial_indent */ false, /* depth */ 1);
            }
            std::cout << "data:\n";
            for (uint32_t i = 0; i < frame->data_depth; i++) {
                std::cout << "- " << i << " = ";
                pprint(frame->data()[i], /* initial_indent */ false, /* depth */ 1);
            }

            frame = frame->next();
        }
    }

    inline void VM::single_step()
    {
        Code* frame_code = this->current_frame->v_code.obj_code();
        Array* frame_insts = frame_code->v_insts.obj_array();
        Array* frame_args = frame_code->v_args.obj_array();

        uint64_t num_insts = frame_insts->length;
        if (this->current_frame->inst_spot == num_insts) {
            this->unwind_frame(/* tail_call */ false);
            return;
        }

        if (this->current_frame->inst_spot > num_insts) [[unlikely]] {
            ASSERT_MSG(false, "shifted beyond instructions array in call frame");
        }

        int64_t inst = frame_insts->components()[this->current_frame->inst_spot].fixnum();
        ASSERT(0 <= inst && inst < UINT32_MAX);
        OpCode op = static_cast<OpCode>((uint32_t)inst & 0xFF);
        uint32_t arg_spot = (uint32_t)inst >> 8;

        auto shift_inst = [this]() -> void { this->current_frame->inst_spot++; };
        auto arg = [frame_args, arg_spot](int offset = 0) -> Value {
            ASSERT(arg_spot + offset >= 0 && arg_spot + offset < frame_args->length);
            return frame_args->components()[arg_spot + offset];
        };

        switch (op) {
            case OpCode::LOAD_REG: {
                this->current_frame->push(this->current_frame->regs()[arg().fixnum()]);
                shift_inst();
                break;
            }
            case OpCode::STORE_REG: {
                this->current_frame->regs()[arg().fixnum()] = this->current_frame->pop();
                shift_inst();
                break;
            }
            case OpCode::LOAD_REF: {
                this->current_frame->push(
                    this->current_frame->regs()[arg().fixnum()].obj_ref()->v_ref);
                shift_inst();
                break;
            }
            case OpCode::STORE_REF: {
                this->current_frame->regs()[arg().fixnum()].obj_ref()->v_ref =
                    this->current_frame->pop();
                shift_inst();
                break;
            }
            case OpCode::LOAD_VALUE: {
                this->current_frame->push(arg());
                shift_inst();
                break;
            }
            case OpCode::INIT_REF: {
                // arg() is invalidated by any GC access, so acquire the local index ahead of time.
                int64_t local_index = arg().fixnum();
                ValueRoot r_ref(this->gc, this->current_frame->pop());
                this->current_frame->regs()[local_index] = Value::object(make_ref(this->gc, r_ref));
                shift_inst();
                break;
            }
            case OpCode::LOAD_MODULE: {
                this->current_frame->push(
                    module_lookup_or_fail(this->current_frame->v_module, arg().obj_string()));
                shift_inst();
                break;
            }
            case OpCode::STORE_MODULE: {
                Value& slot =
                    module_lookup_or_fail(this->current_frame->v_module, arg().obj_string());
                slot = this->current_frame->pop();
                shift_inst();
                break;
            }
            case OpCode::INVOKE:
            case OpCode::INVOKE_TAIL: {
                Value v_method =
                    module_lookup_or_fail(this->current_frame->v_module, arg(+0).obj_string());
                int64_t num_args = arg(+1).fixnum();
                // TODO: check uint32_t
                Value* args = this->current_frame->pop_many(num_args);

                bool tail_call = op == OpCode::INVOKE_TAIL;

                // invoke() takes care of shifting the instruction spot.
                this->invoke(v_method, tail_call, num_args, args);
                break;
            }
            case OpCode::DROP: {
                this->current_frame->pop();
                shift_inst();
                break;
            }
            case OpCode::MAKE_TUPLE: {
                // arg() is invalidated by any GC access, so acquire num_components ahead of
                // time.
                auto num_components = arg().fixnum();
                // TODO: check >= 0
                Tuple* tuple = make_tuple_nofill(this->gc, num_components);
                // TODO: check uint32_t
                Value* components = this->current_frame->pop_many(num_components);
                for (int64_t i = 0; i < num_components; i++) {
                    tuple->components()[i] = components[i];
                }
                this->current_frame->push(Value::object(tuple));
                shift_inst();
                break;
            }
            case OpCode::MAKE_ARRAY: {
                // arg() is invalidated by any GC access, so acquire num_components ahead of
                // time.
                auto num_components = arg().fixnum();
                // TODO: check >= 0
                Array* array = make_array_nofill(this->gc, num_components);
                // TODO: check uint32_t
                Value* components = this->current_frame->pop_many(num_components);
                for (int64_t i = 0; i < num_components; i++) {
                    array->components()[i] = components[i];
                }
                this->current_frame->push(Value::object(array));
                shift_inst();
                break;
            }
            case OpCode::MAKE_VECTOR: {
                // arg() is invalidated by any GC access, so acquire num_components ahead of
                // time.
                auto num_components = arg().fixnum();
                // TODO: check >= 0
                Array* array = make_array_nofill(this->gc, num_components);
                // TODO: check uint32_t
                Value* components = this->current_frame->pop_many(num_components);
                for (int64_t i = 0; i < num_components; i++) {
                    array->components()[i] = components[i];
                }
                Vector* vec = make_vector(this->gc, /* length */ num_components, array);
                this->current_frame->push(Value::object(vec));
                shift_inst();
                break;
            }
            case OpCode::MAKE_CLOSURE: {
                // arg() is invalidated by any GC access, so acquire the closure's Code ahead of
                // time.
                Root<Code> r_code(this->gc, arg().obj_code());
                uint64_t num_upregs = r_code->v_upreg_map.obj_array()->length;

                Root<Array> r_upregs(this->gc,
                                     make_array(this->gc, num_upregs)); // null-initialized
                Closure* closure =
                    make_closure(this->gc, /* r_code */ r_code, /* r_upregs */ r_upregs);

                // Copy from the current stack frame's data stack into the closure's upregs.
                // TODO: check uint32_t
                Value* upreg_vals = this->current_frame->pop_many(num_upregs);
                Array* upregs = *r_upregs;
                for (uint64_t i = 0; i < num_upregs; i++) {
                    upregs->components()[i] = upreg_vals[i];
                }

                this->current_frame->push(Value::object(closure));
                shift_inst();
                break;
            }
            case OpCode::MAKE_INSTANCE: {
                // arg() is invalidated by any GC access, so acquire num_slots ahead of time.
                auto num_slots = arg().fixnum();
                // TODO: check >= 0
                // Peek instead of pop so we keep the values live.
                Value* type_and_slots = this->current_frame->peek_many(1 + num_slots);
                Root<Type> r_type(this->gc, type_and_slots[0].obj_type());
                DataclassInstance* inst = make_instance_nofill(this->gc, r_type);
                // Now we can pop, since there's no further allocation.
                type_and_slots = this->current_frame->pop_many(1 + num_slots);
                Value* slots = type_and_slots + 1;
                for (int64_t i = 0; i < num_slots; i++) {
                    inst->slots()[i] = slots[i];
                }
                this->current_frame->push(Value::object(inst));
                shift_inst();
                break;
            }
            case OpCode::VERIFY_IS_TYPE: {
                Value value = this->current_frame->peek();
                if (!value.is_obj_type()) {
                    throw std::runtime_error("value must be a Type");
                }
                shift_inst();
                break;
            }
            case OpCode::GET_SLOT: {
                // arg() is invalidated by any GC access, so acquire slot_index ahead of time.
                auto slot_index = arg().fixnum();
                DataclassInstance* inst = this->current_frame->pop().obj_instance();
                // TODO: check within bounds
                this->current_frame->push(inst->slots()[slot_index]);
                shift_inst();
                break;
            }
            case OpCode::SET_SLOT: {
                // arg() is invalidated by any GC access, so acquire slot_index ahead of time.
                auto slot_index = arg().fixnum();
                Value value = this->current_frame->pop();
                DataclassInstance* inst = this->current_frame->pop().obj_instance();
                // TODO: check within bounds
                inst->slots()[slot_index] = value;
                shift_inst();
                break;
            }
            default: {
                ALWAYS_ASSERT_MSG(false, "forgot an OpCode");
            }
        }
    }

    void VM::unwind_frame(bool tail_call)
    {
#if DEBUG_ASSERTIONS
        Code* frame_code = this->current_frame->v_code.obj_code();
        Array* frame_insts = frame_code->v_insts.obj_array();
        // Make sure all instructions were used.
        ASSERT(this->current_frame->inst_spot == frame_insts->length);
#endif

        // Unwind the frame!
        ASSERT(this->current_frame->caller);
        Frame* caller = this->current_frame->caller;
        if (tail_call) {
            // Caller must set up a new call frame which will produce another value, and when
            // unwinding from _that_, the return value will go to the `caller`'s data stack. In this
            // case, the current frame's data depth need not be 1, since the state is that all
            // arguments to the upcoming tail-call should be pushed to the stack, rather than the
            // result of that call.
        } else {
            ASSERT(this->current_frame->data_depth == 1);
            ASSERT_MSG(caller->data_depth < caller->num_data,
                       "unwinding would overflow caller's data stack");
            caller->push(this->current_frame->data()[0]);
        }
        this->current_frame = caller;
    }

    Value& VM::module_lookup_or_fail(Value v_module, String* name)
    {
        Value* lookup = module_lookup(v_module.obj_module(), name);
        ASSERT_MSG(lookup, "didn't find invocation name in module");
        return *lookup;
    }

    Frame* VM::alloc_frame(uint32_t num_regs, uint32_t num_data, Value v_code, Value v_module,
                           Value v_marker)
    {
        Frame* frame = this->current_frame ? this->current_frame->next()
                                           : reinterpret_cast<Frame*>(this->call_stack_mem);
        size_t frame_size = Frame::size(num_regs, num_data);
        if (reinterpret_cast<uint8_t*>(frame) + frame_size >
            this->call_stack_mem + this->call_stack_size) {
            throw std::runtime_error("katsu stack overflow");
        }

        // Help with debugging.
        std::memset(frame, 0x56, frame_size);

        frame->caller = this->current_frame;
        frame->v_code = v_code;
        frame->inst_spot = 0;
        frame->num_regs = num_regs;
        frame->num_data = num_data;
        frame->data_depth = 0;
        frame->v_module = v_module;
        frame->v_marker = v_marker;
        // regs() / data() is up to caller to initialize as desired.
        return frame;
    }

    Frame* VM::alloc_frames(uint64_t total_length)
    {
        Frame* bottom_frame = this->current_frame ? this->current_frame->next()
                                                  : reinterpret_cast<Frame*>(this->call_stack_mem);
        if (reinterpret_cast<uint8_t*>(bottom_frame) + total_length >
            this->call_stack_mem + this->call_stack_size) {
            throw std::runtime_error("katsu stack overflow");
        }
        return reinterpret_cast<Frame*>(reinterpret_cast<uint8_t*>(bottom_frame) + total_length);
    }

    // Doesn't allocate.
    bool params_match(VM& vm, Array* param_matchers, Value* args)
    {
        uint32_t i = 0;
        for (Value matcher : param_matchers) {
            Value arg = args[i++];

            if (matcher.is_null()) {
                continue;
            } else if (matcher.is_obj_type()) {
                Type* t = matcher.obj_type();
                if (!is_instance(vm, arg, t)) {
                    return false;
                }
            } else if (matcher.is_obj_ref()) {
                Ref* r = matcher.obj_ref();
                // TODO: identity? or more general equality?
                if (arg != r->v_ref) {
                    return false;
                }
            } else {
                ASSERT_MSG(false, "missed a param matcher type");
            }
        }
        return true;
    }

    bool operator<=(Value param_matcher_a, Value param_matcher_b)
    {
        // Ordering: value matchers < type matchers < any matchers.
        // Within type matchers, matcher A <= matcher B if the type for A is a subtype of the type
        // for B.
        if (param_matcher_b.is_null()) {
            return true;
        } else if (param_matcher_b.is_obj_type()) {
            if (param_matcher_a.is_null()) {
                return false;
            } else if (param_matcher_a.is_obj_type()) {
                return is_subtype(param_matcher_a.obj_type(), param_matcher_b.obj_type());
            } else {
                ASSERT_MSG(param_matcher_a.is_obj_ref(), "missed a param matcher type");
                // param_matcher_a is a value matcher
                return true;
            }
        } else {
            ASSERT_MSG(param_matcher_b.is_obj_ref(), "missed a param matcher type");
            // param_matcher_b is a value matcher
            if (param_matcher_a.is_obj_ref()) {
                // TODO: identity? or more general equality?
                return param_matcher_a.obj_ref()->v_ref == param_matcher_b.obj_ref()->v_ref;
            } else {
                return false;
            }
        }
    }

    bool operator<=(Array& param_matchers_a, Array& param_matchers_b)
    {
        ASSERT(param_matchers_a.length == param_matchers_b.length);
        for (uint64_t i = 0; i < param_matchers_a.length; i++) {
            if (!(param_matchers_a.components()[i] <= param_matchers_b.components()[i])) {
                return false;
            }
        }
        return true;
    }

    bool operator<=(Method& method_a, Method& method_b)
    {
        Array* matchers_a = method_a.v_param_matchers.obj_array();
        Array* matchers_b = method_b.v_param_matchers.obj_array();
        return *matchers_a <= *matchers_b;
    }

    // Doesn't allocate!
    Method* multimethod_dispatch(VM& vm, MultiMethod* multimethod, Value* args)
    {
        Vector* methods = multimethod->v_methods.obj_vector();
#if DEBUG_ASSERTIONS
        for (Value v_method : methods) {
            ASSERT(v_method.is_obj_method());
            ASSERT(v_method.obj_method()->v_param_matchers.obj_array()->length ==
                   multimethod->num_params);
        }
#endif

        // TODO: optimize this (by a lot!).
        // Perform two passes:
        // 1) Find any minimum among methods matching the arguments -- assuming one even exists!
        //    (Otherwise, error: no matching method.)
        // 2) Ensure it is a global minimum among methods matching the arguments.
        //    (Otherwise, error: ambiguous method resolution.)
        // The ordering among methods is the product partial ordering induced by parameter matcher
        // ordering, with value matchers < type matchers < any matchers, and within type matchers,
        // naturally a matcher for type A is less than a matcher for type B if and only if A is a
        // strict subtype of B.

        // Pass 1:
        Method* min = nullptr;
        for (Value v_method : methods) {
            Method* method = v_method.obj_method();
            Array* matchers = method->v_param_matchers.obj_array();
            if (!params_match(vm, matchers, args)) {
                continue;
            }
            if (!min || *method <= *min) {
                min = method;
            }
        }
        if (!min) {
            // TODO: raise to katsu
            throw std::runtime_error("no matching methods");
        }

        // Pass 2:
        for (Value v_method : methods) {
            Method* method = v_method.obj_method();
            Array* matchers = method->v_param_matchers.obj_array();
            if (!params_match(vm, matchers, args)) {
                continue;
            }
            if (!(*min <= *method)) {
                throw std::runtime_error("ambiguous method resolution");
            }
        }

        return min;
    }

    void VM::invoke(Value v_callable, bool tail_call, int64_t num_args, Value* args)
    {
        if (!v_callable.is_obj_multimethod()) {
            // TODO: make this a katsu runtime error instead
            throw std::runtime_error("can only invoke a multimethod");
        }
        MultiMethod* multimethod = v_callable.obj_multimethod();

        ASSERT(num_args == multimethod->num_params);
        Method* method = multimethod_dispatch(*this, multimethod, args);

        if (method->v_code.is_null()) {
            // Native or intrinsic handler.
            if (method->native_handler) {
                // Note that we cannot support tail-calls here. However, it's not really an issue,
                // since native handler shouldn't add to the Katsu call stack themselves; as soon as
                // the handler returns the Katsu call frame should be complete as well.
                this->current_frame->inst_spot++;
                Value v_result = method->native_handler(*this, num_args, args);
                this->current_frame->push(v_result);
            } else if (method->intrinsic_handler) {
                // Handler takes care of updating inst_spot.
                OpenVM open(*this);
                method->intrinsic_handler(open, tail_call, num_args, args);
            } else {
                ASSERT_MSG(false,
                           "method must have v_code or a native_handler or intrinsic_handler");
            }
        } else {
            this->current_frame->inst_spot++;

            // In case of tail-call, we need to temporarily store the args as we unwind the current
            // frame and replace it with a new frame.
            Value args_copy[num_args];
            if (tail_call) {
                for (uint32_t i = 0; i < num_args; i++) {
                    args_copy[i] = args[i];
                }
                this->unwind_frame(/* tail_call */ true);
                args = args_copy;
            }

            // Bytecode body.
            Code* code = method->v_code.obj_code();
            ASSERT_MSG(code->v_upreg_map.is_null(), "method's v_code's v_upreg_map should be null");
            ASSERT(num_args == code->num_params);

            Frame* frame = this->alloc_frame(code->num_regs,
                                             code->num_data,
                                             method->v_code,
                                             code->v_module,
                                             /* v_marker */ Value::null());
            for (uint32_t i = 0; i < num_args; i++) {
                frame->regs()[i] = args[i];
            }
            for (uint32_t i = num_args; i < code->num_regs; i++) {
                frame->regs()[i] = Value::null();
            }
            this->current_frame = frame;
        }
    }
};

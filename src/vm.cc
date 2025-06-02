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
            visitor(&frame->v_cleanup);
            visitor(&frame->v_module);

            for (uint32_t i = 0; i < frame->num_regs; i++) {
                visitor(&frame->regs()[i]);
            }

            // Note that we don't need to go visit all the way to num_data.
            // Only up to data_depth is guaranteed valid.
            for (uint32_t i = 0; i < frame->data_depth; i++) {
                visitor(&frame->data()[i]);
            }

            frame = reinterpret_cast<Frame*>(
                align_up(reinterpret_cast<uint64_t>(frame) + frame->size(), TAG_BITS));
        }
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
                                         /* v_cleanup */ Value::null(),
                                         /* is_cleanup */ false,
                                         r_code->v_module);
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
                bool no_cleanup = this->current_frame->v_cleanup.is_null();
                if (finished_instructions && no_cleanup) {
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
            std::cout << "arg_spot = " << frame->arg_spot << "\n";
            std::cout << "num_regs = " << frame->num_regs << "\n";
            std::cout << "num_data = " << frame->num_data << "\n";
            std::cout << "data_depth = " << frame->data_depth << "\n";
            std::cout << "v_cleanup: ";
            pprint(frame->v_cleanup, /* initial_indent */ false);
            std::cout << "is_cleanup = " << frame->is_cleanup << "\n";
            // TODO: show v_module only if folding is possible, otherwise too noisy.

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

            frame = reinterpret_cast<Frame*>(
                align_up(reinterpret_cast<uint64_t>(frame) + frame->size(), TAG_BITS));
        }
    }

    inline void VM::single_step()
    {
        Code* frame_code = this->current_frame->v_code.obj_code();
        Array* frame_insts = frame_code->v_insts.obj_array();
        Array* frame_args = frame_code->v_args.obj_array();

        auto arg = [this, frame_args](int offset = 0) -> Value {
            ASSERT(this->current_frame->arg_spot + offset >= 0 &&
                   this->current_frame->arg_spot + offset < frame_args->length);
            return frame_args->components()[this->current_frame->arg_spot + offset];
        };

        auto shift_inst = [this]() -> void { this->current_frame->inst_spot++; };
        auto shift_arg = [this](int count = 1) -> void { this->current_frame->arg_spot += count; };

        uint64_t num_insts = frame_insts->length;
        if (this->current_frame->inst_spot == num_insts) {
            // Make sure all arguments were used.
            ASSERT(this->current_frame->arg_spot == frame_args->length);

            // Unwind the frame!
            // TODO: implement unwinding with cleanup
            ASSERT_MSG(this->current_frame->v_cleanup.is_null() && !this->current_frame->is_cleanup,
                       "unwinding with cleanup not yet implemented");
            ASSERT(this->current_frame->caller);
            ASSERT(this->current_frame->data_depth == 1);
            Frame* caller = this->current_frame->caller;
            ASSERT_MSG(caller->data_depth < caller->num_data,
                       "unwinding would overflow caller's data stack");
            caller->push(this->current_frame->data()[0]);
            this->current_frame = caller;

            return;
        }

        if (this->current_frame->inst_spot > num_insts) [[unlikely]] {
            ASSERT_MSG(false, "shifted beyond instructions array in call frame");
        }

        int64_t inst = frame_insts->components()[this->current_frame->inst_spot].fixnum();
        switch (inst) {
            case OpCode::LOAD_REG: {
                this->current_frame->push(this->current_frame->regs()[arg().fixnum()]);
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::STORE_REG: {
                this->current_frame->regs()[arg().fixnum()] = this->current_frame->pop();
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::LOAD_REF: {
                this->current_frame->push(
                    this->current_frame->regs()[arg().fixnum()].obj_ref()->v_ref);
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::STORE_REF: {
                this->current_frame->regs()[arg().fixnum()].obj_ref()->v_ref =
                    this->current_frame->pop();
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::LOAD_VALUE: {
                this->current_frame->push(arg());
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::INIT_REF: {
                // arg() is invalidated by any GC access, so acquire the local index ahead of time.
                int64_t local_index = arg().fixnum();
                ValueRoot r_ref(this->gc, this->current_frame->pop());
                this->current_frame->regs()[local_index] = Value::object(make_ref(this->gc, r_ref));
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::LOAD_MODULE: {
                this->current_frame->push(
                    module_lookup_or_fail(this->current_frame->v_module, arg().obj_string()));
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::STORE_MODULE: {
                Value& slot =
                    module_lookup_or_fail(this->current_frame->v_module, arg().obj_string());
                slot = this->current_frame->pop();
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::INVOKE:
            case OpCode::INVOKE_TAIL: {
                Value v_method =
                    module_lookup_or_fail(this->current_frame->v_module, arg(+0).obj_string());
                int64_t num_args = arg(+1).fixnum();
                // TODO: check uint32_t
                Value* args = this->current_frame->pop_many(num_args);

                bool tail_call = inst == OpCode::INVOKE_TAIL;

                // invoke() takes care of shifting the instruction / arg spots.
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
                shift_arg();
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
                shift_arg();
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
                shift_arg();
                break;
            }
            default: {
                ALWAYS_ASSERT_MSG(false, "forgot an OpCode");
            }
        }
    }

    Value& VM::module_lookup_or_fail(Value v_module, String* name)
    {
        Value* lookup = module_lookup(v_module.obj_module(), name);
        ASSERT_MSG(lookup, "didn't find invocation name in module");
        return *lookup;
    }

    Frame* VM::alloc_frame(uint32_t num_regs, uint32_t num_data, Value v_code, Value v_cleanup,
                           bool is_cleanup, Value v_module)
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
        frame->arg_spot = 0;
        frame->num_regs = num_regs;
        frame->num_data = num_data;
        frame->data_depth = 0;
        frame->v_cleanup = Value::null();
        frame->is_cleanup = false;
        frame->v_module = v_module;
        // regs() / data() is up to caller to initialize as desired.
        return frame;
    }

    void VM::invoke(Value v_callable, bool tail_call, int64_t num_args, Value* args)
    {
        if (!v_callable.is_obj_multimethod()) {
            // TODO: make this a katsu runtime error instead
            throw std::runtime_error("can only invoke a multimethod");
        }
        MultiMethod* multimethod = v_callable.obj_multimethod();

        // TODO: actually do a proper multimethod dispatch
        Vector* methods = multimethod->v_methods.obj_vector();
        if (methods->length == 0) {
            // TODO: make this a katsu runtime error instead
            throw std::runtime_error("need a method in multimethod");
        }
        Value v_method = methods->v_array.obj_array()->components()[0];
        Method* method = v_method.obj_method();

        if (method->v_code.is_null()) {
            // Native or intrinsic handler.
            if (method->native_handler) {
                // Note that we cannot support tail-calls here. However, it's not really an issue,
                // since native handler shouldn't add to the Katsu call stack themselves; as soon as
                // the handler returns the Katsu call frame should be complete as well.
                this->current_frame->inst_spot++;
                this->current_frame->arg_spot += 2;
                Value v_result = method->native_handler(*this, num_args, args);
                this->current_frame->push(v_result);
            } else if (method->intrinsic_handler) {
                // Handler takes care of updating inst_spot / arg_spot.
                OpenVM open(*this);
                method->intrinsic_handler(open, tail_call, num_args, args);
            } else {
                ASSERT_MSG(false,
                           "method must have v_code or a native_handler or intrinsic_handler");
            }
        } else {
            // TODO: support tail-calls for bytecode
            ASSERT_MSG(!tail_call, "tail-call not implemented");
            this->current_frame->inst_spot++;
            this->current_frame->arg_spot += 2;

            // Bytecode body.
            Code* code = method->v_code.obj_code();
            ASSERT_MSG(code->v_upreg_map.is_null(), "method's v_code's v_upreg_map should be null");
            ASSERT(num_args == code->num_params);

            Frame* frame = this->alloc_frame(code->num_regs,
                                             code->num_data,
                                             method->v_code,
                                             /* v_cleanup */ Value::null(),
                                             /* is_cleanup */ false,
                                             code->v_module);
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

#include "vm.h"

#include "value_utils.h"
#include <algorithm>
#include <cstring>

namespace Katsu
{
    VM::VM(GC& _gc, uint64_t call_stack_size)
        : gc(_gc)
    {
        if ((call_stack_size & TAG_MASK) != 0) {
            throw std::invalid_argument("call_stack_size must be TAG_BITS-aligned");
        }

        this->call_stack_mem =
            reinterpret_cast<uint8_t*>(aligned_alloc(1 << TAG_BITS, call_stack_size));
        if (!this->call_stack_mem) {
            throw std::bad_alloc();
        }
        this->call_stack_size = call_stack_size;

        this->current_frame = nullptr;

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
        // TODO: scan stack frames to add roots
    }

    Value VM::eval_toplevel(Value v_code)
    {
        if (this->current_frame) {
            throw std::logic_error("shouldn't already have a call frame if eval-ing at top level");
        }

        Root r_code(this->gc, std::move(v_code));
        uint32_t code_num_regs = r_code.get().obj_code()->num_regs;
        uint32_t code_num_data = r_code.get().obj_code()->num_data;

        // auto next_frame_mem =
        // reinterpret_cast<uint8_t*>(align_up(reinterpret_cast<uint64_t>(this->current_frame),
        // TAG_BITS));
        uint8_t* next_frame_mem = this->call_stack_mem;
        uint64_t next_frame_size = sizeof(Frame) + (code_num_regs + code_num_data) * sizeof(Value);
        if (next_frame_mem + next_frame_size > this->call_stack_mem + this->call_stack_size) {
            throw std::runtime_error("stack overflow");
        }

        // Help with debugging.
        std::memset(next_frame_mem, 0x1234ABCD, next_frame_size);

        auto frame = reinterpret_cast<Frame*>(next_frame_mem);
        frame->v_code = r_code.get();
        frame->inst_spot = 0;
        frame->arg_spot = 0;
        frame->num_regs = code_num_regs;
        frame->num_data = code_num_data;
        frame->data_depth = 0;
        frame->v_cleanup = Value::null();
        frame->is_cleanup = false;
        frame->v_module = r_code.get().obj_code()->v_module;
        this->current_frame = frame;

        while (true) {
            Code* frame_code = this->current_frame->v_code.obj_code();
            Array* frame_insts = frame_code->v_insts.obj_array();
            if (reinterpret_cast<uint8_t*>(this->current_frame) == this->call_stack_mem) {
                // There is only a single frame in the call stack; check if we're done.
                bool finished_instructions = this->current_frame->inst_spot == frame_insts->length;
                bool no_cleanup = this->current_frame->v_cleanup.is_null();
                if (finished_instructions && no_cleanup) {
                    Value v_return_value = this->current_frame->data()[0];
                    this->current_frame = nullptr;
                    return v_return_value;
                }
            }
            single_step();
        }
    }

    inline void VM::single_step()
    {
        Code* frame_code = this->current_frame->v_code.obj_code();
        Array* frame_insts = frame_code->v_insts.obj_array();
        Array* frame_args = frame_code->v_args.obj_array();

        auto push = [this](Value value) -> void {
            this->current_frame->data()[this->current_frame->data_depth++] = value;
        };
        auto pop = [this]() -> Value {
            Value* v_top = &this->current_frame->data()[this->current_frame->data_depth];
            Value v_popped = *v_top;
            *v_top = Value::null();
            this->current_frame->data_depth--;
            return v_popped;
        };

        auto arg = [this, frame_args](int offset = 0) -> Value {
            return frame_args->components()[this->current_frame->arg_spot + offset];
        };

        auto shift_inst = [this]() -> void { this->current_frame->inst_spot++; };
        auto shift_arg = [this](int count = 1) -> void { this->current_frame->arg_spot += count; };

        uint64_t num_insts = frame_insts->length;
        if (this->current_frame->inst_spot == num_insts) {
            // TODO: unwind frame and/or invoke cleanup
        }

        if (this->current_frame->inst_spot > num_insts) [[unlikely]] {
            throw std::logic_error("got beyond instructions in frame");
        }

        int64_t inst = frame_insts->components()[this->current_frame->inst_spot].fixnum();
        switch (inst) {
            case OpCode::LOAD_REG: {
                push(this->current_frame->regs()[arg().fixnum()]);
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::STORE_REG: {
                this->current_frame->regs()[arg().fixnum()] = pop();
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::LOAD_REF: {
                push(this->current_frame->regs()[arg().fixnum()].obj_ref()->v_ref);
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::STORE_REF: {
                this->current_frame->regs()[arg().fixnum()].obj_ref()->v_ref = pop();
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::LOAD_VALUE: {
                push(arg());
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::LOAD_MODULE: {
                push(module_lookup_or_fail(this->current_frame->v_module, arg().obj_string()));
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::STORE_MODULE: {
                Value& slot =
                    module_lookup_or_fail(this->current_frame->v_module, arg().obj_string());
                slot = pop();
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::INVOKE: {
                Value v_method =
                    module_lookup_or_fail(this->current_frame->v_module, arg(+0).obj_string());
                int64_t num_args = arg(+1).fixnum();
                this->current_frame->data_depth -= num_args;

                shift_inst();
                shift_arg(2);

                this->invoke(v_method,
                             num_args,
                             this->current_frame->data() + this->current_frame->data_depth);
                break;
            }
            case OpCode::MAKE_TUPLE: {
                auto num_components = arg().fixnum();
                // TODO: check >= 0
                Tuple* tuple = make_tuple(this->gc,
                                          num_components,
                                          /* components */ this->current_frame->data() +
                                              this->current_frame->data_depth - num_components);
                this->current_frame->data_depth -= num_components;
                for (int64_t i = 0; i < num_components; i++) {
                    Value* component =
                        &this->current_frame->data()[this->current_frame->data_depth + i];
                    *component = Value::null();
                }
                push(Value::object(tuple));
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::MAKE_VECTOR: {
                auto num_components = arg().fixnum();
                // TODO: check >= 0
                Vector* vec = make_vector(gc,
                                          /* capacity */ num_components,
                                          /* length */ num_components,
                                          /* components */ this->current_frame->data() +
                                              this->current_frame->data_depth - num_components);
                this->current_frame->data_depth -= num_components;
                for (int64_t i = 0; i < num_components; i++) {
                    Value* component =
                        &this->current_frame->data()[this->current_frame->data_depth + i];
                    *component = Value::null();
                }
                push(Value::object(vec));
                shift_inst();
                shift_arg();
                break;
            }
            case OpCode::MAKE_CLOSURE: {
                uint32_t num_upregs;
                {
                    Code* code = arg().obj_code();
                    // TODO: check <= UINT32_MAX
                    num_upregs = code->v_upreg_map.obj_array()->length;
                }

                Array* upregs = make_array(gc, num_upregs); // null-initialized
                Root r_upregs(this->gc, Value::object(upregs));

                Closure* closure =
                    make_closure(gc, /* v_code */ arg(), /* v_upregs */ r_upregs.get());
                // Don't need to add the closure as a root; we're done with allocation.
                // Also pull out _upregs again for convenience; it could have moved during closure
                // allocation.
                upregs = r_upregs.get().obj_array();

                // Copy from the current stack frame registers into the closure's upregs.
                Array* upreg_map = arg().obj_code()->v_upreg_map.obj_array();
                for (uint64_t i = 0; i < upreg_map->length; i++) {
                    int64_t src = upreg_map->components()[i].fixnum();
                    upregs->components()[i] = this->current_frame->regs()[src];
                }

                push(Value::object(closure));
                shift_inst();
                shift_arg();
                break;
            }
            default: {
                throw std::logic_error("shouldn't get here");
            }
        }
    }

    Value& VM::module_lookup_or_fail(Value v_module, String* name)
    {
        Value* lookup = module_lookup(v_module.obj_module(), name);
        if (lookup) {
            return *lookup;
        } else {
            throw std::logic_error("didn't find invocation name in module");
        }
    }

    void VM::invoke(Value v_callable, int64_t num_args, Value* args)
    {
        if (!v_callable.is_obj_multimethod()) {
            // TODO: make this a katsu runtime error instead
            throw std::runtime_error("can only invoke a multimethod");
        }
        MultiMethod* multimethod = v_callable.obj_multimethod();

        // TODO: actually do a proper multimethod dispatch
        Vector* methods = multimethod->v_methods.obj_vector();
        if (methods->length == 0) {
            throw std::runtime_error("need a method in multimethod");
        }
        Value v_method = methods->v_array.obj_array()->components()[0];
        Method* method = v_method.obj_method();

        if (method->v_code.is_null()) {
            // Native handler.
            if (!method->native_handler) {
                throw std::logic_error("method must have v_code or a native_handler");
            }
            Value v_result = method->native_handler(*this, num_args, args);
            auto push = [this](Value value) -> void {
                this->current_frame->data()[this->current_frame->data_depth++] = value;
            };
            push(v_result);
        } else {
            // Bytecode body.
            Code* code = method->v_code.obj_code();
            if (!code->v_upreg_map.is_null()) {
                throw std::logic_error("method's v_code's v_upreg_map should be null");
            }

            uint32_t code_num_regs = code->num_regs;
            uint32_t code_num_data = code->num_data;

            uint8_t* next_frame_mem = reinterpret_cast<uint8_t*>(
                align_up(reinterpret_cast<uint64_t>(this->current_frame), TAG_BITS));
            uint64_t next_frame_size =
                sizeof(Frame) + (code_num_regs + code_num_data) * sizeof(Value);
            if (next_frame_mem + next_frame_size > this->call_stack_mem + this->call_stack_size) {
                throw std::runtime_error("stack overflow");
            }

            // Help with debugging.
            std::memset(next_frame_mem, 0x1234ABCD, next_frame_size);

            auto frame = reinterpret_cast<Frame*>(next_frame_mem);
            frame->v_code = method->v_code;
            frame->inst_spot = 0;
            frame->arg_spot = 0;
            frame->num_regs = code_num_regs;
            frame->num_data = code_num_data;
            frame->data_depth = 0;
            frame->v_cleanup = Value::null();
            frame->is_cleanup = false;
            frame->v_module = code->v_module;
            this->current_frame = frame;
        }
    }
};

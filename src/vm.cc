#include "vm.h"

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

        Root code(this->gc, std::move(v_code));
        int64_t code_num_regs =
            code.get().value<Object*>()->object<Code*>()->v_num_regs.value<int64_t>();
        int64_t code_num_data =
            code.get().value<Object*>()->object<Code*>()->v_num_data.value<int64_t>();
        if (code_num_regs < 0) {
            throw std::logic_error("num_regs is negative");
        }
        if (code_num_data < 0) {
            throw std::logic_error("num_data is negative");
        }

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
        frame->v_code = code;
        frame->inst_spot = 0;
        frame->arg_spot = 0;
        frame->num_regs = code_num_regs;
        frame->num_data = code_num_data;
        frame->data_depth = 0;
        frame->v_cleanup = Value::null();
        frame->is_cleanup = false;
        frame->return_id = 0; // TODO
        frame->v_module = code.get().value<Object*>()->object<Code*>()->v_module;
        this->current_frame = frame;

        while (true) {
            Code* frame_code = this->current_frame->v_code.value<Object*>()->object<Code*>();
            Vector* frame_insts = frame_code->v_insts.value<Object*>()->object<Vector*>();
            if (reinterpret_cast<uint8_t*>(this->current_frame) == this->call_stack_mem) {
                // There is only a single frame in the call stack; check if we're done.
                bool finished_instructions =
                    this->current_frame->inst_spot == frame_insts->v_length.value<int64_t>();
                bool no_cleanup = this->current_frame->v_cleanup.tag() == Tag::_NULL;
                if (finished_instructions && no_cleanup) {
                    Value return_value = this->current_frame->data()[0];
                    this->current_frame = nullptr;
                    return return_value;
                }
            }
            single_step();
        }
    }

    inline void VM::single_step()
    {
        Code* frame_code = this->current_frame->v_code.value<Object*>()->object<Code*>();
        Vector* frame_insts = frame_code->v_insts.value<Object*>()->object<Vector*>();
        Vector* frame_args = frame_code->v_args.value<Object*>()->object<Vector*>();

        auto push = [this](Value value) -> void {
            this->current_frame->data()[this->current_frame->data_depth++] = value;
        };
        auto pop = [this]() -> Value {
            Value* top = &this->current_frame->data()[this->current_frame->data_depth];
            Value popped = *top;
            *top = Value::null();
            this->current_frame->data_depth--;
            return popped;
        };

        auto arg = [this, frame_args](int offset = 0) -> Value {
            return frame_args->components()[this->current_frame->arg_spot + offset];
        };

        auto shift_inst = [this]() -> void { this->current_frame->inst_spot++; };
        auto shift_arg = [this](int count = 1) -> void { this->current_frame->arg_spot += count; };

        int64_t num_insts = frame_insts->v_length.value<int64_t>();
        if (this->current_frame->inst_spot == num_insts) {
            // TODO: unwind frame and/or invoke cleanup
        }

        if (this->current_frame->inst_spot > num_insts) [[unlikely]] {
            throw std::logic_error("got beyond instructions in frame");
        }

        int64_t inst = frame_insts->components()[this->current_frame->inst_spot].value<int64_t>();
        switch (inst) {
            case BytecodeOp::LOAD_REG: {
                push(this->current_frame->regs()[arg().value<int64_t>()]);
                shift_inst();
                shift_arg();
                break;
            }
            case BytecodeOp::STORE_REG: {
                this->current_frame->regs()[arg().value<int64_t>()] = pop();
                shift_inst();
                shift_arg();
                break;
            }
            case BytecodeOp::LOAD_REF: {
                push(this->current_frame->regs()[arg().value<int64_t>()]
                         .value<Object*>()
                         ->object<Ref*>()
                         ->v_ref);
                shift_inst();
                shift_arg();
                break;
            }
            case BytecodeOp::STORE_REF: {
                this->current_frame->regs()[arg().value<int64_t>()]
                    .value<Object*>()
                    ->object<Ref*>()
                    ->v_ref = pop();
                shift_inst();
                shift_arg();
                break;
            }
            case BytecodeOp::LOAD_VALUE: {
                push(arg());
                shift_inst();
                shift_arg();
                break;
            }
            case BytecodeOp::LOAD_MODULE: {
                push(module_lookup(this->current_frame->v_module,
                                   arg().value<Object*>()->object<String*>()));
                shift_inst();
                shift_arg();
                break;
            }
            case BytecodeOp::STORE_MODULE: {
                Value& slot = module_lookup(this->current_frame->v_module,
                                            arg().value<Object*>()->object<String*>());
                slot = pop();
                shift_inst();
                shift_arg();
                break;
            }
            case BytecodeOp::INVOKE: {
                Value method = module_lookup(this->current_frame->v_module,
                                             arg(+0).value<Object*>()->object<String*>());
                int64_t num_args = arg(+1).value<int64_t>();
                this->current_frame->data_depth -= num_args;

                shift_inst();
                shift_arg(2);

                this->invoke(method,
                             num_args,
                             this->current_frame->data() + this->current_frame->data_depth);
                break;
            }
            case BytecodeOp::MAKE_TUPLE: {
                auto num_components = arg().value<int64_t>();
                auto tuple = this->gc.alloc<Tuple>(num_components);
                this->current_frame->data_depth -= num_components;
                for (int i = 0; i < num_components; i++) {
                    Value* component =
                        &this->current_frame->data()[this->current_frame->data_depth + i];
                    tuple->components()[i] = *component;
                    *component = Value::null();
                }
                push(Value::object(tuple));
                shift_inst();
                shift_arg();
                break;
            }
            case BytecodeOp::MAKE_VECTOR: {
                auto num_components = arg().value<int64_t>();
                auto vec = this->gc.alloc<Vector>(num_components);
                vec->v_capacity = Value::fixnum(num_components);
                vec->v_length = Value::fixnum(num_components);
                this->current_frame->data_depth -= num_components;
                for (int i = 0; i < num_components; i++) {
                    Value* component =
                        &this->current_frame->data()[this->current_frame->data_depth + i];
                    vec->components()[i] = *component;
                    *component = Value::null();
                }
                push(Value::object(vec));
                shift_inst();
                shift_arg();
                break;
            }
            case BytecodeOp::MAKE_CLOSURE: {
                int64_t num_upregs;
                {
                    Code* code = arg().value<Object*>()->object<Code*>();
                    num_upregs = code->v_upreg_map.value<Object*>()
                                     ->object<Vector*>()
                                     ->v_length.value<int64_t>();
                }

                Vector* _upregs = this->gc.alloc<Vector>(num_upregs);
                _upregs->v_capacity = Value::fixnum(num_upregs);
                _upregs->v_length = Value::fixnum(num_upregs);
                Root upregs(this->gc, Value::object(_upregs));

                Closure* closure = this->gc.alloc<Closure>();
                // Don't need to add the closure as a root; we're done with allocation.
                // Also pull out _upregs again for convenience; it could have moved during closure
                // allocation.
                _upregs = upregs.get().value<Object*>()->object<Vector*>();

                closure->v_code = arg();
                closure->v_upregs = upregs.get();

                // Copy from the current stack frame registers into the closure's upregs.
                Vector* upreg_map = arg()
                                        .value<Object*>()
                                        ->object<Code*>()
                                        ->v_upreg_map.value<Object*>()
                                        ->object<Vector*>();
                for (int64_t i = 0; i < upreg_map->v_length.value<int64_t>(); i++) {
                    int64_t src = upreg_map->components()[i].value<int64_t>();
                    _upregs->components()[i] = this->current_frame->regs()[src];
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

    Value& VM::module_lookup(Value v_module, String* name)
    {
        int64_t name_length = name->v_length.value<int64_t>();
        // TODO: check against size_t?

        while (v_module.tag() != Tag::_NULL) {
            Module* module = v_module.value<Object*>()->object<Module*>();
            int64_t num_entries = module->v_length.value<int64_t>();
            for (int64_t i = 0; i < num_entries; i++) {
                Module::Entry& entry = module->entries()[i];
                String* entry_name = entry.key.value<Object*>()->object<String*>();
                if (entry_name->v_length.value<int64_t>() != name_length) {
                    continue;
                }
                if (memcmp(entry_name->contents(), name->contents(), name_length) != 0) {
                    continue;
                }
                // Match!
                return entry.value;
            }
            v_module = module->v_base;
        }

        throw std::logic_error("didn't find invocation name in module");
    }

    void VM::invoke(Value v_callable, int64_t num_args, Value* args)
    {
        if (v_callable.tag() != Tag::OBJECT ||
            v_callable.value<Object*>()->tag() != ObjectTag::MULTIMETHOD) {
            // TODO: make this a katsu runtime error instead
            throw std::runtime_error("can only invoke a multimethod");
        }
        MultiMethod* multimethod = v_callable.value<Object*>()->object<MultiMethod*>();

        // TODO: actually do a proper multimethod dispatch
        Vector* methods = multimethod->v_methods.value<Object*>()->object<Vector*>();
        if (methods->v_length.value<int64_t>() == 0) {
            throw std::runtime_error("need a method in multimethod");
        }
        Value v_method = methods->components()[0];
        Method* method = v_method.value<Object*>()->object<Method*>();

        if (method->v_code.tag() == Tag::_NULL) {
            // Native handler.
            if (!method->native_handler) {
                throw std::logic_error("method must have v_code or a native_handler");
            }
            Value result = method->native_handler(*this, num_args, args);
            auto push = [this](Value value) -> void {
                this->current_frame->data()[this->current_frame->data_depth++] = value;
            };
            push(result);
        } else {
            // Bytecode body.
            Code* code = method->v_code.value<Object*>()->object<Code*>();
            if (code->v_upreg_map.tag() != Tag::_NULL) {
                throw std::logic_error("method's v_code's v_upreg_map should be null");
            }

            int64_t code_num_regs = code->v_num_regs.value<int64_t>();
            int64_t code_num_data = code->v_num_data.value<int64_t>();

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
            frame->return_id = 0; // TODO
            frame->v_module = code->v_module;
            this->current_frame = frame;
        }
    }
};

#include "builtin.h"

#include "assert.h"
#include "value_utils.h"
#include "vm.h"

#include <algorithm>
#include <iostream>

namespace Katsu
{
    // TODO: param matchers / return type
    void add_native(GC& gc, Root<Module>& r_module, const std::string& name,
                    NativeHandler native_handler)
    {
        // TODO: check if name exists already in module!

        Root<String> r_name(gc, make_string(gc, name));

        Root<Vector> r_methods(gc, make_vector(gc, 1));
        {
            ValueRoot r_param_matchers(gc, Value::null()); // TODO
            OptionalRoot<Type> r_return_type(gc, nullptr); // TODO
            OptionalRoot<Code> r_code(gc, nullptr);        // native!
            Root<Vector> r_attributes(gc, make_vector(gc, 0));

            ValueRoot r_method(gc,
                               Value::object(make_method(gc,
                                                         r_param_matchers,
                                                         r_return_type,
                                                         r_code,
                                                         r_attributes,
                                                         native_handler,
                                                         /* intrinsic_handler */ nullptr)));
            append(gc, r_methods, r_method);
        }
        Root<Vector> r_attributes(gc, make_vector(gc, 0));

        ValueRoot r_multimethod(
            gc,
            Value::object(make_multimethod(gc, r_name, r_methods, r_attributes)));
        append(gc, r_module, r_name, r_multimethod);
    }

    // TODO: param matchers / return type
    void add_intrinsic(GC& gc, Root<Module>& r_module, const std::string& name,
                       IntrinsicHandler intrinsic_handler)
    {
        // TODO: check if name exists already in module!

        Root<String> r_name(gc, make_string(gc, name));

        Root<Vector> r_methods(gc, make_vector(gc, 1));
        {
            ValueRoot r_param_matchers(gc, Value::null()); // TODO
            OptionalRoot<Type> r_return_type(gc, nullptr); // TODO
            OptionalRoot<Code> r_code(gc, nullptr);        // native!
            Root<Vector> r_attributes(gc, make_vector(gc, 0));

            ValueRoot r_method(
                gc,
                Value::object(make_method(gc,
                                          r_param_matchers,
                                          r_return_type,
                                          r_code,
                                          r_attributes,
                                          /* native_handler */ nullptr,
                                          /* intrinsic_handler */ intrinsic_handler)));
            append(gc, r_methods, r_method);
        }
        Root<Vector> r_attributes(gc, make_vector(gc, 0));

        ValueRoot r_multimethod(
            gc,
            Value::object(make_multimethod(gc, r_name, r_methods, r_attributes)));
        append(gc, r_module, r_name, r_multimethod);
    }

    Value native__tilde_(VM& vm, int64_t nargs, Value* args)
    {
        // a ~ b
        ASSERT(nargs == 2);
        Root<String> r_a(vm.gc, args[0].obj_string());
        Root<String> r_b(vm.gc, args[1].obj_string());
        uint64_t length_a = r_a->length;
        uint64_t length_b = r_b->length;
        String* c = make_string_nofill(vm.gc, length_a + length_b);
        memcpy(c->contents(), r_a->contents(), length_a);
        memcpy(c->contents() + length_a, r_b->contents(), length_b);
        return Value::object(c);
    }

    Value native__plus_(VM& vm, int64_t nargs, Value* args)
    {
        // a + b
        ASSERT(nargs == 2);
        return Value::fixnum(args[0].fixnum() + args[1].fixnum());
    }

    Value native__print_(VM& vm, int64_t nargs, Value* args)
    {
        // _ print: val
        ASSERT(nargs == 2);
        String* s = args[1].obj_string();
        std::cout.write(reinterpret_cast<char*>(s->contents()), s->length) << "\n";
        return Value::null();
    }

    Value native__pretty_print_(VM& vm, int64_t nargs, Value* args)
    {
        // _ pretty-print: val
        ASSERT(nargs == 2);
        pprint(args[1]);
        return Value::null();
    }

    void call_impl(OpenVM& vm, bool tail_call, Value v_callable, int64_t nargs, Value* args)
    {
        // TODO: tail-calls
        ASSERT_MSG(!tail_call, "tail calls not implemented");
        Frame* frame = vm.frame();
        if (v_callable.is_obj_closure()) {
            Closure* closure = v_callable.obj_closure();
            ASSERT(closure->v_code.is_obj_code());
            ASSERT(closure->v_upregs.is_obj_array());
            Code* code = closure->v_code.obj_code();
            ASSERT(code->v_upreg_map.is_obj_array());
            Array* upregs = closure->v_upregs.obj_array();
            Array* upreg_map = code->v_upreg_map.obj_array();
            ASSERT(upregs->length == upreg_map->length);

            if ((nargs == 0 && code->num_params != 1) || (nargs > 0 && code->num_params != nargs)) {
                // TODO: Katsu error instead, should be able to handle it
                throw std::runtime_error("called a closure with wrong number of arguments");
            }

            Frame* next = vm.alloc_frame(code->num_regs,
                                         code->num_data,
                                         Value::object(code),
                                         /* v_cleanup */ Value::null(),
                                         /* is_cleanup */ false,
                                         code->v_module);

            // In the closure's frame:
            // - local 0...n are the call arguments (which may just be <null>, in the special case
            // of 0 args and 1 param)
            // - upreg_map points where to load upregs
            // - all other regs null-initialized
            ASSERT(next->num_regs > 0);
            // Copy arguments:
            if (nargs == 0) {
                next->regs()[0] = Value::null();
            }
            for (uint32_t i = 0; i < nargs; i++) {
                next->regs()[i] = args[i];
            }
            // Null-initialize the rest (since we don't know which are upregs):
            for (uint32_t i = nargs; i < next->num_regs; i++) {
                next->regs()[i] = Value::null();
            }
            // Finally, load upregs:
            for (uint64_t i = 0; i < upreg_map->length; i++) {
                Value upreg = upregs->components()[i];
                int64_t dst = upreg_map->components()[i].fixnum();
                ASSERT(dst >= 0 && dst < next->num_regs);
                next->regs()[dst] = upreg;
            }

            frame->inst_spot++;
            frame->arg_spot += 2;
            vm.set_frame(next);
        } else {
            // Just push the callable; it returns itself.
            // TODO: what if multimethod or method? should actually be callable
            frame->inst_spot++;
            frame->arg_spot += 2;
            frame->data()[frame->data_depth++] = v_callable;
        }
    }

    void intrinsic__then_else_(OpenVM& vm, bool tail_call, int64_t nargs, Value* args)
    {
        // cond then: tbody else: fbody
        ASSERT(nargs == 3);
        Value body = (args[0].is_bool() && args[0]._bool()) ? args[1] : args[2];
        call_impl(vm, tail_call, /* v_callable */ body, /* nargs */ 0, /* args */ nullptr);
    }

    void intrinsic__call(OpenVM& vm, bool tail_call, int64_t nargs, Value* args)
    {
        // value call
        ASSERT(nargs == 1);
        call_impl(vm, tail_call, /* v_callable */ args[0], /* nargs */ 0, /* args */ nullptr);
    }

    void intrinsic__call_(OpenVM& vm, bool tail_call, int64_t nargs, Value* args)
    {
        // value call: arg
        ASSERT(nargs == 2);
        call_impl(vm, tail_call, /* v_callable */ args[0], /* nargs */ 1, /* args */ &args[1]);
    }

    void intrinsic__call_star_(OpenVM& vm, bool tail_call, int64_t nargs, Value* args)
    {
        // value call*: args
        ASSERT(nargs == 2);
        Value v_callable = args[0];
        Value v_args = args[1];
        if (!v_args.is_obj_tuple()) {
            throw std::runtime_error("call*: arguments must be a tuple");
        }
        Tuple* args_tuple = v_args.obj_tuple();
        if (args_tuple->length == 0) {
            throw std::runtime_error("call*: arguments must be non-empty");
        }
        call_impl(vm,
                  tail_call,
                  v_callable,
                  /* nargs */ args_tuple->length,
                  /* args */ args_tuple->components());
    }

    Value make_base_type(GC& gc, Root<String>& r_name)
    {
        Root<Vector> r_bases(gc, make_vector(gc, 0));
        OptionalRoot<Vector> r_slots(gc, nullptr);
        return Value::object(make_type(gc,
                                       r_name,
                                       r_bases,
                                       /* sealed */ true,
                                       Type::Kind::PRIMITIVE,
                                       r_slots));
    }

    void _register(VM& vm, BuiltinId id, Root<String>& r_name, Root<Module>& r_module,
                   ValueRoot& r_value)
    {
        vm.register_builtin(id, *r_value);
        append(vm.gc, r_module, r_name, r_value);
    }
    void _register(VM& vm, BuiltinId id, const std::string& name, Root<Module>& r_module,
                   Value value)
    {
        ValueRoot r_value(vm.gc, std::move(value));
        Root<String> r_name(vm.gc, make_string(vm.gc, name));
        _register(vm, id, r_name, r_module, r_value);
    }

    void register_builtins(VM& vm, Root<Module>& r_module)
    {
        _register(vm, BuiltinId::_null, "null", r_module, Value::null());
        _register(vm, BuiltinId::_true, "t", r_module, Value::_bool(true));
        _register(vm, BuiltinId::_false, "f", r_module, Value::_bool(false));

        auto register_base_type = [&vm, &r_module](BuiltinId id, const std::string& name) {
            Root<String> r_name(vm.gc, make_string(vm.gc, name));
            ValueRoot r_type(vm.gc, make_base_type(vm.gc, r_name));
            _register(vm, id, r_name, r_module, r_type);
        };

        // TODO: Number?
        register_base_type(BuiltinId::_Fixnum, "Fixnum");
        register_base_type(BuiltinId::_Float, "Float");
        register_base_type(BuiltinId::_Bool, "Bool");
        register_base_type(BuiltinId::_Null, "Null");
        register_base_type(BuiltinId::_Ref, "Ref");
        register_base_type(BuiltinId::_Tuple, "Tuple");
        register_base_type(BuiltinId::_Array, "Array");
        register_base_type(BuiltinId::_Vector, "Vector");
        register_base_type(BuiltinId::_Module, "Module");
        register_base_type(BuiltinId::_String, "String");
        register_base_type(BuiltinId::_Code, "Code");
        register_base_type(BuiltinId::_Closure, "Closure");
        register_base_type(BuiltinId::_Method, "Method");
        register_base_type(BuiltinId::_MultiMethod, "MultiMethod");

        add_native(vm.gc, r_module, "~:", &native__tilde_);
        add_native(vm.gc, r_module, "+:", &native__plus_);
        add_native(vm.gc, r_module, "print:", &native__print_);
        add_native(vm.gc, r_module, "pretty-print:", &native__pretty_print_);
        add_intrinsic(vm.gc, r_module, "then:else:", &intrinsic__then_else_);
        add_intrinsic(vm.gc, r_module, "call", &intrinsic__call);
        add_intrinsic(vm.gc, r_module, "call:", &intrinsic__call_);
        add_intrinsic(vm.gc, r_module, "call*:", &intrinsic__call_star_);
        // add_native(vm.gc, r_module, "type", &type);

        /*
         * TODO:
         * - cleanup:
         * - panic!:
         * - method:does:
         * - method:does:::
         * - defer-method: (?)
         * - type
         * - is-instance?:
         * - Fixnum? (etc. for other builtin types)
         * - data:has:
         * - data:extends:has:
         * - mixin:
         * - mix-in:to:
         * - let:
         * - mut:
         * - ~:
         * - and:
         * - or:
         * - ==:
         * - !=:
         * - <:
         * - <=:
         * - >:
         * - >=:
         * - +:
         * - -:
         * - *:
         * - /:
         * - not
         * - +
         * - -
         * - print
         * - pr
         * - print:
         * - >string
         * - at:
         * - at:=:
         * - append:
         * - pop
         * - length (String / Vector)
         * - anything for FFI!
         * - anything for delimited continuations
         */
    }
};

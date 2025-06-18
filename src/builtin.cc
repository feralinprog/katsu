#include "builtin.h"

#include "assert.h"
#include "value_utils.h"
#include "vm.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace Katsu
{
    // TODO: return type
    void _add_handler(GC& gc, Root<Module>& r_module, const std::string& name, uint32_t num_params,
                      Root<Array>& r_param_matchers, NativeHandler native_handler,
                      IntrinsicHandler intrinsic_handler)
    {
        // TODO: check if name exists already in module!
        Root<String> r_name(gc, make_string(gc, name));

        Value* v_existing = module_lookup(*r_module, *r_name);
        MultiMethod* multi;
        if (v_existing) {
            if (!v_existing->is_obj_multimethod()) {
                throw std::runtime_error("name exists in module but is not a multimethod!");
            }
            multi = v_existing->obj_multimethod();
            ASSERT(multi->num_params == num_params);
        } else {
            Root<Vector> r_methods(gc, make_vector(gc, 1));
            Root<Vector> r_attributes(gc, make_vector(gc, 0));
            multi = make_multimethod(gc, r_name, num_params, r_methods, r_attributes);
            ValueRoot r_multimethod(gc, Value::object(multi));
            append(gc, r_module, r_name, r_multimethod);
            multi = r_multimethod->obj_multimethod();
        }

        Root<MultiMethod> r_multi(gc, std::move(multi));

        OptionalRoot<Type> r_return_type(gc, nullptr); // TODO
        OptionalRoot<Code> r_code(gc, nullptr);        // native / intrinsic!
        Root<Vector> r_attributes(gc, make_vector(gc, 0));

        Root<Method> r_method(gc,
                              make_method(gc,
                                          r_param_matchers,
                                          r_return_type,
                                          r_code,
                                          r_attributes,
                                          native_handler,
                                          intrinsic_handler));

        add_method(gc, r_multi, r_method, /* require_unique */ true);
    }

    void add_native(GC& gc, Root<Module>& r_module, const std::string& name, uint32_t num_params,
                    Root<Array>& r_param_matchers, NativeHandler native_handler)
    {
        _add_handler(gc,
                     r_module,
                     name,
                     num_params,
                     r_param_matchers,
                     native_handler,
                     /* intrinsic_handler */ nullptr);
    }

    void add_intrinsic(GC& gc, Root<Module>& r_module, const std::string& name, uint32_t num_params,
                       Root<Array>& r_param_matchers, IntrinsicHandler intrinsic_handler)
    {
        _add_handler(gc,
                     r_module,
                     name,
                     num_params,
                     r_param_matchers,
                     /* native_handler */ nullptr,
                     intrinsic_handler);
    }

    Value native__tilde_(VM& vm, int64_t nargs, Value* args)
    {
        // a ~ b
        ASSERT(nargs == 2);
        Root<String> r_a(vm.gc, args[0].obj_string());
        Root<String> r_b(vm.gc, args[1].obj_string());
        return Value::object(concat(vm.gc, r_a, r_b));
    }

    Value native__add_(VM& vm, int64_t nargs, Value* args)
    {
        // a + b
        ASSERT(nargs == 2);
        return Value::fixnum(args[0].fixnum() + args[1].fixnum());
    }

    Value native__sub_(VM& vm, int64_t nargs, Value* args)
    {
        // a - b
        ASSERT(nargs == 2);
        return Value::fixnum(args[0].fixnum() - args[1].fixnum());
    }

    Value native__plus(VM& vm, int64_t nargs, Value* args)
    {
        // + b
        ASSERT(nargs == 1);
        return args[0];
    }

    Value native__minus(VM& vm, int64_t nargs, Value* args)
    {
        // - b
        ASSERT(nargs == 1);
        return Value::fixnum(-args[0].fixnum());
    }

    Value native__mult_(VM& vm, int64_t nargs, Value* args)
    {
        // a * b
        ASSERT(nargs == 2);
        return Value::fixnum(args[0].fixnum() * args[1].fixnum());
    }

    Value native__div_(VM& vm, int64_t nargs, Value* args)
    {
        // a / b
        ASSERT(nargs == 2);
        return Value::fixnum(args[0].fixnum() / args[1].fixnum());
    }

    Value native__id_eq_(VM& vm, int64_t nargs, Value* args)
    {
        // a id= b
        // a = b
        ASSERT(nargs == 2);
        return Value::_bool(args[0] == args[1]);
    }

    Value native__id_ne_(VM& vm, int64_t nargs, Value* args)
    {
        // a id!= b
        // a != b
        ASSERT(nargs == 2);
        return Value::_bool(args[0] != args[1]);
    }

    Value native__gt_(VM& vm, int64_t nargs, Value* args)
    {
        // a > b
        ASSERT(nargs == 2);
        return Value::_bool(args[0].fixnum() > args[1].fixnum());
    }

    Value native__gte_(VM& vm, int64_t nargs, Value* args)
    {
        // a >= b
        ASSERT(nargs == 2);
        return Value::_bool(args[0].fixnum() >= args[1].fixnum());
    }

    Value native__lt_(VM& vm, int64_t nargs, Value* args)
    {
        // a < b
        ASSERT(nargs == 2);
        return Value::_bool(args[0].fixnum() < args[1].fixnum());
    }

    Value native__lte_(VM& vm, int64_t nargs, Value* args)
    {
        // a <= b
        ASSERT(nargs == 2);
        return Value::_bool(args[0].fixnum() <= args[1].fixnum());
    }

    Value native__and_(VM& vm, int64_t nargs, Value* args)
    {
        // a and b
        ASSERT(nargs == 2);
        bool a = args[0]._bool();
        bool b = args[1]._bool();
        return Value::_bool(a && b);
    }

    Value native__or_(VM& vm, int64_t nargs, Value* args)
    {
        // a or b
        ASSERT(nargs == 2);
        bool a = args[0]._bool();
        bool b = args[1]._bool();
        return Value::_bool(a || b);
    }

    Value native__not(VM& vm, int64_t nargs, Value* args)
    {
        // not a
        ASSERT(nargs == 1);
        return Value::_bool(!args[0]._bool());
    }

    Value native__print_(VM& vm, int64_t nargs, Value* args)
    {
        // _ print: val
        ASSERT(nargs == 2);
        String* s = args[1].obj_string();
        std::cout.write(reinterpret_cast<char*>(s->contents()), s->length) << "\n";
        return Value::null();
    }

    Value native__pr(VM& vm, int64_t nargs, Value* args)
    {
        // val pr
        ASSERT(nargs == 1);
        String* s = args[0].obj_string();
        std::cout.write(reinterpret_cast<char*>(s->contents()), s->length) << "\n";
        return args[0];
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
        // In case of tail-call, we need to temporarily store the args as we unwind the current
        // frame and replace it with a new frame.
        Value args_copy[nargs];
        if (tail_call) {
            Frame* frame = vm.frame();
            frame->inst_spot++;
            for (uint32_t i = 0; i < nargs; i++) {
                args_copy[i] = args[i];
            }
            vm.unwind_frame(/* tail_call */ true);
            args = args_copy;
        }

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

            Frame* next =
                vm.alloc_frame(code->num_regs, code->num_data, Value::object(code), code->v_module);

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

            if (!tail_call) {
                Frame* frame = vm.frame();
                frame->inst_spot++;
            }
            vm.set_frame(next);
        } else {
            // Just push the callable; it returns itself.
            // TODO: what if multimethod or method? should actually be callable
            Frame* frame = vm.frame();
            if (!tail_call) {
                frame->inst_spot++;
            }
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

    Value native__type(VM& vm, int64_t nargs, Value* args)
    {
        // value type
        ASSERT(nargs == 1);
        return type_of(vm, args[0]);
    }

    Value native__subtype_p_(VM& vm, int64_t nargs, Value* args)
    {
        // a subtype?: b
        ASSERT(nargs == 2);
        return Value::_bool(is_subtype(args[0].obj_type(), args[1].obj_type()));
    }

    Value native__instance_p_(VM& vm, int64_t nargs, Value* args)
    {
        // v instance?: t
        ASSERT(nargs == 2);
        return Value::_bool(is_instance(vm, args[0], args[1].obj_type()));
    }

    Value native__make_method_with_return_type_code_attrs_(VM& vm, int64_t nargs, Value* args)
    {
        // param-matchers make-method-with-return-type: type code: code attrs: attrs
        ASSERT(nargs == 4);
        ASSERT(args[1].is_obj_type() || args[1].is_null());
        Root<Array> r_param_matchers(vm.gc, args[0].obj_array());
        OptionalRoot<Type> r_return_type(vm.gc,
                                         args[1].is_obj_type() ? args[1].obj_type() : nullptr);
        OptionalRoot<Code> r_code(vm.gc, args[2].obj_code());
        Root<Vector> r_attributes(vm.gc, args[3].obj_vector());
        return Value::object(make_method(vm.gc,
                                         r_param_matchers,
                                         r_return_type,
                                         r_code,
                                         r_attributes,
                                         /* native_handler */ nullptr,
                                         /* intrinsic_handler */ nullptr));
    }

    Value native__add_method_to_require_unique_(VM& vm, int64_t nargs, Value* args)
    {
        // method add-method-to: multimethod require-unique: unique
        ASSERT(nargs == 3);
        Root<Method> r_method(vm.gc, args[0].obj_method());
        Root<MultiMethod> r_multimethod(vm.gc, args[1].obj_multimethod());
        bool require_unique = args[2]._bool();
        add_method(vm.gc, r_multimethod, r_method, require_unique);
        return Value::null();
    }

    Value native__TEST_ASSERT_(VM& vm, int64_t nargs, Value* args)
    {
        // _ TEST-ASSERT: bool
        ASSERT(nargs == 2);
        ASSERT_MSG(args[1]._bool(), "TEST-ASSERT: failed assertion");
        return Value::null();
    }

    Value make_base_type(GC& gc, Root<String>& r_name)
    {
        Root<Array> r_bases(gc, make_array(gc, 0));
        OptionalRoot<Array> r_slots(gc, nullptr);
        return Value::object(make_type(gc,
                                       r_name,
                                       r_bases,
                                       /* sealed */ true,
                                       Type::Kind::PRIMITIVE,
                                       r_slots,
                                       /* num_total_slots */ std::nullopt));
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
        register_base_type(BuiltinId::_Type, "Type");

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = vm.builtin(BuiltinId::_String);
            matchers2->components()[1] = vm.builtin(BuiltinId::_String);
            add_native(vm.gc, r_module, "~:", 2, matchers2, &native__tilde_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = vm.builtin(BuiltinId::_Fixnum);
            matchers2->components()[1] = vm.builtin(BuiltinId::_Fixnum);
            add_native(vm.gc, r_module, "+:", 2, matchers2, &native__add_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = vm.builtin(BuiltinId::_Fixnum);
            matchers2->components()[1] = vm.builtin(BuiltinId::_Fixnum);
            add_native(vm.gc, r_module, "-:", 2, matchers2, &native__sub_);
        }

        {
            Root<Array> matchers1(vm.gc, make_array(vm.gc, 1));
            matchers1->components()[0] = vm.builtin(BuiltinId::_Fixnum);
            add_native(vm.gc, r_module, "+", 1, matchers1, &native__plus);
        }

        {
            Root<Array> matchers1(vm.gc, make_array(vm.gc, 1));
            matchers1->components()[0] = vm.builtin(BuiltinId::_Fixnum);
            add_native(vm.gc, r_module, "-", 1, matchers1, &native__minus);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = vm.builtin(BuiltinId::_Fixnum);
            matchers2->components()[1] = vm.builtin(BuiltinId::_Fixnum);
            add_native(vm.gc, r_module, "*:", 2, matchers2, &native__mult_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = vm.builtin(BuiltinId::_Fixnum);
            matchers2->components()[1] = vm.builtin(BuiltinId::_Fixnum);
            add_native(vm.gc, r_module, "/:", 2, matchers2, &native__div_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = Value::null(); // any
            matchers2->components()[1] = Value::null(); // any
            // By default, = and id= are the same.
            add_native(vm.gc, r_module, "id=:", 2, matchers2, &native__id_eq_);
            add_native(vm.gc, r_module, "=:", 2, matchers2, &native__id_eq_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = Value::null(); // any
            matchers2->components()[1] = Value::null(); // any
            add_native(vm.gc, r_module, "id!=:", 2, matchers2, &native__id_ne_);
            add_native(vm.gc, r_module, "!=:", 2, matchers2, &native__id_ne_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = vm.builtin(BuiltinId::_Fixnum);
            matchers2->components()[1] = vm.builtin(BuiltinId::_Fixnum);
            add_native(vm.gc, r_module, ">:", 2, matchers2, &native__gt_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = vm.builtin(BuiltinId::_Fixnum);
            matchers2->components()[1] = vm.builtin(BuiltinId::_Fixnum);
            add_native(vm.gc, r_module, ">=:", 2, matchers2, &native__gte_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = vm.builtin(BuiltinId::_Fixnum);
            matchers2->components()[1] = vm.builtin(BuiltinId::_Fixnum);
            add_native(vm.gc, r_module, "<:", 2, matchers2, &native__lt_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = vm.builtin(BuiltinId::_Fixnum);
            matchers2->components()[1] = vm.builtin(BuiltinId::_Fixnum);
            add_native(vm.gc, r_module, "<=:", 2, matchers2, &native__lte_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = vm.builtin(BuiltinId::_Bool);
            matchers2->components()[1] = vm.builtin(BuiltinId::_Bool);
            add_native(vm.gc, r_module, "and:", 2, matchers2, &native__and_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = vm.builtin(BuiltinId::_Bool);
            matchers2->components()[1] = vm.builtin(BuiltinId::_Bool);
            add_native(vm.gc, r_module, "or:", 2, matchers2, &native__or_);
        }

        {
            Root<Array> matchers1(vm.gc, make_array(vm.gc, 1));
            matchers1->components()[0] = vm.builtin(BuiltinId::_Bool);
            add_native(vm.gc, r_module, "not", 1, matchers1, &native__not);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = Value::null(); // any
            matchers2->components()[1] = vm.builtin(BuiltinId::_String);
            add_native(vm.gc, r_module, "print:", 2, matchers2, &native__print_);
        }

        {
            Root<Array> matchers1(vm.gc, make_array(vm.gc, 1));
            matchers1->components()[0] = vm.builtin(BuiltinId::_String);
            add_native(vm.gc, r_module, "pr", 1, matchers1, &native__pr);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = Value::null(); // any
            matchers2->components()[1] = Value::null(); // any
            add_native(vm.gc, r_module, "pretty-print:", 2, matchers2, &native__pretty_print_);
        }

        {
            Root<Array> matchers3(vm.gc, make_array(vm.gc, 3));
            matchers3->components()[0] = Value::null(); // any
            matchers3->components()[1] = Value::null(); // any
            matchers3->components()[2] = Value::null(); // any
            add_intrinsic(vm.gc, r_module, "then:else:", 3, matchers3, &intrinsic__then_else_);
        }

        {
            Root<Array> matchers1(vm.gc, make_array(vm.gc, 1));
            matchers1->components()[0] = Value::null(); // any
            add_intrinsic(vm.gc, r_module, "call", 1, matchers1, &intrinsic__call);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = Value::null(); // any
            matchers2->components()[1] = Value::null(); // any
            add_intrinsic(vm.gc, r_module, "call:", 2, matchers2, &intrinsic__call_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = Value::null(); // any
            matchers2->components()[1] = vm.builtin(BuiltinId::_Tuple);
            add_intrinsic(vm.gc, r_module, "call*:", 2, matchers2, &intrinsic__call_star_);
        }

        {
            Root<Array> matchers1(vm.gc, make_array(vm.gc, 1));
            matchers1->components()[0] = Value::null(); // any
            add_native(vm.gc, r_module, "type", 1, matchers1, &native__type);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = vm.builtin(BuiltinId::_Type);
            matchers2->components()[1] = vm.builtin(BuiltinId::_Type);
            add_native(vm.gc, r_module, "subtype?:", 2, matchers2, &native__subtype_p_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = Value::null(); // any
            matchers2->components()[1] = vm.builtin(BuiltinId::_Type);
            add_native(vm.gc, r_module, "instance?:", 2, matchers2, &native__instance_p_);
        }

        {
            Root<Array> matchers4(vm.gc, make_array(vm.gc, 4));
            matchers4->components()[0] = vm.builtin(BuiltinId::_Array);
            matchers4->components()[1] = Value::null(); // TODO: Type or Null
            matchers4->components()[2] = vm.builtin(BuiltinId::_Code);
            matchers4->components()[3] = vm.builtin(BuiltinId::_Vector);
            add_native(vm.gc,
                       r_module,
                       "make-method-with-return-type:code:attrs:",
                       4,
                       matchers4,
                       &native__make_method_with_return_type_code_attrs_);
        }

        {
            Root<Array> matchers3(vm.gc, make_array(vm.gc, 3));
            matchers3->components()[0] = vm.builtin(BuiltinId::_Method);
            matchers3->components()[1] = vm.builtin(BuiltinId::_MultiMethod);
            matchers3->components()[2] = vm.builtin(BuiltinId::_Bool);
            add_native(vm.gc,
                       r_module,
                       "add-method-to:require-unique:",
                       3,
                       matchers3,
                       &native__add_method_to_require_unique_);
        }

        {
            Root<Array> matchers2(vm.gc, make_array(vm.gc, 2));
            matchers2->components()[0] = Value::null(); // any
            matchers2->components()[1] = vm.builtin(BuiltinId::_Bool);
            add_native(vm.gc, r_module, "TEST-ASSERT:", 2, matchers2, &native__TEST_ASSERT_);
        }

        /*
         * TODO:
         * - cleanup: (or maybe this is just delimited continuations...)
         * - panic!: (or maybe this is just delimited continuations...)
         * - mix-in:to:
         * - >string
         * - at:
         * - at:=:
         * - append:
         * - pop
         * - length (String / Vector)
         * - anything for FFI!
         * - anything for delimited continuations
         *
         * also:
         * - tail calls
         * - source spans in bytecode
         * - module imports/exports (actually start a standard library)
         *
         * also move / add some things to compile-time builtins:
         * - method:does:
         * - method:does:::
         * - let:
         * - mut:
         * - data:has:
         * - data:extends:has:
         * - mixin:
         *
         * also move / add some things to katsu instead:
         * - Fixnum? (etc. for other builtin types)
         *   (v Fixnum? === v instance-of?: Fixnum)
         * - print:
         * - pr
         * - pretty-print:
         */
    }
};

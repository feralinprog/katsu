#include "builtin.h"

#include "assert.h"
#include "compile.h"
#include "condition.h"
#include "parser.h"
#include "value_utils.h"
#include "vm.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace Katsu
{
    // TODO: return type
    void _add_handler(GC& gc, Root<Assoc>& r_module, const std::string& name, uint32_t num_params,
                      Root<Array>& r_param_matchers, NativeHandler native_handler,
                      IntrinsicHandler intrinsic_handler)
    {
        // TODO: check if name exists already in module!
        Root<String> r_name(gc, make_string(gc, name));

        Value* v_existing = assoc_lookup(*r_module, *r_name);
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
            ValueRoot r_key(gc, r_name.value());
            append(gc, r_module, r_key, r_multimethod);
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

    void add_native(GC& gc, Root<Assoc>& r_module, const std::string& name, uint32_t num_params,
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

    void add_intrinsic(GC& gc, Root<Assoc>& r_module, const std::string& name, uint32_t num_params,
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
        int64_t a = args[0].fixnum();
        int64_t b = args[1].fixnum();
        if (b == 0) {
            throw condition_error("divide-by-zero", "cannot divide by integer 0");
        }
        return Value::fixnum(a / b);
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

    Value native__str_eq_(VM& vm, int64_t nargs, Value* args)
    {
        // a = b
        ASSERT(nargs == 2);
        return Value::_bool(string_eq(args[0].obj_string(), args[1].obj_string()));
    }

    Value native__str_ne_(VM& vm, int64_t nargs, Value* args)
    {
        // a != b
        ASSERT(nargs == 2);
        return Value::_bool(!string_eq(args[0].obj_string(), args[1].obj_string()));
    }

    Value native__foreign_eq_(VM& vm, int64_t nargs, Value* args)
    {
        // a = b
        ASSERT(nargs == 2);
        return Value::_bool(args[0].obj_foreign()->value == args[1].obj_foreign()->value);
    }

    Value native__foreign_ne_(VM& vm, int64_t nargs, Value* args)
    {
        // a != b
        ASSERT(nargs == 2);
        return Value::_bool(args[0].obj_foreign()->value != args[1].obj_foreign()->value);
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

    void call_impl(OpenVM& vm, bool tail_call, Value v_callable, int64_t nargs, Value* args,
                   Value v_marker = Value::null())
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
                throw condition_error("argument-count-mismatch",
                                      "called a closure with wrong number of arguments");
            }

            Frame* next = vm.alloc_frame(code->num_regs,
                                         code->num_data,
                                         Value::object(code),
                                         code->v_module,
                                         v_marker);

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
        } else if (v_callable.is_obj_call_segment()) {
            CallSegment* segment = v_callable.obj_call_segment();
            // Place the segment's frames on top of the stack, and push the one argument provided.
            if (nargs != 1) {
                throw condition_error(
                    "argument-count-mismatch",
                    "called a call-segment with wrong number of arguments (should be 1)");
            }
            ASSERT_MSG(!tail_call, "tail-call call segment not implemented");
            Frame* old_top = vm.frame();
            old_top->inst_spot++;
            Frame* past_old_top = old_top->next();
            Frame* past_new_top = vm.alloc_frames(segment->length);
            memcpy(past_old_top, segment->frames(), segment->length);
            // Set up caller-pointers throughout the new stack region.
            Frame* prev = old_top;
            Frame* cur = past_old_top;
            while (cur < past_new_top) {
                cur->caller = prev;
                prev = cur;
                cur = cur->next();
            }
            ASSERT(cur == past_new_top);
            Frame* new_top = prev;
            vm.set_frame(new_top);
            new_top->push(args[0]);
        } else if (v_callable.is_obj_code()) {
            Code* code = v_callable.obj_code();
            if (!code->v_upreg_map.is_null()) {
                throw condition_error(
                    "raw-closure-call",
                    "cannot call a raw Code object which requires upregs (a closure)");
            }

            if (code->num_params != nargs) {
                throw condition_error("argument-count-mismatch",
                                      "called a raw Code object with wrong number of arguments");
            }

            Frame* next = vm.alloc_frame(code->num_regs,
                                         code->num_data,
                                         Value::object(code),
                                         code->v_module,
                                         v_marker);

            // In the closure's frame:
            // - local 0...n are the call arguments (which may just be <null>, in the special case
            // of 0 args and 1 param)
            // - there are no upregs to deal with!
            // - all other regs null-initialized
            ASSERT(next->num_regs > 0);
            // Copy arguments:
            if (nargs == 0) {
                next->regs()[0] = Value::null();
            }
            for (uint32_t i = 0; i < nargs; i++) {
                next->regs()[i] = args[i];
            }
            // Null-initialize the rest:
            for (uint32_t i = nargs; i < next->num_regs; i++) {
                next->regs()[i] = Value::null();
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
            frame->push(v_callable);
        }
    }

    void intrinsic__then_else_(OpenVM& vm, bool tail_call, int64_t nargs, Value* args)
    {
        // cond then: tbody else: fbody
        ASSERT(nargs == 3);
        Value body = (args[0].is_bool() && args[0]._bool()) ? args[1] : args[2];
        call_impl(vm,
                  tail_call,
                  /* v_callable */ body,
                  /* nargs */ 0,
                  /* args */ nullptr);
    }

    void intrinsic__call(OpenVM& vm, bool tail_call, int64_t nargs, Value* args)
    {
        // value call
        ASSERT(nargs == 1);
        call_impl(vm,
                  tail_call,
                  /* v_callable */ args[0],
                  /* nargs */ 0,
                  /* args */ nullptr);
    }

    void intrinsic__call_(OpenVM& vm, bool tail_call, int64_t nargs, Value* args)
    {
        // value call: arg
        ASSERT(nargs == 2);
        call_impl(vm,
                  tail_call,
                  /* v_callable */ args[0],
                  /* nargs */ 1,
                  /* args */ &args[1]);
    }

    void intrinsic__call_star_(OpenVM& vm, bool tail_call, int64_t nargs, Value* args)
    {
        // value call*: args
        ASSERT(nargs == 2);
        Value v_callable = args[0];
        Value v_args = args[1];
        Tuple* args_tuple = v_args.obj_tuple();
        if (args_tuple->length == 0) {
            throw condition_error("invalid-argument", "arguments must be non-empty");
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

    template <typename T> inline T unsafe_read_at_offset(Object* object, int64_t offset)
    {
        return *(T*)((uint8_t*)object + offset);
    }

    template <typename T>
    inline void unsafe_write_at_offset(Object* object, int64_t offset, T value)
    {
        *(T*)((uint8_t*)object + offset) = value;
    }

    Value native__unsafe_read_u8_at_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // obj read-u8-at-offset: offset
        ASSERT(nargs == 2);
        ASSERT(args[0].is_object());
        return Value::fixnum(unsafe_read_at_offset<uint8_t>(args[0].object(), args[1].fixnum()));
    }
    Value native__unsafe_write_u8_at_offset_value_(VM& vm, int64_t nargs, Value* args)
    {
        // obj write-u8-at-offset: offset value: value
        ASSERT(nargs == 3);
        ASSERT(args[0].is_object());
        // TODO: check range
        unsafe_write_at_offset<uint8_t>(args[0].object(),
                                        args[1].fixnum(),
                                        (uint8_t)args[2].fixnum());
        return Value::null();
    }

    Value native__unsafe_read_u32_at_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // obj read-u32-at-offset: offset
        ASSERT(nargs == 2);
        ASSERT(args[0].is_object());
        return Value::fixnum(unsafe_read_at_offset<uint32_t>(args[0].object(), args[1].fixnum()));
    }
    Value native__unsafe_write_u32_at_offset_value_(VM& vm, int64_t nargs, Value* args)
    {
        // obj write-u32-at-offset: offset value: value
        ASSERT(nargs == 3);
        ASSERT(args[0].is_object());
        // TODO: check range
        unsafe_write_at_offset<uint32_t>(args[0].object(),
                                         args[1].fixnum(),
                                         (uint32_t)args[2].fixnum());
        return Value::null();
    }

    Value native__unsafe_read_u64_at_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // obj read-u64-at-offset: offset
        ASSERT(nargs == 2);
        ASSERT(args[0].is_object());
        uint64_t read = unsafe_read_at_offset<uint64_t>(args[0].object(), args[1].fixnum());
        ASSERT(read <= INT64_MAX);
        return Value::fixnum((int64_t)read);
    }
    Value native__unsafe_write_u64_at_offset_value_(VM& vm, int64_t nargs, Value* args)
    {
        // obj write-u64-at-offset: offset value: u64
        ASSERT(nargs == 3);
        ASSERT(args[0].is_object());
        ASSERT(args[2].fixnum() >= 0);
        uint64_t write = (uint64_t)args[2].fixnum();
        unsafe_write_at_offset(args[0].object(), args[1].fixnum(), write);
        return Value::null();
    }

    Value native__unsafe_read_value_at_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // obj read-value-at-offset: offset
        ASSERT(nargs == 2);
        ASSERT(args[0].is_object());
        return unsafe_read_at_offset<Value>(args[0].object(), args[1].fixnum());
    }

    Value native__unsafe_write_value_at_offset_value_(VM& vm, int64_t nargs, Value* args)
    {
        // obj write-value-at-offset: offset value: value
        ASSERT(nargs == 3);
        ASSERT(args[0].is_object());
        unsafe_write_at_offset<Value>(args[0].object(), args[1].fixnum(), args[2]);
        return Value::null();
    }

    void intrinsic__get_call_stack(OpenVM& vm, bool tail_call, int64_t nargs, Value* args)
    {
        // _ get-call-stack
        ASSERT(nargs == 1);
        vm.frame()->inst_spot++;
        Frame* past_top = vm.frame()->next();
        Frame* bottom = vm.bottom_frame();
        uint64_t total_stack_length =
            reinterpret_cast<uint8_t*>(past_top) - reinterpret_cast<uint8_t*>(bottom);
        vm.frame()->push(
            Value::object(make_call_segment(vm.gc, vm.bottom_frame(), total_stack_length)));
    }

    void intrinsic__call_marked_(OpenVM& vm, bool tail_call, int64_t nargs, Value* args)
    {
        // value call/marked: marker
        ASSERT(nargs == 2);
        Value v_callable = args[0];
        Value v_marker = args[1];
        call_impl(vm, tail_call, v_callable, /* nargs */ 0, /* args */ &v_marker, v_marker);
    }

    void intrinsic__call_dc_(OpenVM& vm, bool tail_call, int64_t nargs, Value* args)
    {
        // TODO: tail-call call/dc:?
        ASSERT_MSG(!tail_call, "call/dc: tail-call not implemented");
        // value call/dc: marker
        ASSERT(nargs == 2);
        ValueRoot r_callable(vm.gc, std::move(args[0]));
        Value v_marker = args[1];
        // Search call stack (from top) for the marker, move that portion of the stack into a
        // CallSegment, and then call the callable value with that CallSegment.
        Frame* marked = vm.frame();
        while (marked && marked->v_marker != v_marker) {
            marked = marked->caller;
        }
        if (!marked) {
            throw condition_error("marker-not-found", "did not find marker in call stack");
        }
        vm.frame()->inst_spot++;
        Frame* past_top = vm.frame()->next();
        uint64_t total_length =
            reinterpret_cast<uint8_t*>(past_top) - reinterpret_cast<uint8_t*>(marked);
        Value v_segment = Value::object(make_call_segment(vm.gc, marked, total_length));
        vm.set_frame(marked->caller);
        // Rewind the new top frame; we are pretending that it is about to call the segment.
        vm.frame()->inst_spot--;
        call_impl(vm,
                  /* tail_call */ false,
                  /* v_callable */ *r_callable,
                  /* nargs */ 1,
                  /* args */ &v_segment,
                  /* v_marker */ Value::null());
    }

    void intrinsic__loaded_modules(OpenVM& vm, bool tail_call, int64_t nargs, Value* args)
    {
        // _ loaded-modules
        ASSERT(nargs == 1);
        vm.frame()->push(Value::object(vm.vm.modules()));
        vm.frame()->inst_spot++;
    }

    Value native__read_file_(VM& vm, int64_t nargs, Value* args)
    {
        // _ read-file: path
        ASSERT(nargs == 2);
        std::string filepath = native_str(args[1].obj_string());

        try {
            std::ifstream file_stream;
            // Raise exceptions on logical error or read/write error.
            file_stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
            file_stream.open(filepath.c_str());

            std::stringstream str_stream;
            str_stream << file_stream.rdbuf();
            std::string file_contents = str_stream.str();

            return Value::object(make_string(vm.gc, file_contents));
        } catch (const std::ios_base::failure& e) {
            throw condition_error("io-error", e.what());
        }
    }

    Value native__make_empty_assoc(VM& vm, int64_t nargs, Value* args)
    {
        // _ make-empty-assoc
        ASSERT(nargs == 1);
        return Value::object(make_assoc(vm.gc, 0));
    }

    Value native__append_(VM& vm, int64_t nargs, Value* args)
    {
        // vector append: value
        ASSERT(nargs == 2);
        Root<Vector> r_vector(vm.gc, args[0].obj_vector());
        Value v_value = args[1];
        ValueRoot r_value(vm.gc, std::move(v_value));
        return Value::object(append(vm.gc, r_vector, r_value));
    }

    Value native__add_value_(VM& vm, int64_t nargs, Value* args)
    {
        // assoc add: key value: value
        ASSERT(nargs == 3);
        Root<Assoc> r_assoc(vm.gc, args[0].obj_assoc());
        Value v_key = args[1];
        ValueRoot r_key(vm.gc, std::move(v_key));
        Value value = args[2];
        ValueRoot r_value(vm.gc, std::move(value));
        return Value::object(append(vm.gc, r_assoc, r_key, r_value));
    }

    struct RunContext
    {
        Lexer lexer;
        TokenStream stream;
        std::unique_ptr<PrattParser> parser;

        RunContext(const SourceFile& source)
            : lexer(source)
            , stream(lexer)
            , parser(make_default_parser())
        {}

        Value to_value(GC& gc)
        {
            return Value::object(make_foreign(gc, reinterpret_cast<void*>(this)));
        }
        static RunContext* from_value(Value v)
        {
            ASSERT(v.is_obj_foreign());
            void* p = v.obj_foreign()->value;
            return reinterpret_cast<RunContext*>(p);
        }
    };
    Value native__make_run_context_for_path_(VM& vm, int64_t nargs, Value* args)
    {
        // _ make-run-context-for-path: path contents: contents
        ASSERT(nargs == 3);
        Root<String> r_path(vm.gc, args[1].obj_string());
        Root<String> r_contents(vm.gc, args[2].obj_string());

        SourceFile source = {
            .path = std::make_shared<std::string>(native_str(args[1].obj_string())),
            .source = std::make_shared<std::string>(native_str(args[2].obj_string()))};
        RunContext* context = new RunContext(source);

        // Skip any leading semicolons / newlines to get to the meat.
        while (context->stream.current_has_type(TokenType::SEMICOLON) ||
               context->stream.current_has_type(TokenType::NEWLINE)) {
            context->stream.consume();
        }

        return context->to_value(vm.gc);
    }
    Value native__parse_and_compile_in_module_imports_(VM& vm, int64_t nargs, Value* args)
    {
        // run-context parse-and-compile-in-module: module imports: import-vec
        ASSERT(nargs == 3);

        RunContext* context = RunContext::from_value(args[0]);
        Root<Assoc> r_module(vm.gc, args[1].obj_assoc());
        Root<Vector> r_imports(vm.gc, args[2].obj_vector());

        if (context->stream.current_has_type(TokenType::END)) {
            return Value::null();
        }

        std::unique_ptr<Expr> top_level_expr =
            context->parser->parse(context->stream, 0 /* precedence */, true /* is_toplevel */);

        std::vector<std::unique_ptr<Expr>> top_level_exprs;
        top_level_exprs.emplace_back(std::move(top_level_expr));
        Code* code =
            compile_into_module(vm, r_module, r_imports, top_level_exprs[0]->span, top_level_exprs);

        // Ratchet past any semicolons and newlines, since the parser explicitly stops
        // when it sees either of these at the top level.
        while (context->stream.current_has_type(TokenType::SEMICOLON) ||
               context->stream.current_has_type(TokenType::NEWLINE)) {
            context->stream.consume();
        }

        return Value::object(code);
    }
    Value native__free(VM& vm, int64_t nargs, Value* args)
    {
        // run-context free
        ASSERT(nargs == 1);
        RunContext* context = RunContext::from_value(args[0]);
        delete context;
        return Value::null();
    }

    void intrinsic__set_condition_handler_from_module(OpenVM& vm, bool tail_call, int64_t nargs,
                                                      Value* args)
    {
        // _ set-condition-handler-from-module
        ASSERT(nargs == 1);
        String* name = make_string(vm.gc, "handle-raw-condition-with-message:");
        Assoc* module = vm.frame()->v_module.obj_assoc();
        Value* handler = assoc_lookup(module, name);
        ASSERT(handler);
        vm.vm.v_condition_handler = *handler;
        vm.frame()->push(Value::null());
        vm.frame()->inst_spot++;
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

    void _register(VM& vm, BuiltinId id, Root<String>& r_name, Root<Assoc>& r_module,
                   ValueRoot& r_value)
    {
        vm.register_builtin(id, *r_value);
        ValueRoot r_key(vm.gc, r_name.value());
        append(vm.gc, r_module, r_key, r_value);
    }
    void _register(VM& vm, BuiltinId id, const std::string& name, Root<Assoc>& r_module,
                   Value value)
    {
        ValueRoot r_value(vm.gc, std::move(value));
        Root<String> r_name(vm.gc, make_string(vm.gc, name));
        _register(vm, id, r_name, r_module, r_value);
    }

    void register_builtins(VM& vm, Root<Assoc>& r_defaults, Root<Assoc>& r_extras)
    {
        const auto register_base_type = [&vm, &r_defaults](BuiltinId id, const std::string& name) {
            Root<String> r_name(vm.gc, make_string(vm.gc, name));
            ValueRoot r_type(vm.gc, make_base_type(vm.gc, r_name));
            _register(vm, id, r_name, r_defaults, r_type);
        };

        const std::function<Value()> matches_any = []() { return Value::null(); };
        const auto matches_type = [&vm](BuiltinId id) -> std::function<Value()> {
            return [&vm, id]() { return vm.builtin(id); };
        };
        const auto register_native = [&vm](const std::string& name,
                                           Root<Assoc>& r_module,
                                           const std::vector<std::function<Value()>>& matchers,
                                           NativeHandler handler) -> void {
            Root<Array> r_matchers(vm.gc, make_array(vm.gc, matchers.size()));
            for (size_t i = 0; i < matchers.size(); i++) {
                r_matchers->components()[i] = matchers[i]();
            }
            add_native(vm.gc, r_module, name, matchers.size(), r_matchers, handler);
        };
        const auto register_intrinsic = [&vm](const std::string& name,
                                              Root<Assoc>& r_module,
                                              const std::vector<std::function<Value()>>& matchers,
                                              IntrinsicHandler handler) -> void {
            Root<Array> r_matchers(vm.gc, make_array(vm.gc, matchers.size()));
            for (size_t i = 0; i < matchers.size(); i++) {
                r_matchers->components()[i] = matchers[i]();
            }
            add_intrinsic(vm.gc, r_module, name, matchers.size(), r_matchers, handler);
        };

        _register(vm, BuiltinId::_null, "null", r_defaults, Value::null());
        _register(vm, BuiltinId::_true, "t", r_defaults, Value::_bool(true));
        _register(vm, BuiltinId::_false, "f", r_defaults, Value::_bool(false));

        // TODO: Number?
        register_base_type(BuiltinId::_Fixnum, "Fixnum");
        register_base_type(BuiltinId::_Float, "Float");
        register_base_type(BuiltinId::_Bool, "Bool");
        register_base_type(BuiltinId::_Null, "Null");
        register_base_type(BuiltinId::_Ref, "Ref");
        register_base_type(BuiltinId::_Tuple, "Tuple");
        register_base_type(BuiltinId::_Array, "Array");
        register_base_type(BuiltinId::_Vector, "Vector");
        register_base_type(BuiltinId::_Assoc, "Assoc");
        register_base_type(BuiltinId::_String, "String");
        register_base_type(BuiltinId::_Code, "Code");
        register_base_type(BuiltinId::_Closure, "Closure");
        register_base_type(BuiltinId::_Method, "Method");
        register_base_type(BuiltinId::_MultiMethod, "MultiMethod");
        register_base_type(BuiltinId::_Type, "Type");
        register_base_type(BuiltinId::_CallSegment, "CallSegment");
        register_base_type(BuiltinId::_Foreign, "Foreign");

        // Use shorthand for builtin IDs just to reduce noise and make it easier to read.
        register_native("~:",
                        r_defaults,
                        {matches_type(_String), matches_type(_String)},
                        &native__tilde_);
        register_native("+:",
                        r_defaults,
                        {matches_type(_Fixnum), matches_type(_Fixnum)},
                        &native__add_);
        register_native("-:",
                        r_defaults,
                        {matches_type(_Fixnum), matches_type(_Fixnum)},
                        &native__sub_);
        register_native("+", r_defaults, {matches_type(_Fixnum)}, &native__plus);
        register_native("-", r_defaults, {matches_type(_Fixnum)}, &native__minus);
        register_native("*:",
                        r_defaults,
                        {matches_type(_Fixnum), matches_type(_Fixnum)},
                        &native__mult_);
        register_native("/:",
                        r_defaults,
                        {matches_type(_Fixnum), matches_type(_Fixnum)},
                        &native__div_);

        // By default, = and id= are the same.
        register_native("id=:", r_defaults, {matches_any, matches_any}, &native__id_eq_);
        register_native("=:", r_defaults, {matches_any, matches_any}, &native__id_eq_);
        // Likewise with != and id!= are the same.
        register_native("id!=:", r_defaults, {matches_any, matches_any}, &native__id_ne_);
        register_native("!=:", r_defaults, {matches_any, matches_any}, &native__id_ne_);

        register_native("=:",
                        r_defaults,
                        {matches_type(_String), matches_type(_String)},
                        &native__str_eq_);
        register_native("!=:",
                        r_defaults,
                        {matches_type(_String), matches_type(_String)},
                        &native__str_ne_);

        register_native("=:",
                        r_defaults,
                        {matches_type(_Foreign), matches_type(_Foreign)},
                        &native__foreign_eq_);
        register_native("!=:",
                        r_defaults,
                        {matches_type(_Foreign), matches_type(_Foreign)},
                        &native__foreign_ne_);

        register_native(">:",
                        r_defaults,
                        {matches_type(_Fixnum), matches_type(_Fixnum)},
                        &native__gt_);
        register_native(">=:",
                        r_defaults,
                        {matches_type(_Fixnum), matches_type(_Fixnum)},
                        &native__gte_);
        register_native("<:",
                        r_defaults,
                        {matches_type(_Fixnum), matches_type(_Fixnum)},
                        &native__lt_);
        register_native("<=:",
                        r_defaults,
                        {matches_type(_Fixnum), matches_type(_Fixnum)},
                        &native__lte_);

        register_native("and:",
                        r_defaults,
                        {matches_type(_Bool), matches_type(_Bool)},
                        &native__and_);
        register_native("or:",
                        r_defaults,
                        {matches_type(_Bool), matches_type(_Bool)},
                        &native__or_);
        register_native("not", r_defaults, {matches_type(_Bool)}, &native__not);

        register_native("print:", r_extras, {matches_any, matches_type(_String)}, &native__print_);
        register_native("pretty-print:",
                        r_extras,
                        {matches_any, matches_any},
                        &native__pretty_print_);

        register_intrinsic("then:else:",
                           r_defaults,
                           {matches_any, matches_any, matches_any},
                           &intrinsic__then_else_);

        register_intrinsic("call", r_defaults, {matches_any}, &intrinsic__call);
        register_intrinsic("call:", r_defaults, {matches_any, matches_any}, &intrinsic__call_);
        register_intrinsic("call*:",
                           r_defaults,
                           {matches_any, matches_type(_Tuple)},
                           &intrinsic__call_star_);

        register_native("type", r_defaults, {matches_any}, &native__type);
        register_native("subtype?:",
                        r_defaults,
                        {matches_type(_Type), matches_type(_Type)},
                        &native__subtype_p_);
        register_native("instance?:",
                        r_defaults,
                        {matches_any, matches_type(_Type)},
                        &native__instance_p_);

        register_native("make-method-with-return-type:code:attrs:",
                        r_defaults, // needed for method definitions
                        {matches_type(_Array),
                         matches_any, // TODO: Type or Null
                         matches_type(_Code),
                         matches_type(_Vector)},
                        &native__make_method_with_return_type_code_attrs_);
        register_native("add-method-to:require-unique:",
                        r_defaults, // needed for method definitions
                        {matches_type(_Method), matches_type(_MultiMethod), matches_type(_Bool)},
                        &native__add_method_to_require_unique_);

        register_native("TEST-ASSERT:",
                        r_extras,
                        {matches_any, matches_type(_Bool)},
                        &native__TEST_ASSERT_);

        register_native("unsafe-read-u8-at-offset:",
                        r_extras,
                        {matches_any, matches_type(_Fixnum)},
                        &native__unsafe_read_u8_at_offset_);
        register_native("unsafe-write-u8-at-offset:value:",
                        r_extras,
                        {matches_any, matches_type(_Fixnum), matches_type(_Fixnum)},
                        &native__unsafe_write_u8_at_offset_value_);
        register_native("unsafe-read-u32-at-offset:",
                        r_extras,
                        {matches_any, matches_type(_Fixnum)},
                        &native__unsafe_read_u32_at_offset_);
        register_native("unsafe-write-u32-at-offset:value:",
                        r_extras,
                        {matches_any, matches_type(_Fixnum), matches_type(_Fixnum)},
                        &native__unsafe_write_u32_at_offset_value_);
        register_native("unsafe-read-u64-at-offset:",
                        r_extras,
                        {matches_any, matches_type(_Fixnum)},
                        &native__unsafe_read_u64_at_offset_);
        register_native("unsafe-write-u64-at-offset:value:",
                        r_extras,
                        {matches_any, matches_type(_Fixnum), matches_type(_Fixnum)},
                        &native__unsafe_write_u64_at_offset_value_);
        register_native("unsafe-read-value-at-offset:",
                        r_extras,
                        {matches_any, matches_type(_Fixnum)},
                        &native__unsafe_read_value_at_offset_);
        register_native("unsafe-write-value-at-offset:value:",
                        r_extras,
                        {matches_any, matches_type(_Fixnum), matches_any},
                        &native__unsafe_write_value_at_offset_value_);

        register_intrinsic("get-call-stack", r_extras, {matches_any}, &intrinsic__get_call_stack);

        register_intrinsic("call/marked:",
                           r_extras,
                           {matches_any, matches_any},
                           &intrinsic__call_marked_);
        register_intrinsic("call/dc:", r_extras, {matches_any, matches_any}, &intrinsic__call_dc_);

        register_intrinsic("loaded-modules", r_extras, {matches_any}, &intrinsic__loaded_modules);

        register_native("read-file:",
                        r_extras,
                        {matches_any, matches_type(_String)},
                        &native__read_file_);

        register_native("make-empty-assoc", r_extras, {matches_any}, &native__make_empty_assoc);

        register_native("append:",
                        r_extras,
                        {matches_type(_Vector), matches_any},
                        &native__append_);
        register_native("add:value:",
                        r_extras,
                        {matches_type(_Assoc), matches_any, matches_any},
                        &native__add_value_);


        // TODO: this is super hacky. figure out a different way to do this.
        register_native("make-run-context-for-path:contents:",
                        r_extras,
                        {matches_any, matches_type(_String), matches_type(_String)},
                        &native__make_run_context_for_path_);
        register_native("parse-and-compile-in-module:imports:",
                        r_extras,
                        {matches_any, matches_type(_Assoc), matches_type(_Vector)},
                        &native__parse_and_compile_in_module_imports_);
        register_native("free", r_extras, {matches_any}, &native__free);

        register_intrinsic("set-condition-handler-from-module",
                           r_extras,
                           {matches_any},
                           &intrinsic__set_condition_handler_from_module);

        /*
         * TODO:
         * - cleanup: (or maybe this is just delimited continuations...)
         * - panic!: (or maybe this is just delimited continuations...)
         * - mix-in:to:
         * - at:
         * - at:=:
         * - append:
         * - pop
         * - length (String / Vector)
         * - anything for FFI!
         *
         * also move / add some things to compile-time builtins:
         * - let:do:
         * - let:do:::
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
         * - pretty-print:
         * - >string
         */
    }
};

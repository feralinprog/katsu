#include "builtin.h"

#include "assert.h"
#include "value_utils.h"

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

    Value plus(VM& vm, int64_t nargs, Value* args)
    {
        ASSERT(nargs == 2);
        return Value::fixnum(args[0].fixnum() + args[1].fixnum());
    }

    Value pprint(VM& vm, int64_t nargs, Value* args)
    {
        ASSERT(nargs == 2);
        pprint(args[1]);
        return Value::null();
    }

    Builtins::Builtins(GC& _gc)
        : gc(_gc)
    {
        for (size_t i = 0; i < BuiltinId::NUM_BUILTINS; i++) {
            this->builtin_values[i] = Value::null();
        }
        this->gc.root_providers.push_back(this);
    }

    Builtins::~Builtins()
    {
        this->gc.root_providers.erase(
            std::find(this->gc.root_providers.begin(), this->gc.root_providers.end(), this));
    }

    Value make_base_type(GC& gc, Root<String>& r_name)
    {
        Root<Vector> r_bases(gc, make_vector(gc, 0));
        Root<Vector> r_linearization(gc, make_vector(gc, 1));
        Root<Vector> r_subtypes(gc, make_vector(gc, 0));
        OptionalRoot<Vector> r_slots(gc, nullptr);
        Root<Type> r_type(gc,
                          make_type(gc,
                                    r_name,
                                    r_bases,
                                    /* sealed */ true,
                                    r_linearization,
                                    r_subtypes,
                                    Type::Kind::PRIMITIVE,
                                    r_slots));
        ValueRoot rv_type(gc, r_type.value());
        // TODO: automatically compute C3 linearization (in make_type()?)
        append(gc, r_linearization, rv_type);
        return r_type.value();
    }

    void Builtins::_register(BuiltinId id, Root<String>& r_name, Root<Module>& r_module,
                             ValueRoot& r_value)
    {
        ASSERT(this->builtin_values[id] == Value::null());
        this->builtin_values[id] = *r_value;
        append(this->gc, r_module, r_name, r_value);
    }
    void Builtins::_register(BuiltinId id, const std::string& name, Root<Module>& r_module,
                             Value value)
    {
        ValueRoot r_value(this->gc, std::move(value));
        Root<String> r_name(this->gc, make_string(gc, name));
        this->_register(id, r_name, r_module, r_value);
    }

    void Builtins::register_builtins(Root<Module>& r_module)
    {
        this->_register(BuiltinId::_null, "null", r_module, Value::null());
        this->_register(BuiltinId::_true, "t", r_module, Value::_bool(true));
        this->_register(BuiltinId::_false, "f", r_module, Value::_bool(false));

        auto register_base_type = [this, &r_module](BuiltinId id, const std::string& name) {
            Root<String> r_name(this->gc, make_string(this->gc, name));
            ValueRoot r_type(this->gc, make_base_type(this->gc, r_name));
            this->_register(id, r_name, r_module, r_type);
        };

        register_base_type(BuiltinId::_Fixnum, "Fixnum");
        register_base_type(BuiltinId::_Float, "Float");
        register_base_type(BuiltinId::_Bool, "Bool");
        register_base_type(BuiltinId::_Null, "Null");
        register_base_type(BuiltinId::_Tuple, "Tuple");
        register_base_type(BuiltinId::_Array, "Array");
        register_base_type(BuiltinId::_Vector, "Vector");
        register_base_type(BuiltinId::_Module, "Module");
        register_base_type(BuiltinId::_String, "String");
        register_base_type(BuiltinId::_Code, "Code");
        register_base_type(BuiltinId::_Closure, "Closure");
        register_base_type(BuiltinId::_Method, "Method");
        register_base_type(BuiltinId::_MultiMethod, "MultiMethod");

        // TODO: Number?
        // TODO: DataclassType?

        add_native(this->gc, r_module, "+:", &plus);
        add_native(this->gc, r_module, "pretty-print:", &pprint);

        /*
         * TODO:
         * - if:then:else:
         * - call
         * - call:
         * - call*:
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

    void Builtins::visit_roots(std::function<void(Value*)>& visitor)
    {
        for (Value& builtin : this->builtin_values) {
            visitor(&builtin);
        }
    }
};

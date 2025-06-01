#include "builtin.h"

#include "assert.h"
#include "value_utils.h"

namespace Katsu
{
    // TODO: param matchers / return type
    void add_native(GC& gc, Root<Module>& r_module, const std::string& name, NativeHandler handler)
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
                                                         handler)));
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

    Value print(VM& vm, int64_t nargs, Value* args)
    {
        ASSERT(nargs == 2);
        pprint(args[1]);
        return Value::null();
    }

    void add_builtins(GC& gc, Root<Module>& r_module)
    {
        add_native(gc, r_module, "+:", &plus);
        add_native(gc, r_module, "print:", &print);
    }
};

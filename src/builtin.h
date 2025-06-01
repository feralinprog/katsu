#pragma once

#include "gc.h"
#include "value.h"

namespace Katsu
{
    class Builtins : public RootProvider
    {
    public:
        // Registers this class with the GC as a RootProvider for
        // various other objects that require more direct access than only through a module lookup.
        Builtins(GC& _gc);
        ~Builtins();

        // Add builtins to the module.
        void register_builtins(Root<Module>& r_module);

        // See RootProvider.
        void visit_roots(std::function<void(Value*)>& visitor) override;

    private:
        enum BuiltinId
        {
            _null,
            _true,
            _false,

            _Fixnum,
            _Float,
            _Bool,
            _Null,
            _Tuple,
            _Array,
            _Vector,
            _Module,
            _String,
            _Code,
            _Closure,
            _Method,
            _MultiMethod,
            _Type,

            // Keep this last!
            NUM_BUILTINS,
        };

        void _register(BuiltinId id, Root<String>& r_name, Root<Module>& r_module,
                       ValueRoot& r_value);
        void _register(BuiltinId id, const std::string& name, Root<Module>& r_module, Value value);

        GC& gc;

        // Builtin values that we need convenient access to (and which are GC'ed).
        // Indexed by BuiltinId.
        Value builtin_values[BuiltinId::NUM_BUILTINS];
    };
};

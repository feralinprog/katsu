#pragma once

#include "gc.h"
#include "value.h"
#include "vm.h"

namespace Katsu
{
    // Add builtins to the VM's builtin array and to various core.builtin.* modules.
    void register_builtins(VM& vm, Root<Assoc>& r_modules);

    // Helper functions:
    void add_native(GC& gc, Value& v_multimethods, bool global, Root<Assoc>& r_module,
                    const std::string& name, uint32_t num_params, Root<Array>& r_param_matchers,
                    NativeHandler native_handler);
    void add_intrinsic(GC& gc, Value& v_multimethods, bool global, Root<Assoc>& r_module,
                       const std::string& name, uint32_t num_params, Root<Array>& r_param_matchers,
                       IntrinsicHandler intrinsic_handler);
};

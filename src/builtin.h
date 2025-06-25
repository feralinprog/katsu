#pragma once

#include "gc.h"
#include "value.h"
#include "vm.h"

namespace Katsu
{
    // Add builtins to the VM's builtin array and to the module.
    void register_builtins(VM& vm, Root<Assoc>& r_module);

    // Helper functions:
    void add_native(GC& gc, Root<Assoc>& r_module, const std::string& name, uint32_t num_params,
                    Root<Array>& r_param_matchers, NativeHandler native_handler);
    void add_intrinsic(GC& gc, Root<Assoc>& r_module, const std::string& name, uint32_t num_params,
                       Root<Array>& r_param_matchers, IntrinsicHandler intrinsic_handler);
};

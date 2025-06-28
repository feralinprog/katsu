#pragma once

#include "gc.h"
#include "value.h"
#include "vm.h"

namespace Katsu
{
    // Add builtins to the VM's builtin array and to the given defaults / extras modules.
    // (Defaults are automatically imported by every module; extras are opt-in.)
    void register_builtins(VM& vm, Root<Assoc>& r_defaults, Root<Assoc>& r_extras);

    // Helper functions:
    void add_native(GC& gc, Root<Assoc>& r_module, const std::string& name, uint32_t num_params,
                    Root<Array>& r_param_matchers, NativeHandler native_handler);
    void add_intrinsic(GC& gc, Root<Assoc>& r_module, const std::string& name, uint32_t num_params,
                       Root<Array>& r_param_matchers, IntrinsicHandler intrinsic_handler);
};

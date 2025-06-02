#pragma once

#include "gc.h"
#include "value.h"
#include "vm.h"

namespace Katsu
{
    // Add builtins to the VM's builtin array and to the module.
    void register_builtins(VM& vm, Root<Module>& r_module);
};

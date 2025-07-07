#pragma once

#include "gc.h"
#include "value.h"
#include "vm.h"

namespace Katsu
{
    void register_ffi_builtins(VM& vm, Root<Assoc>& r_ffi);
};

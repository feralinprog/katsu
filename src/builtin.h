#pragma once

#include "gc.h"
#include "value.h"

namespace Katsu
{
    void add_builtins(GC& gc, Root<Module>& r_module);
};

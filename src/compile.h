#pragma once

#include "ast.h"
#include "gc.h"
#include "value.h"

namespace Katsu
{
    Code* compile_module(GC& gc, OptionalRoot<Module>& base,
                         std::vector<std::unique_ptr<Expr>>& module_top_level_exprs);
};

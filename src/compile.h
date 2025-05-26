#pragma once

#include "ast.h"
#include "gc.h"
#include "value.h"

namespace Katsu
{
    Module* compile_module(GC& gc, Module* base, Expr& top_level_expr);
};

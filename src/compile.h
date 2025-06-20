#pragma once

#include "ast.h"
#include "gc.h"
#include "value.h"

namespace Katsu
{
    class compile_error : public std::runtime_error
    {
    public:
        compile_error(const std::string& message, const SourceSpan& _span)
            : std::runtime_error(message)
            , span(_span)
        {}

        const SourceSpan span;
    };

    Code* compile_into_module(GC& gc, Root<Module>& r_module, SourceSpan& span,
                              std::vector<std::unique_ptr<Expr>>& module_top_level_exprs);
    Code* compile_module(GC& gc, OptionalRoot<Module>& r_base, SourceSpan& span,
                         std::vector<std::unique_ptr<Expr>>& module_top_level_exprs);
};

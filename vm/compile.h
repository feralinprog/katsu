#pragma once

#include "ast.h"
#include "condition.h"
#include "gc.h"
#include "value.h"

namespace Katsu
{
    class compile_error : public condition_error
    {
    public:
        compile_error(const std::string& condition, const std::string& message,
                      const SourceSpan& _span)
            : condition_error(condition, message)
            , span(_span)
        {}

        compile_error(const std::string& message, const SourceSpan& _span)
            : condition_error("compile-error", message)
            , span(_span)
        {}

        const SourceSpan span;
    };

    // The imports are a vector of (expected to be) assocs. Any non-assocs are ignored,
    // and assocs are used as extra names / values that are usable by the expression under
    // compilation.
    Code* compile_into_module(VM& vm, Root<Assoc>& r_module, Root<Vector>& r_imports,
                              SourceSpan& span,
                              std::vector<std::unique_ptr<Expr>>& module_top_level_exprs);
};

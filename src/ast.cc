#include "ast.h"

namespace Katsu
{
    std::vector<std::unique_ptr<Expr>>* Expr::sequence_components()
    {
        return nullptr;
    }

    std::vector<std::unique_ptr<Expr>>* SequenceExpr::sequence_components()
    {
        return &this->components;
    }
};

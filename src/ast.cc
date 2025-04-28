#include "ast.h"

namespace Katsu
{
    void UnaryOpExpr::accept(ExprVisitor& visitor)
    {
        visitor.visit(*this);
    }
    void BinaryOpExpr::accept(ExprVisitor& visitor)
    {
        visitor.visit(*this);
    }
    void NameExpr::accept(ExprVisitor& visitor)
    {
        visitor.visit(*this);
    }
    void LiteralExpr::accept(ExprVisitor& visitor)
    {
        visitor.visit(*this);
    }
    void UnaryMessageExpr::accept(ExprVisitor& visitor)
    {
        visitor.visit(*this);
    }
    void NAryMessageExpr::accept(ExprVisitor& visitor)
    {
        visitor.visit(*this);
    }
    void ParenExpr::accept(ExprVisitor& visitor)
    {
        visitor.visit(*this);
    }
    void QuoteExpr::accept(ExprVisitor& visitor)
    {
        visitor.visit(*this);
    }
    void DataExpr::accept(ExprVisitor& visitor)
    {
        visitor.visit(*this);
    }
    void SequenceExpr::accept(ExprVisitor& visitor)
    {
        visitor.visit(*this);
    }
    void TupleExpr::accept(ExprVisitor& visitor)
    {
        visitor.visit(*this);
    }

    std::vector<std::unique_ptr<Expr>>* Expr::sequence_components()
    {
        return nullptr;
    }
    std::vector<std::unique_ptr<Expr>>* SequenceExpr::sequence_components()
    {
        return &this->components;
    }
};

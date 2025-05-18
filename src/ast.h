#pragma once

#include <memory>

#include "span.h"
#include "token.h"

namespace Katsu
{
    class ExprVisitor;

    struct Expr
    {
        SourceSpan span;

        Expr(SourceSpan _span)
            : span(_span)
        {}

        virtual ~Expr() = default;

        virtual void accept(ExprVisitor& visitor) = 0;

        // nullptr if no sequence components (default!).
        virtual std::vector<std::unique_ptr<Expr>>* sequence_components();
    };

    struct UnaryOpExpr : public Expr
    {
        Token op;
        std::unique_ptr<Expr> arg;

        UnaryOpExpr(SourceSpan _span, Token _op, std::unique_ptr<Expr> _arg)
            : Expr(_span)
            , op(_op)
            , arg(std::move(_arg))
        {}

        void accept(ExprVisitor& visitor) override;
    };

    struct BinaryOpExpr : public Expr
    {
        Token op;
        std::unique_ptr<Expr> left;
        std::unique_ptr<Expr> right;

        BinaryOpExpr(SourceSpan _span, Token _op, std::unique_ptr<Expr> _left,
                     std::unique_ptr<Expr> _right)
            : Expr(_span)
            , op(_op)
            , left(std::move(_left))
            , right(std::move(_right))
        {}

        void accept(ExprVisitor& visitor) override;
    };

    struct NameExpr : public Expr
    {
        Token name;

        NameExpr(SourceSpan _span, Token _name)
            : Expr(_span)
            , name(_name)
        {}

        void accept(ExprVisitor& visitor) override;
    };

    struct LiteralExpr : public Expr
    {
        Token literal;

        LiteralExpr(SourceSpan _span, Token _literal)
            : Expr(_span)
            , literal(_literal)
        {}

        void accept(ExprVisitor& visitor) override;
    };

    struct UnaryMessageExpr : public Expr
    {
        std::unique_ptr<Expr> target;
        Token message;

        UnaryMessageExpr(SourceSpan _span, std::unique_ptr<Expr> _target, Token _message)
            : Expr(_span)
            , target(std::move(_target))
            , message(_message)
        {}

        void accept(ExprVisitor& visitor) override;
    };

    struct NAryMessageExpr : public Expr
    {
        std::optional<std::unique_ptr<Expr>> target;
        std::vector<Token> messages;
        std::vector<std::unique_ptr<Expr>> args;

        NAryMessageExpr(SourceSpan _span, std::optional<std::unique_ptr<Expr>> _target,
                        std::vector<Token> _messages, std::vector<std::unique_ptr<Expr>> _args)
            : Expr(_span)
            , target(std::move(_target))
            , messages(_messages)
            , args(std::move(_args))
        {}

        void accept(ExprVisitor& visitor) override;
    };

    struct ParenExpr : public Expr
    {
        std::unique_ptr<Expr> inner;

        ParenExpr(SourceSpan _span, std::unique_ptr<Expr> _inner)
            : Expr(_span)
            , inner(std::move(_inner))
        {}

        void accept(ExprVisitor& visitor) override;
    };

    struct BlockExpr : public Expr
    {
        std::vector<std::string> parameters;
        std::unique_ptr<Expr> body;

        BlockExpr(SourceSpan _span, std::vector<std::string> _parameters,
                  std::unique_ptr<Expr> _body)
            : Expr(_span)
            , parameters(_parameters)
            , body(std::move(_body))
        {}

        void accept(ExprVisitor& visitor) override;
    };

    struct DataExpr : public Expr
    {
        std::vector<std::unique_ptr<Expr>> components;

        DataExpr(SourceSpan _span, std::vector<std::unique_ptr<Expr>> _components)
            : Expr(_span)
            , components(std::move(_components))
        {}

        void accept(ExprVisitor& visitor) override;
    };

    struct SequenceExpr : public Expr
    {
        std::vector<std::unique_ptr<Expr>> components;

        SequenceExpr(SourceSpan _span, std::vector<std::unique_ptr<Expr>> _components)
            : Expr(_span)
            , components(std::move(_components))
        {}

        void accept(ExprVisitor& visitor) override;

        std::vector<std::unique_ptr<Expr>>* sequence_components() override;
    };

    struct TupleExpr : public Expr
    {
        std::vector<std::unique_ptr<Expr>> components;

        TupleExpr(SourceSpan _span, std::vector<std::unique_ptr<Expr>> _components)
            : Expr(_span)
            , components(std::move(_components))
        {}

        void accept(ExprVisitor& visitor) override;
    };

    class ExprVisitor
    {
    public:
        virtual ~ExprVisitor() = default;

        virtual void visit(UnaryOpExpr& expr) = 0;
        virtual void visit(BinaryOpExpr& expr) = 0;
        virtual void visit(NameExpr& expr) = 0;
        virtual void visit(LiteralExpr& expr) = 0;
        virtual void visit(UnaryMessageExpr& expr) = 0;
        virtual void visit(NAryMessageExpr& expr) = 0;
        virtual void visit(ParenExpr& expr) = 0;
        virtual void visit(BlockExpr& expr) = 0;
        virtual void visit(DataExpr& expr) = 0;
        virtual void visit(SequenceExpr& expr) = 0;
        virtual void visit(TupleExpr& expr) = 0;
    };
};
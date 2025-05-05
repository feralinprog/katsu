#pragma once

#include <map>
#include <stdexcept>

#include "ast.h"
#include "lexer.h"

namespace Katsu
{
    class parse_error : public std::runtime_error
    {
    public:
        parse_error(const std::string& message, const SourceSpan& _span)
            : std::runtime_error(message)
            , span(_span)
        {}

        const SourceSpan span;
    };

    class PrattParser;

    class PrefixParselet
    {
    public:
        virtual ~PrefixParselet() = default;

        virtual std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                            const Token& token) = 0;
    };

    class InfixParselet
    {
    public:
        virtual ~InfixParselet() = default;

        virtual std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                            std::unique_ptr<Expr> left, const Token& token) = 0;

        virtual int precedence(const Token& token) = 0;
    };

    class PrattParser
    {
    public:
        PrattParser()
            : prefix_parselets{}
            , infix_parselets{}
        {}

        virtual ~PrattParser() = default;

        // Precondition: stream still has a remaining token other than NEWLINE and END.
        std::unique_ptr<Expr> parse(TokenStream& stream, int precedence = 0,
                                    bool is_toplevel = false) const;

        void add_parselet(TokenType type, PrefixParselet& parselet);
        void add_parselet(TokenType type, InfixParselet& parselet);

    private:
        std::map<TokenType, PrefixParselet&> prefix_parselets;
        std::map<TokenType, InfixParselet&> infix_parselets;
    };

    std::unique_ptr<PrattParser> make_default_parser();
};
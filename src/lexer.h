#pragma once

#include <optional>
#include <variant>

#include "span.h"

namespace Katsu
{
    enum class TokenType
    {
        END,        // end of source
        ERROR,
        SEMICOLON,
        NEWLINE,
        WHITESPACE,
        COMMENT,
        LPAREN,     // (
        RPAREN,     // )
        LCURLY,     // {
        RCURLY,     // }
        LSQUARE,    // [
        RSQUARE,    // ]
        COMMA,      // ,
        NAME,       // same as operator, except operators have different character set
        MESSAGE,    // <name/operator>: or <name/operator>.
        SYMBOL,     // :<name/operator>
        QUOTE,      // '<name>
        BACKSLASH,  // \ (as stated on the tin)
        OPERATOR,   // same as names, but limited character set
        INTEGER,
        STRING,
    };

    using TokenValue = std::variant<std::string, long long, std::monostate>;

    struct Token
    {
        SourceSpan span;
        TokenType type;
        TokenValue value;
    };

    class Lexer
    {
    public:
        Lexer(const SourceFile& _source)
            : source(_source)
            , source_len(_source.source->size())
            , loc()
        {}

        Token next();

    private:
        // Determine if at end of file or not.
        bool eof();

        // Get current character without bumping 'loc' to the next spot.
        char peek();
        // Get current character and bump `loc` to the next spot.
        char get();

        // Source file to pull tokens from.
        const SourceFile source;
        // Length of the source. (TODO: for future interactive use, need to delete this.)
        const size_t source_len;

        // Current location in `source`.
        SourceLocation loc;
    };

    class TokenStream
    {
    public:
        TokenStream(Lexer& _lexer)
            : lexer(_lexer)
            , current()
        {}

        Token peek();

        bool current_has_type(TokenType type);

        Token consume();
        std::optional<Token> consume(TokenType expected_type);

    private:
        // Skip WHITESPACE / COMMENT tokens, and also condense multiple NEWLINE tokens
        // (after whitespace skipping) into a single one.
        // Returns a single condensed NEWLINE token, if available; else std::nullopt.
        std::optional<Token> condense();

        // Token source.
        Lexer& lexer;

        // Current token, or std::nullopt if the token pump has not been primed yet.
        std::optional<Token> current;
    };
};

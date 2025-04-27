#pragma once

#include <optional>
#include <variant>
#include <iostream>

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

    std::ostream& operator<<(std::ostream& s, TokenType type);

    using TokenValue = std::variant<std::string, long long, std::monostate>;

    struct Token
    {
        SourceSpan span;
        TokenType type;
        TokenValue value;
    };
};

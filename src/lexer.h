#pragma once

#include <deque>
#include <optional>

#include "span.h"
#include "token.h"

namespace Katsu
{
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
            , lookahead{}
        {}

        Token peek();

        bool current_has_type(TokenType type);

        Token consume();

    private:
        // Skip WHITESPACE / COMMENT tokens, and also condense multiple NEWLINE tokens
        // (after whitespace skipping) into a single one.
        void condense();

        // Ensure there is at least one token in the lookahead.
        void pump();

        // Token source.
        Lexer& lexer;

        // Queue of tokens we have available from the lexer.
        std::deque<Token> lookahead;
    };
};

#include "lexer.h"

#include "assertions.h"

#include <cctype>
#include <stdexcept>
#include <string>

namespace Katsu
{
    bool Lexer::eof()
    {
        return this->loc.index == this->source_len;
    }

    char Lexer::peek()
    {
        ASSERT(!this->eof());
        return (*this->source.source)[this->loc.index];
    }

    char Lexer::get()
    {
        ASSERT(!this->eof());

        char cur = this->peek();

        if (cur == '\n') {
            this->loc.line++;
            this->loc.column = 0;
        } else {
            this->loc.column++;
        }
        this->loc.index++;

        return cur;
    }

    Token Lexer::next()
    {
        ASSERT_MSG(this->loc.index <= this->source_len, "lexer got out of bounds");

        if (this->eof()) {
            return Token{
                .span = SourceSpan{.file = this->source, .start = this->loc, .end = this->loc},
                .type = TokenType::END,
                .value = std::monostate(),
            };
        }

        SourceLocation start = this->loc;
        const auto make_token = [this, &start](TokenType type,
                                               TokenValue value = std::monostate{}) {
            return Token{
                .span = {.file = this->source, .start = start, .end = this->loc},
                .type = type,
                .value = value,
            };
        };

        const auto is_whitespace = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };

        char c = this->get();
        switch (c) {
            case ';': return make_token(TokenType::SEMICOLON);
            case '\n': return make_token(TokenType::NEWLINE);
            case ' ':
            case '\t':
            case '\r': {
                // Collect whitespace!
                while (!this->eof() && is_whitespace(this->peek())) {
                    c = this->get();
                }
                return make_token(TokenType::WHITESPACE);
            }
            case '(': return make_token(TokenType::LPAREN);
            case ')': return make_token(TokenType::RPAREN);
            case '{': return make_token(TokenType::LCURLY);
            case '}': return make_token(TokenType::RCURLY);
            case '[': return make_token(TokenType::LSQUARE);
            case ']': return make_token(TokenType::RSQUARE);
            case ',': return make_token(TokenType::COMMA);
            case '\\': return make_token(TokenType::BACKSLASH);
            case '"': {
                // Consume string until terminating quote.
                std::string str;
                while (!this->eof() && this->peek() != '"') {
                    c = this->get();
                    // TODO: handle escape sequences
                    str += c;
                }
                if (this->eof()) {
                    // There wasn't any terminating quote.
                    return make_token(TokenType::ERROR);
                }
                // Skip over terminating quote.
                this->get();
                return make_token(TokenType::STRING, str);
            }
            default: {
                // It's a comment, symbol, message, quote, operator, name, or integer.
                // We need to collect the lexeme and then see what kind of token it is.

                const auto is_word_char = [](char c) {
                    return !(c == ';' || c == '\n' || c == ' ' || c == '\t' || c == '\r' ||
                             c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' ||
                             c == '"' || c == ',');
                };
                const auto is_op_char = [](char c) {
                    return c == '`' || c == '~' || c == '!' || c == '@' || c == '#' || c == '$' ||
                           c == '%' || c == '^' || c == '&' || c == '*' || c == '-' || c == '+' ||
                           c == '=' || c == '\\' || c == '|' || c == '"' || c == '\'' || c == ',' ||
                           c == '<' || c == '.' || c == '>' || c == '/' || c == '?';
                };

                // Collect word characters.
                std::string word;
                word.push_back(c);
                while (!this->eof() && is_word_char(this->peek())) {
                    word.push_back(this->get());
                }

                // Downselect to the token type, and pull out a value as necessary
                // from the word.

                // Special case: comments.
                bool all_comment_chars = true;
                for (char c : word) {
                    if (c != '#') {
                        all_comment_chars = false;
                        break;
                    }
                }
                if (all_comment_chars) {
                    // It's a comment. Continue until end of line or EOF.
                    while (!this->eof() && this->peek() != '\n') {
                        this->get();
                    }
                    return make_token(TokenType::COMMENT);
                }

                // Symbols / messages:
                if (word.find(':') != std::string::npos) {
                    if (word == ":") {
                        return make_token(TokenType::ERROR);
                    }
                    // Special case "::" as a message token with name ":".
                    // This is purely a convenience.
                    if (word[0] == ':' && word != "::") {
                        return make_token(TokenType::SYMBOL, word.substr(1, word.size() - 1));
                    } else if (word[word.size() - 1] == ':') {
                        return make_token(TokenType::MESSAGE, word.substr(0, word.size() - 1));
                    } else {
                        // Word should not both start and end with ':'.
                        return make_token(TokenType::ERROR);
                    }
                }

                // Operators:
                bool all_op_chars = true;
                for (char c : word) {
                    if (!is_op_char(c)) {
                        all_op_chars = false;
                        break;
                    }
                }
                if (all_op_chars) {
                    return make_token(TokenType::OPERATOR, word);
                }

                // Special-case operators:
                // (TODO: expose to language?)
                if (word == "and" || word == "or" || word == "not") {
                    return make_token(TokenType::OPERATOR, word);
                }

                // Integers:
                // (TODO: different bases, maybe also _ for separation)
                bool all_digits = true;
                bool first = true;
                for (char c : word) {
                    if (!(isdigit(c) || (first && (c == '+' || c == '-')))) {
                        all_digits = false;
                        break;
                    }
                    first = false;
                }
                if (all_digits) {
                    return make_token(TokenType::INTEGER, std::stoll(word));
                }

                // TODO: non-integer number literals.

                // Otherwise, by default it's a name.
                return make_token(TokenType::NAME, word);
            }
        }
    }

    Token TokenStream::peek()
    {
        this->condense();
        // condense() should have ensured that there is a current token available.
        return this->lookahead[0];
    }

    bool TokenStream::current_has_type(TokenType type)
    {
        return this->peek().type == type;
    }

    Token TokenStream::consume()
    {
        this->condense();
        // condense() should have ensured that there is a current token available.
        Token token = this->lookahead[0];
        this->lookahead.pop_front();
        return token;
    }

    void TokenStream::condense()
    {
        // Prime the pump, if needed.
        this->pump();
        // Skip whitespace / comment.
        while (this->lookahead[0].type == TokenType::WHITESPACE ||
               this->lookahead[0].type == TokenType::COMMENT) {
            this->lookahead.pop_front();
            this->pump();
        }
        // Condense newlines.
        if (this->lookahead[0].type == TokenType::NEWLINE) {
            // Keep this NEWLINE prefix, and skip following tokens until there's a
            // non-whitespace-or-newline.
            // TODO: really should just handle this as part of some internal state,
            // without collecting further tokens. Should remember that the last
            // token was a newline, and then _next_ time we peek / consume we should
            // skip whitespace / comments / newlines.
            if (this->lookahead.size() < 2) {
                this->lookahead.push_back(this->lexer.next());
            }
            while (this->lookahead[1].type == TokenType::WHITESPACE ||
                   this->lookahead[1].type == TokenType::COMMENT ||
                   this->lookahead[1].type == TokenType::NEWLINE) {
                this->lookahead[1] = this->lexer.next();
            }
        }
    }

    void TokenStream::pump()
    {
        if (this->lookahead.empty()) {
            this->lookahead.push_back(this->lexer.next());
        }
    }
};

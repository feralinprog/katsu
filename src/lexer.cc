#include "lexer.h"

#include <stdexcept>
#include <cctype>
#include <string>

namespace Katsu
{
    bool Lexer::eof()
    {
        return this->loc.index == this->source_len;
    }

    char Lexer::peek()
    {
        if (this->eof()) {
            throw std::runtime_error("get() must not be called if eof()");
        }

        return (*this->source.source)[this->loc.index];
    }

    char Lexer::get()
    {
        if (this->eof()) {
            throw std::runtime_error("get() must not be called if eof()");
        }

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
        if (this->loc.index > this->source_len) {
            throw std::runtime_error("lexer got out of bounds");
        }

        if (this->eof()) {
            return Token {
                .span = SourceSpan { .file = this->source, .start = this->loc, .end = this->loc },
                .type = TokenType::END,
                .value = std::monostate{},
            };
        }

        SourceLocation start = this->loc;
        const auto make_token = [this, &start](TokenType type, TokenValue value = std::monostate{}) {
            return Token {
                .span = { .file = this->source, .start = start, .end = this->loc },
                .type = type,
                .value = value,
            };
        };

        const auto is_whitespace = [](char c) {
            return c == ' ' || c == '\t' || c == '\r';
        };

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
                    return !(c == ';' || c == '\n' || c == ' ' || c == '\t' || c == '\r'
                        || c == '(' || c == ')' || c == '{' || c == '}' || c == '['
                        || c == ']' || c == '"' || c == ',');
                };
                const auto is_op_char = [](char c) {
                    return c == '`' || c == '~' || c == '!' || c == '@' || c == '#'
                        || c == '$' || c == '%' || c == '^' || c == '&' || c == '*'
                        || c == '-' || c == '+' || c == '=' || c == '\\' || c == '|'
                        || c == '"' || c == '\'' || c == ',' || c == '<' || c == '.'
                        || c == '>' || c == '/' || c == '?';
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
                if (word[0] == '#') {
                    // It's a comment. Continue until end of line or EOF.
                    while (!this->eof() && this->peek() != '\n') {
                        this->get();
                    }
                    return make_token(TokenType::COMMENT);
                }

                // Symbols / messages:
                if (word.find(':') != std::string::npos)
                {
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

                // Quotes:
                if (word[0] == '\'' && word != "'") {
                    return make_token(TokenType::QUOTE, word.substr(1, word.size() - 1));
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
                for (char c : word) {
                    if (!isdigit(c)) {
                        all_digits = false;
                        break;
                    }
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
        return this->condense().value_or(this->current.value());
    }

    bool TokenStream::current_has_type(TokenType type)
    {
        return this->peek().type == type;
    }

    Token TokenStream::consume()
    {
        std::optional<Token> maybe_newline = this->condense();
        if (maybe_newline.has_value()) {
            return *maybe_newline;
        }
        // condense() should have ensured that there is a current token available.
        Token token = this->current.value();
        this->current = this->lexer.next();
        return token;
    }

    std::optional<Token> TokenStream::consume(TokenType expected_type)
    {
        Token token = this->consume();
        if (token.type != expected_type) {
            return std::nullopt;
        }
        return token;
    }

    std::optional<Token> TokenStream::condense()
    {
        // Prime the pump, if needed.
        if (!this->current.has_value()) {
            this->current = this->lexer.next();
        }
        // Skip whitespace / comment.
        while (this->current->type == TokenType::WHITESPACE ||
               this->current->type == TokenType::COMMENT) {
            this->current = this->lexer.next();
        }
        // Condense newlines.
        if (this->current->type == TokenType::NEWLINE) {
            Token initial_newline = *this->current;
            while (this->current->type == TokenType::WHITESPACE ||
                this->current->type == TokenType::COMMENT ||
                this->current->type == TokenType::NEWLINE) {
                this->current = this->lexer.next();
            }
            return initial_newline;
        }
        return std::nullopt;
    }
};

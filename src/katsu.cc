#include "katsu.h"
#include "lexer.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include <variant>

namespace Katsu
{
    std::ostream& operator<<(std::ostream& s, Token token)
    {
        switch (token.type) {
            case TokenType::END: s << "END"; break;
            case TokenType::ERROR: s << "ERROR"; break;
            case TokenType::SEMICOLON: s << "SEMICOLON"; break;
            case TokenType::NEWLINE: s << "NEWLINE"; break;
            case TokenType::WHITESPACE: s << "WHITESPACE"; break;
            case TokenType::COMMENT: s << "COMMENT"; break;
            case TokenType::LPAREN: s << "LPAREN"; break;
            case TokenType::RPAREN: s << "RPAREN"; break;
            case TokenType::LCURLY: s << "LCURLY"; break;
            case TokenType::RCURLY: s << "RCURLY"; break;
            case TokenType::LSQUARE: s << "LSQUARE"; break;
            case TokenType::RSQUARE: s << "RSQUARE"; break;
            case TokenType::COMMA: s << "COMMA"; break;
            case TokenType::NAME: s << "NAME"; break;
            case TokenType::MESSAGE: s << "MESSAGE"; break;
            case TokenType::SYMBOL: s << "SYMBOL"; break;
            case TokenType::QUOTE: s << "QUOTE"; break;
            case TokenType::BACKSLASH: s << "BACKSLASH"; break;
            case TokenType::OPERATOR: s << "OPERATOR"; break;
            case TokenType::INTEGER: s << "INTEGER"; break;
            case TokenType::STRING: s << "STRING"; break;
            default: throw std::runtime_error("forgot a case");
        }
        if (const std::string* strval = std::get_if<std::string>(&token.value)) {
            s << "(value=\"" << *strval << "\")";
        } else if (const long long* intval = std::get_if<long long>(&token.value)) {
            s << "(value=" << *intval << ")";
        }
        return s;
    }

    void execute_source(const SourceFile source)
    {
        Lexer lexer(source);
        TokenStream stream(lexer);

        Token token;
        while (true) {
            token = stream.consume();

            std::cout << token << "\n";

            if (token.type == TokenType::END) {
                break;
            }
        }
    }

    void execute_file(const std::string& filepath)
    {
        std::ifstream file_stream;
        // Raise exceptions on logical error or read/write error.
        file_stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        file_stream.open(filepath.c_str());

        std::stringstream str_stream;
        str_stream << file_stream.rdbuf();
        std::string file_contents = str_stream.str();

        SourceFile source {
            .path = std::make_shared<std::string>(filepath),
            .source = std::make_shared<std::string>(std::move(file_contents))
        };

        execute_source(source);
    }
};

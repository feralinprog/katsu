#include "token.h"

namespace Katsu
{
    std::ostream& operator<<(std::ostream& s, TokenType type)
    {
        switch (type) {
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
        return s;
    }
};

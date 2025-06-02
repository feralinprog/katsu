#include "parser.h"

#include "assertions.h"
#include "span.h"

#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace Katsu
{
    // // TODO: deleteme
    // std::ostream& operator<<(std::ostream& s, Token token);

    // int depth = 0;
    // bool should_log = false;

    std::unique_ptr<Expr> PrattParser::parse(TokenStream& stream, int precedence,
                                             bool is_toplevel) const
    {
        // depth += 1;
        Token token = stream.consume();
        while (token.type == TokenType::NEWLINE) {
            token = stream.consume();
        }
        ASSERT_MSG(token.type != TokenType::END,
                   "there must be a remaining token that is not NEWLINE or EOF");

        const auto& prefix_it = this->prefix_parselets.find(token.type);
        if (prefix_it == this->prefix_parselets.end()) {
            std::stringstream ss;
            ss << "No prefix parselet available for " << token.type << ".";
            throw parse_error(ss.str(), token.span);
        }

        PrefixParselet& prefix = prefix_it->second;
        // if (should_log)
        // {
        //     for (int i = 0; i < depth - 1; i++) {
        //         std::cout << "| ";
        //     }
        //     std::cout << "parsing prefix " << token.type << ", prec=" << precedence << ", token="
        //               << token << "\n";
        // }
        std::unique_ptr<Expr> expr = prefix.parse(stream, *this, token);

        const auto active_precedence = [this](Token token) {
            const auto& infix_it = this->infix_parselets.find(token.type);
            if (infix_it == this->infix_parselets.end()) {
                // TODO: throw a parse error? No infix parselets available to determine precedence
                // for {token}
                return 0;
            }
            InfixParselet& infix = infix_it->second;
            int prec = infix.precedence(token);
            return prec;
        };

        while (active_precedence(stream.peek()) > precedence &&
               (!is_toplevel || (!stream.current_has_type(TokenType::SEMICOLON) &&
                                 !stream.current_has_type(TokenType::NEWLINE)))) {
            token = stream.consume();
            // if (should_log)
            // {
            //     for (int i = 0; i < depth - 1; i++) {
            //         std::cout << "| ";
            //     }
            //     std::cout << "got infix token " << token.type << ", prec=" << precedence
            //               << ", token=" << token << "\n";
            // }
            if (token.type == TokenType::END) {
                throw parse_error("Unexpected EOF.", token.span);
            }
            const auto& infix_it = this->infix_parselets.find(token.type);
            if (infix_it == this->infix_parselets.end()) {
                std::stringstream ss;
                ss << "No infix parselet available for " << token.type << ".";
                throw parse_error(ss.str(), token.span);
            }
            InfixParselet& infix = infix_it->second;
            // if (should_log)
            // {
            //     for (int i = 0; i < depth - 1; i++) {
            //         std::cout << "| ";
            //     }
            //     std::cout << "parsing infix " << token.type << ", prec=" << precedence << "\n";
            // }
            expr = infix.parse(stream, *this, std::move(expr), token);
        }

        // if (should_log)
        // {
        //     for (int i = 0; i < depth - 1; i++) {
        //         std::cout << "| ";
        //     }
        //     std::cout << "finished parsing at prec=" << precedence << " since next token "
        //               << stream.peek() << " has active_prec=" << active_precedence(stream.peek())
        //               << "\n";
        // }

        // depth -= 1;
        return expr;
    }

    void PrattParser::add_parselet(TokenType type, PrefixParselet& parselet)
    {
        this->prefix_parselets.emplace(type, parselet);
    }

    void PrattParser::add_parselet(TokenType type, InfixParselet& parselet)
    {
        this->infix_parselets.emplace(type, parselet);
    }


    enum class Precedence
    {
        SEQUENCING = 10,

        N_ARY_MESSAGE = 30,

        ASSIGNMENT = 50,

        COMMA = 70,

        CONCATENATION = 100,
        OR = 110,
        AND = 120,
        COMPARISON = 130,
        SUM_DIFFERENCE = 140,
        DIVISION = 150,
        PRODUCT = 160,

        PREFIX = 500,

        UNARY_MESSAGE = 1000,
    };

    Token expect(TokenStream& stream, TokenType expected_type)
    {
        Token token = stream.consume();
        if (token.type == expected_type) {
            return token;
        } else {
            std::stringstream ss;
            ss << "Expected " << expected_type << ", got " << token.type << ".";
            throw parse_error(ss.str(), token.span);
        }
    }

    class OperatorPrefixParselet : public PrefixParselet
    {
    public:
        std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                    const Token& token) override
        {
            std::unique_ptr<Expr> right =
                parser.parse(stream, static_cast<int>(Precedence::PREFIX));
            return std::make_unique<UnaryOpExpr>(SourceSpan::combine({token.span, right->span}),
                                                 token,
                                                 std::move(right));
        }
    };

    class MessagePrefixParselet : public PrefixParselet
    {
    public:
        std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                    const Token& token) override
        {
            std::vector<Token> messages{};
            std::vector<std::unique_ptr<Expr>> args{};

            messages.push_back(token);
            args.push_back(parser.parse(stream, static_cast<int>(Precedence::N_ARY_MESSAGE) + 1));
            while (stream.current_has_type(TokenType::MESSAGE)) {
                messages.push_back(stream.consume());
                args.push_back(
                    parser.parse(stream, static_cast<int>(Precedence::N_ARY_MESSAGE) + 1));
            }

            std::vector<SourceSpan> spans{};
            for (const auto& token : messages) {
                spans.push_back(token.span);
            }
            for (const auto& arg : args) {
                spans.push_back(arg->span);
            }

            return std::make_unique<NAryMessageExpr>(SourceSpan::combine(spans),
                                                     std::nullopt /* target */,
                                                     messages,
                                                     std::move(args));
        }
    };

    class LParenPrefixParselet : public PrefixParselet
    {
    public:
        std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                    const Token& token) override
        {
            // Support syntax `()` -> empty tuple.
            while (stream.current_has_type(TokenType::NEWLINE)) {
                stream.consume();
            }
            if (stream.current_has_type(TokenType::RPAREN)) {
                Token rparen = stream.consume();
                std::vector<std::unique_ptr<Expr>> components{};
                return std::make_unique<TupleExpr>(SourceSpan::combine({token.span, rparen.span}),
                                                   std::move(components));
            }
            std::unique_ptr<Expr> inner = parser.parse(stream, 0 /* precedence */);
            Token rparen = expect(stream, TokenType::RPAREN);
            return std::make_unique<ParenExpr>(
                SourceSpan::combine({token.span, inner->span, rparen.span}),
                std::move(inner));
        }
    };

    class LSquarePrefixParselet : public PrefixParselet
    {
    public:
        std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                    const Token& token) override
        {
            std::unique_ptr<Expr> body = parser.parse(stream, 0 /* precedence */);
            Token rsquare = expect(stream, TokenType::RSQUARE);
            std::vector<std::string> parameters{};
            return std::make_unique<BlockExpr>(
                SourceSpan::combine({token.span, body->span, rsquare.span}),
                parameters,
                std::move(body));
        }
    };

    class LCurlyPrefixParselet : public PrefixParselet
    {
    public:
        std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                    const Token& token) override
        {
            while (stream.current_has_type(TokenType::NEWLINE)) {
                stream.consume();
            }
            if (stream.current_has_type(TokenType::RCURLY)) {
                Token rcurly = stream.consume();
                std::vector<std::unique_ptr<Expr>> components{};
                return std::make_unique<DataExpr>(SourceSpan::combine({token.span, rcurly.span}),
                                                  std::move(components));
            }

            std::vector<std::unique_ptr<Expr>> components{};
            std::unique_ptr<Expr> inner = parser.parse(stream, 0 /* precedence */);
            Token rcurly = expect(stream, TokenType::RCURLY);
            // Lift the inner's sequence portions if it is a SequenceExpr; otherwise assume this
            // is a single-entry data structure. This is to support syntax like { 1; 2 } producing
            // a vector (1,2) as opposed to a vector (2), while { 1 } still will correctly produce
            // a vector (1); it also allows separating elements by newlines (like any other
            // sequencing expression).
            std::vector<std::unique_ptr<Expr>>* sequence_components = inner->sequence_components();
            SourceSpan inner_span = inner->span; // Since we might move `inner` into `components`.
            if (sequence_components) {
                for (std::unique_ptr<Expr>& component : *sequence_components) {
                    components.push_back(std::move(component));
                }
            } else {
                components.push_back(std::move(inner));
            }
            return std::make_unique<DataExpr>(
                SourceSpan::combine({token.span, inner_span, rcurly.span}),
                std::move(components));
        }
    };

    class NamePrefixParselet : public PrefixParselet
    {
    public:
        std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                    const Token& token) override
        {
            return std::make_unique<NameExpr>(token.span, token);
        }
    };

    class BackslashPrefixParselet : public PrefixParselet
    {
    public:
        std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                    const Token& token) override
        {
            std::vector<Token> param_tokens{};
            std::vector<std::string> parameters{};
            while (stream.current_has_type(TokenType::NAME)) {
                Token param = stream.consume();
                param_tokens.push_back(param);
                parameters.push_back(std::get<std::string>(param.value));
            }
            Token lsquare = expect(stream, TokenType::LSQUARE);
            std::unique_ptr<Expr> body = parser.parse(stream, 0 /* precedence */);
            Token rsquare = expect(stream, TokenType::RSQUARE);

            std::vector<SourceSpan> spans{};
            spans.push_back(token.span);
            for (const auto& token : param_tokens) {
                spans.push_back(token.span);
            }
            spans.push_back(lsquare.span);
            spans.push_back(body->span);
            spans.push_back(rsquare.span);

            return std::make_unique<BlockExpr>(SourceSpan::combine(spans),
                                               parameters,
                                               std::move(body));
        }
    };

    class LiteralPrefixParselet : public PrefixParselet
    {
    public:
        std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                    const Token& token) override
        {
            return std::make_unique<LiteralExpr>(token.span, token);
        }
    };

    class NameInfixParselet : public InfixParselet
    {
    public:
        std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                    std::unique_ptr<Expr> left, const Token& token) override
        {
            return std::make_unique<UnaryMessageExpr>(SourceSpan::combine({left->span, token.span}),
                                                      std::move(left) /* target */,
                                                      token /* message */
            );
        }

        int precedence(const Token& token) override
        {
            return static_cast<int>(Precedence::UNARY_MESSAGE);
        }
    };

    class MessageInfixParselet : public InfixParselet
    {
    public:
        std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                    std::unique_ptr<Expr> left, const Token& token) override
        {
            std::vector<Token> messages{token};
            std::vector<std::unique_ptr<Expr>> args{};
            args.push_back(parser.parse(stream, static_cast<int>(Precedence::N_ARY_MESSAGE) + 1));
            while (stream.current_has_type(TokenType::MESSAGE)) {
                messages.push_back(stream.consume());
                args.push_back(
                    parser.parse(stream, static_cast<int>(Precedence::N_ARY_MESSAGE) + 1));
            }

            std::vector<SourceSpan> spans{};
            spans.push_back(left->span);
            for (const auto& token : messages) {
                spans.push_back(token.span);
            }
            for (const auto& arg : args) {
                spans.push_back(arg->span);
            }

            return std::make_unique<NAryMessageExpr>(SourceSpan::combine(spans),
                                                     std::move(left) /* target */,
                                                     messages,
                                                     std::move(args));
        }

        int precedence(const Token& token) override
        {
            return static_cast<int>(Precedence::N_ARY_MESSAGE);
        }
    };

    class SequencingInfixParselet : public InfixParselet
    {
    public:
        std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                    std::unique_ptr<Expr> left, const Token& token) override
        {
            std::vector<std::unique_ptr<Expr>> sequence{};
            // We're moving `left` into `sequence`, so take a picture.
            SourceSpan left_span = left->span;
            sequence.push_back(std::move(left));

            std::vector<Token> separators{token};

            const auto parse_next_expr_or_trailing_semicolon = [&stream, &parser, &sequence]() {
                // Hack: allow trailing semicolon. Check for a following token that cannot be a
                // prefix.
                Token token = stream.peek();
                if (token.type == TokenType::RPAREN || token.type == TokenType::RCURLY ||
                    token.type == TokenType::RSQUARE || token.type == TokenType::END) {
                    return;
                }
                sequence.push_back(
                    parser.parse(stream, static_cast<int>(Precedence::SEQUENCING) + 1));
            };

            parse_next_expr_or_trailing_semicolon();
            while (stream.current_has_type(TokenType::SEMICOLON) ||
                   stream.current_has_type(TokenType::NEWLINE)) {
                separators.push_back(stream.consume());
                parse_next_expr_or_trailing_semicolon();
            }

            std::vector<SourceSpan> spans{};
            spans.push_back(left_span);
            for (const auto& expr : sequence) {
                spans.push_back(expr->span);
            }
            for (const auto& sep : separators) {
                spans.push_back(sep.span);
            }

            return std::make_unique<SequenceExpr>(SourceSpan::combine(spans), std::move(sequence));
        }

        int precedence(const Token& token) override
        {
            return static_cast<int>(Precedence::SEQUENCING);
        }
    };

    class OperatorInfixParselet : public InfixParselet
    {
    public:
        std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                    std::unique_ptr<Expr> left, const Token& token) override
        {
            ASSERT(token.type == TokenType::OPERATOR);

            const auto& prec_it = this->infix_precedence.find(std::get<std::string>(token.value));
            if (prec_it == this->infix_precedence.end()) {
                std::stringstream ss;
                ss << "Missing infix precedence for operator '"
                   << std::get<std::string>(token.value) << "'.";
                throw parse_error(ss.str(), token.span);
            }

            const auto& assoc_it =
                this->infix_associativity.find(std::get<std::string>(token.value));
            if (assoc_it == this->infix_associativity.end()) {
                std::stringstream ss;
                ss << "Missing infix associativity for operator '"
                   << std::get<std::string>(token.value) << "'.";
                throw parse_error(ss.str(), token.span);
            }

            int op_prec = static_cast<int>(prec_it->second);
            Associativity op_assoc = assoc_it->second;

            std::unique_ptr<Expr> right =
                parser.parse(stream, op_assoc == Associativity::LEFT ? op_prec : (op_prec - 1));

            return std::make_unique<BinaryOpExpr>(
                SourceSpan::combine({left->span, token.span, right->span}),
                token /* op */,
                std::move(left),
                std::move(right));
        }

        int precedence(const Token& token) override
        {
            ASSERT(token.type == TokenType::OPERATOR);

            const auto& prec_it = this->infix_precedence.find(std::get<std::string>(token.value));
            if (prec_it == this->infix_precedence.end()) {
                std::stringstream ss;
                ss << "Missing infix precedence for operator '"
                   << std::get<std::string>(token.value) << "'.";
                throw parse_error(ss.str(), token.span);
            }

            return static_cast<int>(prec_it->second);
        }

    private:
        enum class Associativity
        {
            LEFT,
            RIGHT
        };

        std::unordered_map<std::string, Precedence> infix_precedence{
            {"=",   Precedence::ASSIGNMENT    },
            {"~",   Precedence::CONCATENATION },
            {"and", Precedence::AND           },
            {"or",  Precedence::OR            },
            {"==",  Precedence::COMPARISON    },
            {"!=",  Precedence::COMPARISON    },
            {"<",   Precedence::COMPARISON    },
            {"<=",  Precedence::COMPARISON    },
            {">",   Precedence::COMPARISON    },
            {">=",  Precedence::COMPARISON    },
            {"+",   Precedence::SUM_DIFFERENCE},
            {"-",   Precedence::SUM_DIFFERENCE},
            {"*",   Precedence::PRODUCT       },
            {"/",   Precedence::DIVISION      },
        };

        std::unordered_map<std::string, Associativity> infix_associativity{
            {"=",   Associativity::RIGHT},
            {"~",   Associativity::LEFT },
            {"and", Associativity::LEFT },
            {"or",  Associativity::LEFT },
            {"==",  Associativity::LEFT },
            {"!=",  Associativity::LEFT },
            {"<",   Associativity::LEFT },
            {"<=",  Associativity::LEFT },
            {">",   Associativity::LEFT },
            {">=",  Associativity::LEFT },
            {"+",   Associativity::LEFT },
            {"-",   Associativity::LEFT },
            {"*",   Associativity::LEFT },
            {"/",   Associativity::LEFT },
        };
    };

    class CommaInfixParselet : public InfixParselet
    {
    public:
        std::unique_ptr<Expr> parse(TokenStream& stream, const PrattParser& parser,
                                    std::unique_ptr<Expr> left, const Token& token) override
        {
            std::vector<std::unique_ptr<Expr>> components{};
            // We're moving `left` into `components`, so take a picture.
            SourceSpan left_span = left->span;
            components.push_back(std::move(left));

            std::vector<Token> separators{token};

            const auto parse_next_expr_or_trailing_comma = [&stream, &parser, &components]() {
                // Hack: allow trailing comma. Check for a following token that cannot be a prefix.
                Token token = stream.peek();
                if (token.type == TokenType::RPAREN || token.type == TokenType::RCURLY ||
                    token.type == TokenType::RSQUARE || token.type == TokenType::END) {
                    return;
                }
                components.push_back(parser.parse(stream, static_cast<int>(Precedence::COMMA) + 1));
            };

            parse_next_expr_or_trailing_comma();
            while (stream.current_has_type(TokenType::COMMA)) {
                separators.push_back(stream.consume());
                parse_next_expr_or_trailing_comma();
            }

            std::vector<SourceSpan> spans{};
            spans.push_back(left_span);
            for (const auto& expr : components) {
                spans.push_back(expr->span);
            }
            for (const auto& sep : separators) {
                spans.push_back(sep.span);
            }

            return std::make_unique<TupleExpr>(SourceSpan::combine(spans), std::move(components));
        }

        int precedence(const Token& token) override
        {
            return static_cast<int>(Precedence::COMMA);
        }
    };


    // Struct which simply holds prefix / infix parselets for lifetime reasons;
    // these parselets must survive for as long as the parser's prefix_parselets
    // and infix_parselets survive, i.e. for as long as the paser survives.
    struct DefaultParser : public PrattParser
    {
        // Prefix parselets:
        OperatorPrefixParselet operator_prefix_parselet;
        MessagePrefixParselet message_prefix_parselet;
        LParenPrefixParselet lparen_prefix_parselet;
        LSquarePrefixParselet lsquare_prefix_parselet;
        LCurlyPrefixParselet lcurly_prefix_parselet;
        NamePrefixParselet name_prefix_parselet;
        BackslashPrefixParselet backslash_prefix_parselet;
        LiteralPrefixParselet literal_prefix_parselet;

        // Infix parselets:
        NameInfixParselet name_infix_parselet;
        MessageInfixParselet message_infix_parselet;
        SequencingInfixParselet sequencing_infix_parselet;
        OperatorInfixParselet operator_infix_parselet;
        CommaInfixParselet comma_infix_parselet;
    };

    std::unique_ptr<PrattParser> make_default_parser()
    {
        auto parser = std::make_unique<DefaultParser>();

        // Prefix parselets:
        parser->add_parselet(TokenType::OPERATOR, parser->operator_prefix_parselet);
        parser->add_parselet(TokenType::MESSAGE, parser->message_prefix_parselet);
        parser->add_parselet(TokenType::LPAREN, parser->lparen_prefix_parselet);
        parser->add_parselet(TokenType::LSQUARE, parser->lsquare_prefix_parselet);
        parser->add_parselet(TokenType::LCURLY, parser->lcurly_prefix_parselet);
        parser->add_parselet(TokenType::NAME, parser->name_prefix_parselet);
        parser->add_parselet(TokenType::BACKSLASH, parser->backslash_prefix_parselet);
        parser->add_parselet(TokenType::STRING, parser->literal_prefix_parselet);
        parser->add_parselet(TokenType::INTEGER, parser->literal_prefix_parselet);
        parser->add_parselet(TokenType::SYMBOL, parser->literal_prefix_parselet);

        // Infix parselets:
        parser->add_parselet(TokenType::NAME, parser->name_infix_parselet);
        parser->add_parselet(TokenType::MESSAGE, parser->message_infix_parselet);
        parser->add_parselet(TokenType::SEMICOLON, parser->sequencing_infix_parselet);
        parser->add_parselet(TokenType::NEWLINE, parser->sequencing_infix_parselet);
        parser->add_parselet(TokenType::OPERATOR, parser->operator_infix_parselet);
        parser->add_parselet(TokenType::COMMA, parser->comma_infix_parselet);

        return parser;
    }
};

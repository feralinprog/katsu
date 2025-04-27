#include "katsu.h"
#include "lexer.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include <variant>

#include "parser.h"

namespace Katsu
{
    std::ostream& operator<<(std::ostream& s, Token token)
    {
        s << token.type;
        if (const std::string* strval = std::get_if<std::string>(&token.value)) {
            s << "(value=\"" << *strval << "\")";
        } else if (const long long* intval = std::get_if<long long>(&token.value)) {
            s << "(value=" << *intval << ")";
        }
        return s;
    }

    void show_expr(const Expr& expr, int depth = 0)
    {
        for (int i = 0; i < depth; i++) {
            std::cout << "| ";
        }
        if (auto e = dynamic_cast<const UnaryOpExpr*>(&expr)) {
            std::cout << "unary-op " << std::get<std::string>(e->op.value) << "\n";
            show_expr(*e->arg, depth + 1);
        } else if (auto e = dynamic_cast<const BinaryOpExpr*>(&expr)) {
            std::cout << "binary-op " << std::get<std::string>(e->op.value) << "\n";
            show_expr(*e->left, depth + 1);
            show_expr(*e->right, depth + 1);
        } else if (auto e = dynamic_cast<const NameExpr*>(&expr)) {
            std::cout << "name " << std::get<std::string>(e->name.value) << "\n";
        } else if (auto e = dynamic_cast<const LiteralExpr*>(&expr)) {
            std::cout << "literal " << e->literal << "\n";
        } else if (auto e = dynamic_cast<const UnaryMessageExpr*>(&expr)) {
            std::cout << "unary-msg " << std::get<std::string>(e->message.value) << "\n";
            show_expr(*e->target, depth + 1);
        } else if (auto e = dynamic_cast<const NAryMessageExpr*>(&expr)) {
            std::cout << "nary-msg (target=" << (e->target.has_value() ? "yes" : "no") << ")";
            for (const Token& message : e->messages) {
                std::cout << " " << std::get<std::string>(message.value);
            }
            std::cout << "\n";
            if (e->target.has_value()) {
                show_expr(**e->target, depth + 1);
            }
            for (const std::unique_ptr<Expr>& arg : e->args) {
                show_expr(*arg, depth + 1);
            }
        } else if (auto e = dynamic_cast<const ParenExpr*>(&expr)) {
            std::cout << "paren\n";
            show_expr(*e->inner, depth + 1);
        } else if (auto e = dynamic_cast<const QuoteExpr*>(&expr)) {
            std::cout << "quote";
            for (const std::string& param : e->parameters) {
                std::cout << " " << param;
            }
            std::cout << "\n";
            show_expr(*e->body, depth + 1);
        } else if (auto e = dynamic_cast<const DataExpr*>(&expr)) {
            std::cout << "data\n";
            for (const std::unique_ptr<Expr>& component : e->components) {
                show_expr(*component, depth + 1);
            }
        } else if (auto e = dynamic_cast<const SequenceExpr*>(&expr)) {
            std::cout << "sequence\n";
            for (const std::unique_ptr<Expr>& component : e->components) {
                show_expr(*component, depth + 1);
            }
        } else if (auto e = dynamic_cast<const TupleExpr*>(&expr)) {
            std::cout << "tuple\n";
            for (const std::unique_ptr<Expr>& component : e->components) {
                show_expr(*component, depth + 1);
            }
        } else {
            std::cout << "(not sure how to print this)\n";
        }
    }

    void execute_source(const SourceFile source)
    {
        Lexer lexer(source);
        TokenStream stream(lexer);
        std::unique_ptr<PrattParser> parser = make_default_parser();

        while (!stream.current_has_type(TokenType::END)) {
            std::unique_ptr<Expr> top_level_expr =
                parser->parse(stream, 0 /* precedence */, true /* is_toplevel */);
            show_expr(*top_level_expr);
            // TODO: evaluate it

            // Ratchet past any semicolons and newlines, since the parser explicitly stops
            // when it sees either of these at the top level.
            while (stream.current_has_type(TokenType::SEMICOLON) ||
                   stream.current_has_type(TokenType::NEWLINE)) {
                stream.consume();
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

        SourceFile source{.path = std::make_shared<std::string>(filepath),
                          .source = std::make_shared<std::string>(std::move(file_contents))};

        execute_source(source);
    }
};

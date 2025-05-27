#include "katsu.h"

#include "compile.h"
#include "gc.h"
#include "lexer.h"
#include "parser.h"
#include "value.h"
#include "vm.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include <variant>

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

    class ExprPrinter : public ExprVisitor
    {
        int depth = 0;

    public:
        ExprPrinter(int _depth)
            : depth(_depth)
        {}

    private:
        void prefix()
        {
            for (int i = 0; i < depth; i++) {
                std::cout << "| ";
            }
        }

    public:
        void visit(UnaryOpExpr& e) override
        {
            prefix();
            std::cout << "unary-op " << std::get<std::string>(e.op.value) << "\n";
            ExprPrinter indented(depth + 1);
            e.arg->accept(indented);
        }
        void visit(BinaryOpExpr& e) override
        {
            prefix();
            std::cout << "binary-op " << std::get<std::string>(e.op.value) << "\n";
            ExprPrinter indented(depth + 1);
            e.left->accept(indented);
            e.right->accept(indented);
        }
        void visit(NameExpr& e) override
        {
            prefix();
            std::cout << "name " << std::get<std::string>(e.name.value) << "\n";
        }
        void visit(LiteralExpr& e) override
        {
            prefix();
            std::cout << "literal " << e.literal << "\n";
        }
        void visit(UnaryMessageExpr& e) override
        {
            prefix();
            std::cout << "unary-msg " << std::get<std::string>(e.message.value) << "\n";
            ExprPrinter indented(depth + 1);
            e.target->accept(indented);
        }
        void visit(NAryMessageExpr& e) override
        {
            prefix();
            std::cout << "nary-msg (target=" << (e.target.has_value() ? "yes" : "no") << ")";
            for (const Token& message : e.messages) {
                std::cout << " " << std::get<std::string>(message.value);
            }
            std::cout << "\n";
            ExprPrinter indented(depth + 1);
            if (e.target.has_value()) {
                (*e.target)->accept(indented);
            }
            for (const std::unique_ptr<Expr>& arg : e.args) {
                arg->accept(indented);
            }
        }
        void visit(ParenExpr& e) override
        {
            prefix();
            ExprPrinter indented(depth + 1);
            e.inner->accept(indented);
        }
        void visit(BlockExpr& e) override
        {
            prefix();
            std::cout << "block";
            for (const std::string& param : e.parameters) {
                std::cout << " " << param;
            }
            std::cout << "\n";
            ExprPrinter indented(depth + 1);
            e.body->accept(indented);
        }
        void visit(DataExpr& e) override
        {
            prefix();
            std::cout << "data\n";
            ExprPrinter indented(depth + 1);
            for (const std::unique_ptr<Expr>& component : e.components) {
                component->accept(indented);
            }
        }
        void visit(SequenceExpr& e) override
        {
            prefix();
            std::cout << "sequence\n";
            ExprPrinter indented(depth + 1);
            for (const std::unique_ptr<Expr>& component : e.components) {
                component->accept(indented);
            }
        }
        void visit(TupleExpr& e) override
        {
            prefix();
            std::cout << "tuple\n";
            ExprPrinter indented(depth + 1);
            for (const std::unique_ptr<Expr>& component : e.components) {
                component->accept(indented);
            }
        }
    };

    void execute_source(const SourceFile source)
    {
        Lexer lexer(source);
        TokenStream stream(lexer);
        std::unique_ptr<PrattParser> parser = make_default_parser();
        // 100 MiB GC-managed memory.
        GC gc(100 * 1024 * 1024);
        // 5 MiB call stack size.
        VM vm(gc, 5 * 1024 * 1024);
        OptionalRoot<Module> r_module(gc, nullptr);

        // Skip any leading semicolons / newlines to get to the meat.
        while (stream.current_has_type(TokenType::SEMICOLON) ||
               stream.current_has_type(TokenType::NEWLINE)) {
            stream.consume();
        }

        while (!stream.current_has_type(TokenType::END)) {
            std::unique_ptr<Expr> top_level_expr =
                parser->parse(stream, 0 /* precedence */, true /* is_toplevel */);

            // ExprPrinter printer(0);
            // top_level_expr->accept(printer);

            std::vector<std::unique_ptr<Expr>> top_level_exprs;
            top_level_exprs.emplace_back(std::move(top_level_expr));
            Root<Code> code(gc, compile_module(gc, r_module, top_level_exprs));
            ValueRoot result = vm.eval_toplevel(code);
            std::cout << "EVALUATION RESULT: tag=" << tag_str(result->tag())
                      << ", raw=" << result->raw_value() << "(0x" << std::hex << result->raw_value()
                      << std::dec << ")\n";

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

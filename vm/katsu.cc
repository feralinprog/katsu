#include "katsu.h"

#include "builtin.h"
#include "compile.h"
#include "gc.h"
#include "lexer.h"
#include "parser.h"
#include "value.h"
#include "value_utils.h"
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
            std::cout << "():\n";
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

    SourceFile load_file(const std::string& filepath)
    {
        std::ifstream file_stream;
        // Raise exceptions on logical error or read/write error.
        file_stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        file_stream.open(filepath.c_str());

        std::stringstream str_stream;
        str_stream << file_stream.rdbuf();
        std::string file_contents = str_stream.str();

        return SourceFile{.path = std::make_shared<std::string>(filepath),
                          .source = std::make_shared<std::string>(std::move(file_contents))};
    }

    Value run_source(const SourceFile source, const std::string& module_name, VM& vm)
    {
        Lexer lexer(source);
        TokenStream stream(lexer);
        std::unique_ptr<PrattParser> parser = make_default_parser();

        // Create a separate module for the source we're executing.
        Root<Assoc> r_module(vm.gc, make_assoc(vm.gc, /* capacity */ 0));
        {
            Root<Assoc> r_modules(vm.gc, vm.modules());
            ValueRoot r_name(vm.gc, Value::object(make_string(vm.gc, module_name)));
            ValueRoot rv_module(vm.gc, r_module.value());
            append(vm.gc, r_modules, r_name, rv_module);
            vm.set_modules(*r_modules);
        }

        Root<Vector> r_imports(vm.gc, make_vector(vm.gc, /* capacity */ 0));
        use_default_imports(vm, r_imports);

        // std::cout << "=== INITIAL MODULE STATE ===\n";
        // pprint(r_module.value());

        // Skip any leading semicolons / newlines to get to the meat.
        while (stream.current_has_type(TokenType::SEMICOLON) ||
               stream.current_has_type(TokenType::NEWLINE)) {
            stream.consume();
        }

        Value result = Value::null();
        while (!stream.current_has_type(TokenType::END)) {
            std::unique_ptr<Expr> top_level_expr =
                parser->parse(stream, 0 /* precedence */, true /* is_toplevel */);

            // std::cout << "=== MODULE STATE ===\n";
            // pprint(r_module.value());
            // std::cout << "=== PARSED ===\n";
            // ExprPrinter printer(0);
            // top_level_expr->accept(printer);

            std::vector<std::unique_ptr<Expr>> top_level_exprs;
            top_level_exprs.emplace_back(std::move(top_level_expr));
            // std::cout << "=== COMPILING INTO MODULE ===\n";
            Root<Code> code(vm.gc,
                            compile_into_module(vm,
                                                r_module,
                                                r_imports,
                                                top_level_exprs[0]->span,
                                                top_level_exprs));
            // std::cout << "=== GENERATED CODE ===\n";
            // pprint(code.value());
            // std::cout << "=== EVALUATING ===\n";
            result = vm.eval_toplevel(code);
            // std::cout << "=== EVALUATION RESULT ===\n";
            // std::cout << "-> ";
            // pprint(result);

            // std::cout << "\n\n\n";

            // Ratchet past any semicolons and newlines, since the parser explicitly stops
            // when it sees either of these at the top level.
            while (stream.current_has_type(TokenType::SEMICOLON) ||
                   stream.current_has_type(TokenType::NEWLINE)) {
                stream.consume();
            }
        }
        return result;
    }

    Value bootstrap_and_run_source(const SourceFile source, const std::string& module_name, GC& gc,
                                   uint64_t call_stack_size)
    {
        VM vm(gc, call_stack_size);

        // Establish builtins in the core.builtin module.
        {
            Root<Assoc> r_core_builtin_default(gc, make_assoc(gc, /* capacity */ 0));
            Root<Assoc> r_core_builtin_misc(gc, make_assoc(gc, /* capacity */ 0));
            register_builtins(vm, r_core_builtin_default, r_core_builtin_misc);

            Root<Assoc> r_modules(vm.gc, vm.modules());
            {
                ValueRoot r_name(vm.gc, Value::object(make_string(vm.gc, "core.builtin.default")));
                ValueRoot rv_core_builtin(gc, r_core_builtin_default.value());
                append(vm.gc, r_modules, r_name, rv_core_builtin);
            }
            {
                ValueRoot r_name(vm.gc, Value::object(make_string(vm.gc, "core.builtin.misc")));
                ValueRoot rv_core_builtin(gc, r_core_builtin_misc.value());
                append(vm.gc, r_modules, r_name, rv_core_builtin);
            }
            vm.set_modules(*r_modules);
        }

        // Add a module with some constants for bootstrap files to use in order to load the
        // requested "user" source.
        {
            Root<Assoc> r_core_bootstrap_load(gc, make_assoc(gc, /* capacity */ 3));
            {
                ValueRoot r_name(vm.gc, Value::object(make_string(vm.gc, "user-module-name")));
                ValueRoot r_value(vm.gc, Value::object(make_string(vm.gc, module_name)));
                append(vm.gc, r_core_bootstrap_load, r_name, r_value);
            }
            {
                ValueRoot r_name(vm.gc, Value::object(make_string(vm.gc, "user-source-path")));
                ValueRoot r_value(vm.gc, Value::object(make_string(vm.gc, *source.path)));
                append(vm.gc, r_core_bootstrap_load, r_name, r_value);
            }
            {
                ValueRoot r_name(vm.gc, Value::object(make_string(vm.gc, "user-source-contents")));
                ValueRoot r_value(vm.gc, Value::object(make_string(vm.gc, *source.source)));
                append(vm.gc, r_core_bootstrap_load, r_name, r_value);
            }

            Root<Assoc> r_modules(vm.gc, vm.modules());
            {
                ValueRoot r_name(vm.gc, Value::object(make_string(vm.gc, "core.bootstrap.load")));
                ValueRoot rv_core_builtin(gc, r_core_bootstrap_load.value());
                append(vm.gc, r_modules, r_name, rv_core_builtin);
            }
            vm.set_modules(*r_modules);
        }

        // Run bootstrap files, which should run the user source.
        return run_source(load_file("src/core/core.katsu"), "core", vm);
    }

    void bootstrap_and_run_file(const std::string& filepath, const std::string& module_name)
    {
        SourceFile source = load_file(filepath);

        // 100 MiB GC-managed memory.
        GC gc(100 * 1024 * 1024);
        // 100 KiB call stack size.
        bootstrap_and_run_source(source, module_name, gc, 100 * 1024);
    }
};

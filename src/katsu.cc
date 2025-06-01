#include "katsu.h"

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

    void pprint(std::vector<Object*>& objects_seen, Value value, int depth,
                const std::string& prefix, bool initial_indent = true)
    {
        auto indent = [](int depth) {
            for (int i = 0; i < depth; i++) {
                std::cout << "  ";
            }
        };
        auto pchild = [&objects_seen, depth](Value child,
                                             const std::string& prefix = "",
                                             bool initial_indent = true,
                                             int extra_depth = 1) {
            pprint(objects_seen, child, depth + extra_depth, prefix, initial_indent);
        };
        auto pnative = [&indent, depth]() -> std::ostream& {
            indent(depth + 1);
            return std::cout;
        };

        if (initial_indent) {
            indent(depth);
        }
        std::cout << prefix;

        if (value.is_fixnum()) {
            std::cout << "fixnum " << value.fixnum() << "\n";
        } else if (value.is_float()) {
            std::cout << "float " << value._float() << "\n";
        } else if (value.is_bool()) {
            std::cout << "bool " << value._bool() << "\n";
        } else if (value.is_null()) {
            std::cout << "null\n";
        } else if (value.is_object()) {
            Object* obj = value.object();
            for (size_t i = 0; i < objects_seen.size(); i++) {
                if (objects_seen[i] == obj) {
                    std::cout << "^up " << (objects_seen.size() - i) << "\n";
                    return;
                }
            }
            objects_seen.push_back(obj);

            if (value.is_obj_ref()) {
                Ref* o = value.obj_ref();
                std::cout << "*ref:\n";
                pchild(o->v_ref);
            } else if (value.is_obj_tuple()) {
                Tuple* o = value.obj_tuple();
                std::cout << "*tuple: length=" << o->length << " (\n";
                for (uint64_t i = 0; i < o->length; i++) {
                    std::stringstream ss;
                    ss << i << " = ";
                    pchild(o->components()[i], ss.str());
                }
                indent(depth);
                std::cout << ")\n";
            } else if (value.is_obj_array()) {
                Array* o = value.obj_array();
                std::cout << "*array: length=" << o->length << "\n";
                for (uint64_t i = 0; i < o->length; i++) {
                    std::stringstream ss;
                    ss << i << " = ";
                    pchild(o->components()[i], ss.str());
                }
            } else if (value.is_obj_vector()) {
                Vector* o = value.obj_vector();
                std::cout << "*vector: length=" << o->length << " [\n";
                pchild(o->v_array, "v_array = ");
                indent(depth);
                std::cout << "]\n";
            } else if (value.is_obj_module()) {
                Module* o = value.obj_module();
                std::cout << "*module: length=" << o->length << "\n";
                pchild(o->v_array, "v_array = ");
                pchild(o->v_base, "v_base = ");
            } else if (value.is_obj_string()) {
                String* o = value.obj_string();
                std::cout << "*string: \"";
                for (uint8_t* c = o->contents(); c < o->contents() + o->length; c++) {
                    std::cout << (char)*c;
                }
                std::cout << "\"\n";
            } else if (value.is_obj_code()) {
                Code* o = value.obj_code();
                std::cout << "*code\n";
                // TODO: add back if there's some way to fold. By default, too noisy.
                // pchild(o->v_module, "v_module = ");
                pnative() << "num_regs = " << o->num_regs << "\n";
                pnative() << "num_data = " << o->num_data << "\n";
                pchild(o->v_upreg_map, "v_upreg_map = ");
                // Special pretty-printing for code: decode the instructions and arguments!
                // TODO: better error handling in case of any nonexpected values.
                pnative() << "bytecode:\n";
                uint32_t arg_spot = 0;
                Array* args = o->v_args.obj_array();
                for (uint32_t inst_spot = 0; inst_spot < o->v_insts.obj_array()->length;
                     inst_spot++) {
                    pnative() << "[" << inst_spot << "]: ";
                    int64_t inst = o->v_insts.obj_array()->components()[inst_spot].fixnum();
                    switch (inst) {
                        case LOAD_REG: {
                            std::cout << "load_reg @" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case STORE_REG: {
                            std::cout << "store_reg @" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case LOAD_REF: {
                            std::cout << "load_ref @" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case STORE_REF: {
                            std::cout << "store_ref @" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case LOAD_VALUE: {
                            std::cout << "load_value: ";
                            pchild(args->components()[arg_spot++],
                                   "",
                                   /* initial_indent */ false,
                                   /* extra_depth */ +1);
                            break;
                        }
                        case INIT_REF: {
                            std::cout << "init_ref @" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case LOAD_MODULE: {
                            std::cout << "load_module ";
                            pchild(args->components()[arg_spot++],
                                   "",
                                   /* initial_indent */ false,
                                   /* extra_depth */ +1);
                            break;
                        }
                        case STORE_MODULE: {
                            std::cout << "store_module ";
                            pchild(args->components()[arg_spot++],
                                   "",
                                   /* initial_indent */ false,
                                   /* extra_depth */ +1);
                            break;
                        }
                        case INVOKE: {
                            std::cout << "invoke #" << args->components()[arg_spot + 1].fixnum()
                                      << " ";
                            pchild(args->components()[arg_spot],
                                   "",
                                   /* initial_indent */ false,
                                   /* extra_depth */ +1);
                            arg_spot += 2;
                            break;
                        }
                        case DROP: {
                            std::cout << "drop\n";
                            break;
                        }
                        case MAKE_TUPLE: {
                            std::cout << "make-tuple #" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case MAKE_VECTOR: {
                            std::cout << "make-vector #" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case MAKE_CLOSURE: {
                            std::cout << "make-closure: ";
                            pchild(args->components()[arg_spot++],
                                   "",
                                   /* initial_indent */ false,
                                   /* extra_depth */ +1);
                            break;
                        }
                        default: {
                            std::cout << "??? (inst=" << inst << ")\n";
                            break;
                        }
                    }
                }
                // pchild(o->v_insts, "v_insts = ");
                // pchild(o->v_args, "v_args = ");
            } else if (value.is_obj_closure()) {
                Closure* o = value.obj_closure();
                std::cout << "*closure\n";
                pchild(o->v_code, "v_code = ");
                pchild(o->v_upregs, "v_upregs = ");
            } else if (value.is_obj_method()) {
                Method* o = value.obj_method();
                std::cout << "*method\n";
                pchild(o->v_param_matchers, "v_param_matchers = ");
                pchild(o->v_return_type, "v_return_type = ");
                pchild(o->v_code, "v_code = ");
                pchild(o->v_attributes, "v_attributes = ");
                pnative() << "native_handler = " << (void*)o->native_handler << "\n";
            } else if (value.is_obj_multimethod()) {
                MultiMethod* o = value.obj_multimethod();
                std::cout << "*multimethod\n";
                pchild(o->v_name, "v_name = ");
                pchild(o->v_methods, "v_methods = ");
                pchild(o->v_attributes, "v_attributes = ");
            } else if (value.is_obj_type()) {
                Type* o = value.obj_type();
                std::cout << "*type\n";
                pchild(o->v_name, "v_name = ");
                pchild(o->v_bases, "v_bases = ");
                pnative() << "sealed = " << o->sealed << "\n";
                pchild(o->v_linearization, "v_linearization = ");
                pchild(o->v_subtypes, "v_subtypes = ");
                pnative() << "kind = ";
                switch (o->kind) {
                    case Type::Kind::MIXIN: std::cout << "mixin\n"; break;
                    case Type::Kind::DATACLASS: std::cout << "dataclass\n"; break;
                    default: std::cout << "??? (raw=" << static_cast<int>(o->kind) << "\n"; break;
                }
                pchild(o->v_slots, "v_slots = ");
            } else if (value.is_obj_instance()) {
                DataclassInstance* o = value.obj_instance();
                std::cout << "*instance\n";
                pchild(o->v_type, "v_type = ");
                pnative() << "slots: (TODO)\n";
            } else {
                std::cout << "object: ??? (object tag = " << static_cast<int>(value.object()->tag())
                          << ")\n";
            }

            objects_seen.pop_back();
        } else {
            std::cout << "??? (tag = " << static_cast<int>(value.tag())
                      << ", raw value = " << value.raw_value() << "(0x" << std::hex
                      << value.raw_value() << std::dec << "))\n";
        }
    }

    void pprint(Value value, bool initial_indent = true, int depth = 0)
    {
        std::vector<Object*> objects_seen{};
        pprint(objects_seen, value, depth, "", initial_indent);
    }

    // TODO: param matchers / return type
    void add_native(GC& gc, Root<Module>& r_module, const std::string& name, NativeHandler handler)
    {
        // TODO: check if name exists already in module!

        Root<String> r_name(gc, make_string(gc, name));

        Root<Vector> r_methods(gc, make_vector(gc, 1));
        {
            ValueRoot r_param_matchers(gc, Value::null()); // TODO
            OptionalRoot<Type> r_return_type(gc, nullptr); // TODO
            OptionalRoot<Code> r_code(gc, nullptr);        // native!
            Root<Vector> r_attributes(gc, make_vector(gc, 0));

            ValueRoot r_method(gc,
                               Value::object(make_method(gc,
                                                         r_param_matchers,
                                                         r_return_type,
                                                         r_code,
                                                         r_attributes,
                                                         handler)));
            append(gc, r_methods, r_method);
        }
        Root<Vector> r_attributes(gc, make_vector(gc, 0));

        ValueRoot r_multimethod(
            gc,
            Value::object(make_multimethod(gc, r_name, r_methods, r_attributes)));
        append(gc, r_module, r_name, r_multimethod);
    }

    Value plus(VM& vm, int64_t nargs, Value* args)
    {
        return Value::fixnum(args[0].fixnum() + args[1].fixnum());
    }

    Value print(VM& vm, int64_t nargs, Value* args)
    {
        pprint(args[1]);
        return Value::null();
    }

    void execute_source(const SourceFile source)
    {
        Lexer lexer(source);
        TokenStream stream(lexer);
        std::unique_ptr<PrattParser> parser = make_default_parser();
        // 100 MiB GC-managed memory.
        GC gc(100 * 1024 * 1024);
        // 5 MiB call stack size.
        VM vm(gc, 5 * 1024 * 1024);
        OptionalRoot<Module> r_module_base(gc, nullptr);
        Root<Module> r_module(gc, make_module(gc, r_module_base, /* capacity */ 0));

        add_native(gc, r_module, "+:", &plus);
        add_native(gc, r_module, "print:", &print);

        // std::cout << "=== INITIAL MODULE STATE ===\n";
        // pprint(r_module.value());

        // Skip any leading semicolons / newlines to get to the meat.
        while (stream.current_has_type(TokenType::SEMICOLON) ||
               stream.current_has_type(TokenType::NEWLINE)) {
            stream.consume();
        }

        while (!stream.current_has_type(TokenType::END)) {
            std::unique_ptr<Expr> top_level_expr =
                parser->parse(stream, 0 /* precedence */, true /* is_toplevel */);

            std::cout << "=== MODULE STATE ===\n";
            pprint(r_module.value());
            std::cout << "=== PARSED ===\n";
            ExprPrinter printer(0);
            top_level_expr->accept(printer);

            std::vector<std::unique_ptr<Expr>> top_level_exprs;
            top_level_exprs.emplace_back(std::move(top_level_expr));
            std::cout << "=== COMPILING INTO MODULE ===\n";
            Root<Code> code(gc, compile_into_module(gc, r_module, top_level_exprs));
            std::cout << "=== GENERATED CODE ===\n";
            pprint(code.value());
            std::cout << "=== EVALUATING ===\n";
            Value result = vm.eval_toplevel(code);
            std::cout << "=== EVALUATION RESULT ===\n";
            pprint(result);

            std::cout << "\n\n\n";

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

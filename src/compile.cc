#include "compile.h"

#include "value_utils.h"
#include "vm.h"
#include <cstring>
#include <map>
#include <vector>

namespace Katsu
{
    struct Scope
    {
        struct Binding
        {
            const std::string name;
            bool _mutable;
            int spot;
        };

        std::map<std::string, Binding> bindings;
        std::unique_ptr<Scope> base;
    };

    class Compiler : public ExprVisitor
    {
    public:
        Compiler(GC& _gc, Module* base)
            : gc(_gc)
            , r_base(gc, Value::object(base))
            , r_module(gc, Value::object(make_module(gc, /* r_base */ r_base, /* capacity */ 0)))
            , scope{.bindings = {}, .base = nullptr}
        {}

        ~Compiler() = default;

        void visit(UnaryOpExpr& expr) override
        {
            Root r_name(gc,
                        Value::object(make_string(this->gc, std::get<std::string>(expr.op.value))));
            if (!module_lookup(r_name->obj_string())) {
                // TODO: raise a compilation error and expose to katsu, ideally.
                // should show the source of the issue, i.e. expr.op.span
                throw std::invalid_argument("unknown module name, cannot invoke unaryop");
            }
            expr.arg->accept(*this);
            // INVOKE: <name>, <num args>
            emit_op(OpCode::INVOKE);
            emit_arg(*r_name);
            emit_arg(Value::fixnum(1));
        }

        void visit(BinaryOpExpr& expr) override
        {
            Root r_name(gc,
                        Value::object(make_string(this->gc, std::get<std::string>(expr.op.value))));
            if (!module_lookup(r_name->obj_string())) {
                // TODO: raise a compilation error and expose to katsu, ideally.
                // should show the source of the issue, i.e. expr.op.span
                throw std::invalid_argument("unknown module name, cannot invoke binaryop");
            }
            expr.left->accept(*this);
            expr.right->accept(*this);
            // INVOKE: <name>, <num args>
            emit_op(OpCode::INVOKE);
            emit_arg(*r_name);
            emit_arg(Value::fixnum(2));
        }

        void visit(NameExpr& expr) override
        {
            // TODO: somehow distinguish between a plain `x` being
            // * `<default-receiver> x`, i.e. invoking `x` on the default receiver; or
            // * `x`, i.e. load the local or module value `x`
            // maybe if the name is in module scope but is a multimethod..?
            // but then compilation would vary depending on whether module value is multimethod or
            // something else. Currently this always assumes it's a local/module load, not a unary
            // message.
            Root r_name(
                gc,
                Value::object(make_string(this->gc, std::get<std::string>(expr.name.value))));
            const Scope::Binding* maybe_local = this->scope_lookup(r_name->obj_string());
            if (maybe_local) {
                const Scope::Binding& local = *maybe_local;
                if (local._mutable) {
                    // LOAD_REF: <local index>
                    emit_op(OpCode::LOAD_REF);
                    emit_arg(Value::fixnum(local.spot));
                } else {
                    // LOAD_REG: <local index>
                    emit_op(OpCode::LOAD_REG);
                    emit_arg(Value::fixnum(local.spot));
                }
            } else if (module_lookup(r_name->obj_string())) {
                // LOAD_MODULE: <name>
                emit_op(OpCode::LOAD_MODULE);
                emit_arg(*r_name);
            } else {
                // TODO: raise a compilation error and expose to katsu, ideally.
                // should show the source of the issue, i.e. expr.name.span
                throw std::invalid_argument("unknown local or module name, cannot load name");
            }
        }

        void visit(LiteralExpr& expr) override
        {
            switch (expr.literal.type) {
                case TokenType::INTEGER: {
                    // LOAD_VALUE: <value>
                    this->emit_op(OpCode::LOAD_VALUE);
                    this->emit_arg(Value::fixnum(std::get<long long>(expr.literal.value)));
                    break;
                }
                case TokenType::STRING: {
                    // LOAD_VALUE: <value>
                    this->emit_op(OpCode::LOAD_VALUE);
                    this->emit_arg(
                        Value::object(make_string(gc, std::get<std::string>(expr.literal.value))));
                    break;
                }
                case TokenType::SYMBOL: {
                    throw std::logic_error("visit(LiteralExpr) SYMBOL handling not implemented");
                    break;
                }
                default: throw std::logic_error("unexpected token type in visit(LiteralExpr)");
            }
        }

        void visit(UnaryMessageExpr& expr) override
        {
            Root r_name(
                gc,
                Value::object(make_string(this->gc, std::get<std::string>(expr.message.value))));
            if (!this->module_lookup(r_name->obj_string())) {
                // TODO: raise a compilation error and expose to katsu, ideally.
                // should show the source of the issue, i.e. expr.message.span
                throw std::invalid_argument("unknown module name, cannot invoke unary-message");
            }
            expr.target->accept(*this);
            // INVOKE: <name>, <num args>
            emit_op(OpCode::INVOKE);
            emit_arg(*r_name);
            emit_arg(Value::fixnum(1));
        }

        void visit(NAryMessageExpr& expr) override
        {
            String* combined_name;
            {
                size_t total_len = 0;
                for (const Token& token_part : expr.messages) {
                    const std::string& part = std::get<std::string>(token_part.value);
                    total_len += part.size() + 1 /* for `:` */;
                }
                combined_name = gc.alloc<String>(total_len);
                combined_name->length = total_len;
                size_t offset = 0;
                for (const Token& token_part : expr.messages) {
                    const std::string& part = std::get<std::string>(token_part.value);
                    const size_t len = part.size();
                    memcpy(combined_name->contents() + offset, part.c_str(), len);
                    offset += len;
                    combined_name->contents()[offset++] = ':';
                }
            }
            // Establish a root in case the recursive `accept` calls perform any more
            // allocations.
            Root r_name(gc, Value::object(combined_name));

            // Ensure the message is:
            // * `<name>:` where <name> is a local mutable variable (in which case expr.target
            //   must be nullopt),
            // * or else in the module under construction.
            if (expr.messages.size() == 1) {
                // Check for local mutable variables.
                const std::string& name = std::get<std::string>(expr.messages[0].value);
                const Scope::Binding* maybe_local = this->scope_lookup(name);
                if (maybe_local) {
                    const Scope::Binding& local = *maybe_local;
                    if (local._mutable) {
                        // Ensure there's no target provided.
                        if (expr.target) {
                            // TODO: raise compilation error to katsu instead.
                            throw std::invalid_argument(
                                "setting mutable variable requires no target");
                        }
                        expr.args[0]->accept(*this);
                        // STORE_REF: <local index>
                        emit_op(OpCode::STORE_REF);
                        emit_arg(Value::fixnum(local.spot));
                        return;
                    }
                    // TODO: maybe if module lookup fails after this, error message should
                    // include something like "there's a local of this name, did you mean to
                    // make it mutable?".
                }
            }
            if (!this->module_lookup(r_name->obj_string())) {
                // TODO: raise a compilation error and expose to katsu, ideally.
                // should show the source of the issue, i.e. expr.op.span
                throw std::invalid_argument("unknown module name (or mutable local variable "
                                            "name), cannot invoke n-ary-message");
            }

            if (expr.target) {
                (*expr.target)->accept(*this);
            } else {
                // Load the default receiver, which is always register 0.
                // LOAD_REG: <local index>
                emit_op(OpCode::LOAD_REG);
                emit_arg(Value::fixnum(0));
            }
            for (const std::unique_ptr<Expr>& arg : expr.args) {
                arg->accept(*this);
            }
            // INVOKE: <name>, <num args>
            emit_op(OpCode::INVOKE);
            emit_arg(*r_name);
            emit_arg(Value::fixnum(expr.args.size()));
        }

        void visit(ParenExpr& expr) override
        {
            expr.inner->accept(*this);
        }

        void visit(BlockExpr& expr) override
        {
            // TODO: use make_code() instead.
            Code* code = this->gc.alloc<Code>();
            code->v_module = *this->r_module;
            // TODO: set up the rest
            // Value v_module;    // Module
            // Value v_num_regs;  // fixnum
            // Value v_num_data;  // fixnum
            // Value v_upreg_map; // Null for methods; Vector (of fixnum) for closures
            // // TODO: byte array inline?
            // Value v_insts; // Vector of fixnums
            // // TODO: arg array inline?
            // Value v_args; // Vector (of arbitrary values)
            Root r_code(this->gc, Value::object(code));
            if (expr.parameters.empty()) {
                // It still has an implicit parameter: `it`.
            }
            throw std::logic_error("not implemented yet");
        }

        void visit(DataExpr& expr) override
        {
            for (const std::unique_ptr<Expr>& component : expr.components) {
                component->accept(*this);
            }
            // MAKE_VECTOR: <num components>
            emit_op(OpCode::MAKE_VECTOR);
            emit_arg(Value::fixnum(expr.components.size()));
        }

        void visit(SequenceExpr& expr) override
        {
            if (expr.components.empty()) {
                // Empty sequence -> just load null.
                // LOAD_VALUE: <value>
                emit_op(OpCode::LOAD_VALUE);
                emit_arg(Value::null());
                return;
            }

            // Drop all but the last component's result.
            for (size_t i = 0; i < expr.components.size(); i++) {
                bool last = i == expr.components.size() - 1;
                expr.components[i]->accept(*this);
                if (!last) {
                    emit_op(OpCode::DROP);
                }
            }
        }

        void visit(TupleExpr& expr) override
        {
            for (const std::unique_ptr<Expr>& component : expr.components) {
                component->accept(*this);
            }
            // MAKE_TUPLE: <num components>
            emit_op(OpCode::MAKE_TUPLE);
            emit_arg(Value::fixnum(expr.components.size()));
        }

    private:
        void emit_op(OpCode op) {}
        void emit_arg(Value v_arg) {}

        Scope::Binding* scope_lookup(const std::string& name)
        {
            Scope* cur = &this->scope;
            while (cur) {
                auto it = cur->bindings.find(name);
                if (it != cur->bindings.end()) {
                    return &it->second;
                }
                cur = cur->base.get();
            }
            return nullptr;
        }
        Scope::Binding* scope_lookup(String* name)
        {
            uint64_t name_len = name->length;
            // TODO: check against size_t?

            Scope* cur = &this->scope;
            while (cur) {
                for (auto& [binding_name, binding] : cur->bindings) {
                    if (name_len != binding_name.size()) {
                        continue;
                    }
                    if (memcmp(binding_name.c_str(), name->contents(), name_len) != 0) {
                        continue;
                    }
                    return &binding;
                }
                cur = cur->base.get();
            }
            return nullptr;
        }

        Value* module_lookup(String* name)
        {
            return Katsu::module_lookup(this->r_module->obj_module(), name);
        }

        GC& gc;
        Root r_base;   // Module
        Root r_module; // Module
        Scope scope;
    };

    Module* compile_module(GC& gc, Module* base,
                           std::vector<std::unique_ptr<Expr>>& module_top_level_exprs)
    {
        Compiler compiler(gc, base);
        // TODO: for future -- first find all multimethod definitions, add them to scope (with
        // zero methods defined), and then go and compile everything.
        module_top_level_expr.accept(compiler);
    }
};

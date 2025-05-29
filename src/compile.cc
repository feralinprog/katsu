#include "compile.h"

#include "value_utils.h"
#include "vm.h"
#include <cstring>
#include <map>
#include <vector>

#include <iostream>
#include <sstream>

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

    Scope::Binding* scope_lookup(Scope& scope, const std::string& name)
    {
        Scope* cur = &scope;
        while (cur) {
            auto it = cur->bindings.find(name);
            if (it != cur->bindings.end()) {
                return &it->second;
            }
            cur = cur->base.get();
        }
        return nullptr;
    }
    Scope::Binding* scope_lookup(Scope& scope, String* name)
    {
        // TODO: check name->length against size_t?
        Scope* cur = &scope;
        while (cur) {
            for (auto& [binding_name, binding] : cur->bindings) {
                if (!string_eq(name, binding_name)) {
                    continue;
                }
                return &binding;
            }
            cur = cur->base.get();
        }
        return nullptr;
    }

    void compile_expr(GC& gc, Root<Code>& r_code, Scope& scope, Expr& _expr)
    {
        auto module = [&r_code]() -> Module* { return r_code->v_module.obj_module(); };

        auto emit_op = [](OpCode op) -> void {};
        auto emit_arg = [](Value v_arg) -> void {};

        if (UnaryOpExpr* expr = dynamic_cast<UnaryOpExpr*>(&_expr)) {
            const std::string& op_name = std::get<std::string>(expr->op.value);
            Root<String> r_name(gc, make_string(gc, op_name));
            if (!module_lookup(module(), *r_name)) {
                // TODO: raise a compilation error and expose to katsu, ideally.
                // should show the source of the issue, i.e. expr.op.span
                throw std::invalid_argument("unknown module name, cannot invoke unaryop");
            }
            compile_expr(gc, r_code, scope, *expr->arg);
            // INVOKE: <name>, <num args>
            emit_op(OpCode::INVOKE);
            emit_arg(r_name.value());
            emit_arg(Value::fixnum(1));
        } else if (BinaryOpExpr* expr = dynamic_cast<BinaryOpExpr*>(&_expr)) {
            const std::string& op_name = std::get<std::string>(expr->op.value);
            Root<String> r_name(gc, make_string(gc, op_name));
            if (!module_lookup(module(), *r_name)) {
                // TODO: raise a compilation error and expose to katsu, ideally.
                // should show the source of the issue, i.e. expr.op.span
                throw std::invalid_argument("unknown module name, cannot invoke binaryop");
            }
            compile_expr(gc, r_code, scope, *expr->left);
            compile_expr(gc, r_code, scope, *expr->right);
            // INVOKE: <name>, <num args>
            emit_op(OpCode::INVOKE);
            emit_arg(r_name.value());
            emit_arg(Value::fixnum(2));
        } else if (NameExpr* expr = dynamic_cast<NameExpr*>(&_expr)) {
            // TODO: somehow distinguish between a plain `x` being
            // * `<default-receiver> x`, i.e. invoking `x` on the default receiver; or
            // * `x`, i.e. load the local or module value `x`
            // maybe if the name is in module scope but is a multimethod..?
            // but then compilation would vary depending on whether module value is multimethod or
            // something else. Currently this always assumes it's a local/module load, not a unary
            // message.
            const std::string& name = std::get<std::string>(expr->name.value);
            Root<String> r_name(gc, make_string(gc, name));
            const Scope::Binding* maybe_local = scope_lookup(scope, *r_name);
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
            } else if (module_lookup(module(), *r_name)) {
                // LOAD_MODULE: <name>
                emit_op(OpCode::LOAD_MODULE);
                emit_arg(r_name.value());
            } else {
                // TODO: raise a compilation error and expose to katsu, ideally.
                // should show the source of the issue, i.e. expr.name.span
                throw std::invalid_argument("unknown local or module name, cannot load name");
            }
        } else if (LiteralExpr* expr = dynamic_cast<LiteralExpr*>(&_expr)) {
            switch (expr->literal.type) {
                case TokenType::INTEGER: {
                    // LOAD_VALUE: <value>
                    emit_op(OpCode::LOAD_VALUE);
                    emit_arg(Value::fixnum(std::get<long long>(expr->literal.value)));
                    break;
                }
                case TokenType::STRING: {
                    // LOAD_VALUE: <value>
                    emit_op(OpCode::LOAD_VALUE);
                    emit_arg(
                        Value::object(make_string(gc, std::get<std::string>(expr->literal.value))));
                    break;
                }
                case TokenType::SYMBOL: {
                    throw std::logic_error("visit(LiteralExpr) SYMBOL handling not implemented");
                    break;
                }
                default: throw std::logic_error("unexpected token type in visit(LiteralExpr)");
            }
        } else if (UnaryMessageExpr* expr = dynamic_cast<UnaryMessageExpr*>(&_expr)) {
            const std::string& name = std::get<std::string>(expr->message.value);
            Root<String> r_name(gc, make_string(gc, name));
            if (!module_lookup(module(), *r_name)) {
                // TODO: raise a compilation error and expose to katsu, ideally.
                // should show the source of the issue, i.e. expr.message.span
                throw std::invalid_argument("unknown module name, cannot invoke unary-message");
            }
            compile_expr(gc, r_code, scope, *expr->target);
            // INVOKE: <name>, <num args>
            emit_op(OpCode::INVOKE);
            emit_arg(r_name.value());
            emit_arg(Value::fixnum(1));
        } else if (NAryMessageExpr* expr = dynamic_cast<NAryMessageExpr*>(&_expr)) {
            String* combined_name;
            {
                size_t total_len = 0;
                for (const Token& token_part : expr->messages) {
                    const std::string& part = std::get<std::string>(token_part.value);
                    total_len += part.size() + 1 /* for `:` */;
                }
                combined_name = gc.alloc<String>(total_len);
                combined_name->length = total_len;
                size_t offset = 0;
                for (const Token& token_part : expr->messages) {
                    const std::string& part = std::get<std::string>(token_part.value);
                    const size_t len = part.size();
                    memcpy(combined_name->contents() + offset, part.c_str(), len);
                    offset += len;
                    combined_name->contents()[offset++] = ':';
                }
            }
            // Establish a root in case the recursive `compile_expr` calls perform any more
            // allocations.
            Root<String> r_name(gc, std::move(combined_name));

            // Ensure the message is:
            // * `<name>:` where <name> is a local mutable variable (in which case expr.target
            //   must be nullopt),
            // * or else in the module under construction.
            if (expr->messages.size() == 1) {
                // Check for local mutable variables.
                const std::string& name = std::get<std::string>(expr->messages[0].value);
                const Scope::Binding* maybe_local = scope_lookup(scope, name);
                if (maybe_local) {
                    const Scope::Binding& local = *maybe_local;
                    if (local._mutable) {
                        // Ensure there's no target provided.
                        if (expr->target) {
                            // TODO: raise compilation error to katsu instead.
                            throw std::invalid_argument(
                                "setting mutable variable requires no target");
                        }
                        compile_expr(gc, r_code, scope, *expr->args[0]);
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
            if (!module_lookup(module(), *r_name)) {
                // TODO: raise a compilation error and expose to katsu, ideally.
                // should show the source of the issue, i.e. expr.op.span
                throw std::invalid_argument("unknown module name (or mutable local variable "
                                            "name), cannot invoke n-ary-message");
            }

            if (expr->target) {
                compile_expr(gc, r_code, scope, *expr->target.value());
            } else {
                // Load the default receiver, which is always register 0.
                // LOAD_REG: <local index>
                emit_op(OpCode::LOAD_REG);
                emit_arg(Value::fixnum(0));
            }
            for (const std::unique_ptr<Expr>& arg : expr->args) {
                compile_expr(gc, r_code, scope, *arg);
            }
            // INVOKE: <name>, <num args>
            emit_op(OpCode::INVOKE);
            emit_arg(r_name.value());
            emit_arg(Value::fixnum(expr->args.size()));
        } else if (ParenExpr* expr = dynamic_cast<ParenExpr*>(&_expr)) {
            compile_expr(gc, r_code, scope, *expr->inner);
        } else if (BlockExpr* expr = dynamic_cast<BlockExpr*>(&_expr)) {
            // TODO: use make_code() instead.
            Code* block_code = gc.alloc<Code>();
            block_code->v_module = r_code->v_module;
            // TODO: set up the rest
            // Value v_module;    // Module
            // Value v_num_regs;  // fixnum
            // Value v_num_data;  // fixnum
            // Value v_upreg_map; // Null for methods; Vector (of fixnum) for closures
            // // TODO: byte array inline?
            // Value v_insts; // Vector of fixnums
            // // TODO: arg array inline?
            // Value v_args; // Vector (of arbitrary values)
            Root<Code> r_block_code(gc, std::move(block_code));
            if (expr->parameters.empty()) {
                // It still has an implicit parameter: `it`.
            }
            throw std::logic_error("not implemented yet");
        } else if (DataExpr* expr = dynamic_cast<DataExpr*>(&_expr)) {
            for (const std::unique_ptr<Expr>& component : expr->components) {
                compile_expr(gc, r_code, scope, *component);
            }
            // MAKE_VECTOR: <num components>
            emit_op(OpCode::MAKE_VECTOR);
            emit_arg(Value::fixnum(expr->components.size()));
        } else if (SequenceExpr* expr = dynamic_cast<SequenceExpr*>(&_expr)) {
            if (expr->components.empty()) {
                // Empty sequence -> just load null.
                // LOAD_VALUE: <value>
                emit_op(OpCode::LOAD_VALUE);
                emit_arg(Value::null());
                return;
            }

            // Drop all but the last component's result.
            for (size_t i = 0; i < expr->components.size(); i++) {
                bool last = i == expr->components.size() - 1;
                compile_expr(gc, r_code, scope, *expr->components[i]);
                if (!last) {
                    emit_op(OpCode::DROP);
                }
            }
        } else if (TupleExpr* expr = dynamic_cast<TupleExpr*>(&_expr)) {
            for (const std::unique_ptr<Expr>& component : expr->components) {
                compile_expr(gc, r_code, scope, *component);
            }
            // MAKE_TUPLE: <num components>
            emit_op(OpCode::MAKE_TUPLE);
            emit_arg(Value::fixnum(expr->components.size()));
        } else {
            throw std::logic_error("forgot a case");
        }
    }

    // receiver, attrs are optional
    void compile_method(GC& gc, Root<Module>& r_module, const std::string& message,
                        SourceSpan& span, Expr* receiver, Expr& _decl, Expr& _body, Expr* attrs)
    {
        if (receiver) {
            std::stringstream ss;
            ss << "" << message << " takes no receiver";
            throw std::invalid_argument(ss.str());
        }

        Expr* decl = &_decl;

        while (ParenExpr* p = dynamic_cast<ParenExpr*>(decl)) {
            decl = p->inner.get();
        }

        if (BlockExpr* b = dynamic_cast<BlockExpr*>(decl)) {
            if (!b->parameters.empty()) {
                std::stringstream ss;
                ss << "" << message << " 'declaration' argument should not specify any parameters";
                throw std::invalid_argument(ss.str());
            }
            decl = b->body.get();
        } else {
            throw std::logic_error("non-block decls for method:does: not yet implemented");
        }

        std::vector<std::string> method_name_parts{};
        std::vector<std::string> param_names{};
        // TODO: param matchers

        auto add_param_name_and_matcher = [&param_names](Expr& param_decl,
                                                         const std::string& error_msg) -> void {
            if (NameExpr* d = dynamic_cast<NameExpr*>(&param_decl)) {
                param_names.push_back(std::get<std::string>(d->name.value));
                // TODO: add an any-matcher
                return;
            } else if (ParenExpr* d = dynamic_cast<ParenExpr*>(&param_decl)) {
                if (NAryMessageExpr* n = dynamic_cast<NAryMessageExpr*>(d->inner.get())) {
                    if (!n->target && n->messages.size() == 1) {
                        param_names.push_back(std::get<std::string>(n->messages[0].value));
                        // TODO: param matcher, from n->args[0]
                        return;
                    }
                }
            }
            throw std::invalid_argument(error_msg);
        };

        if (NameExpr* d = dynamic_cast<NameExpr*>(decl)) {
            method_name_parts.push_back(std::get<std::string>(d->name.value));
            param_names.push_back("self");
            // TODO: add an any-matcher
        } else if (UnaryMessageExpr* d = dynamic_cast<UnaryMessageExpr*>(decl)) {
            std::stringstream ss;
            ss << "When the " << message << "'declaration' argument is a unary message, "
               << "it must be a simple unary message of the form [target-name message-name] "
               << "or else a unary message of the form [(target-name: matcher) message-name]";
            const std::string& error_msg = ss.str();
            method_name_parts.push_back(std::get<std::string>(d->message.value));
            add_param_name_and_matcher(*d->target, error_msg);
        } else if (NAryMessageExpr* d = dynamic_cast<NAryMessageExpr*>(decl)) {
            std::stringstream ss;
            ss << "When the " << message
               << "'declaration' argument is an n-ary message, it must be a simple n-ary message "
               << "of the form "
               << "[target-name message: param-name ...] "
               << "or else an n-ary message of the form "
               << "[(target-name: matcher) message: (param-name: matcher) ...] "
               << "(the target-name is optional, as is each parameter matcher declaration)";
            const std::string& error_msg = ss.str();

            for (const Token& message : d->messages) {
                method_name_parts.push_back(std::get<std::string>(message.value));
            }

            if (d->target) {
                add_param_name_and_matcher(**d->target, error_msg);
            } else {
                param_names.push_back("self");
                // TODO: add an any-matcher
            }

            for (const std::unique_ptr<Expr>& arg : d->args) {
                add_param_name_and_matcher(*arg, error_msg);
            }
        } else {
            // TODO: allow unary / binary ops too
            std::stringstream ss;
            ss << message << " 'declaration' argument should be a name or message";
            throw std::invalid_argument(ss.str());
        }

        Root<String> r_method_name(gc, concat_with_suffix(gc, method_name_parts, ":"));

        Expr* body;
        if (BlockExpr* b = dynamic_cast<BlockExpr*>(&_body)) {
            if (!b->parameters.empty()) {
                std::stringstream ss;
                ss << message << "'body' argument should not specify any parameters";
                throw std::invalid_argument(ss.str());
            }
            body = b->body.get();
        } else {
            std::stringstream ss;
            ss << message << "'body' argument should be a block";
            throw std::invalid_argument(ss.str());
        }

        MultiMethod* multimethod;
        {
            Value* existing = module_lookup(*r_module, *r_method_name);
            if (existing) {
                if (existing->is_obj_multimethod()) {
                    multimethod = existing->obj_multimethod();
                } else {
                    throw std::invalid_argument("method name is already defined in the current "
                                                "context, but not as a multimethod");
                }
            } else {
                // We know we're about to add at least one method!
                Root<Vector> r_methods(gc, make_vector(gc, 1));
                Root<Vector> r_attributes(gc, make_vector(gc, 0));
                multimethod = make_multimethod(gc, r_method_name, r_methods, r_attributes);
                ValueRoot r_multimethod(gc, Value::object(multimethod));
                append(gc, r_module, r_method_name, r_multimethod);
                multimethod = r_multimethod->obj_multimethod();
            }
        }
        Root<MultiMethod> r_multimethod(gc, std::move(multimethod));

        // Compile the body.
        OptionalRoot<Array> r_upreg_map(gc, nullptr); // not a closure!
        Root<Array> r_insts(gc, make_array(gc, 0));
        Root<Array> r_args(gc, make_array(gc, 0));
        Root<Code> r_code(gc,
                          make_code(gc,
                                    r_module,
                                    /* num_regs */ 0,
                                    /* num_data */ 0,
                                    r_upreg_map,
                                    r_insts,
                                    r_args));
        Scope scope = {.bindings = {}, .base = nullptr};
        // Add param names as (immutable) bindings.
        int spot = 0;
        for (const std::string& param_name : param_names) {
            scope.bindings.emplace(param_name,
                                   Scope::Binding{
                                       .name = param_name,
                                       ._mutable = false,
                                       .spot = spot++,
                                   });
        }
        compile_expr(gc, r_code, scope, *body);

        // Generate the method.
        ValueRoot r_param_matchers(gc, Value::null()); // TODO
        OptionalRoot<Type> r_return_type(gc, nullptr); // TODO: support return types for methods
        Code* code = *r_code;
        OptionalRoot<Code> r_opt_code(gc, std::move(code));
        Root<Vector> r_attributes(gc, make_vector(gc, 0)); // TODO: support attributes for methods
        NativeHandler native_handler = nullptr;            // not a native handler
        ValueRoot r_method(gc,
                           Value::object(make_method(gc,
                                                     r_param_matchers,
                                                     r_return_type,
                                                     r_opt_code,
                                                     r_attributes,
                                                     native_handler)));

        Root<Vector> r_methods(gc, r_multimethod->v_methods.obj_vector());
        // TODO: refactor into util function to add to multimethod. Need to do other things like:
        // * roll up method inlining to multimethod inline-dispatch
        // * check for duplicate signatures
        // * sort methods / generate decision tree
        append(gc, r_methods, r_method);
    }

    Code* compile_module(GC& gc, OptionalRoot<Module>& base,
                         std::vector<std::unique_ptr<Expr>>& module_top_level_exprs)
    {
        // TODO: for future -- first find all multimethod definitions, add them to module (with zero
        // methods defined), and then go and compile everything.

        Root<Module> r_module(gc, make_module(gc, base, /* capacity */ 0));
        // `base` must be assumed to be an invalid pointer now.

        OptionalRoot<Array> r_upreg_map(gc, nullptr);
        Root<Array> r_insts(gc, make_array(gc, 0));
        Root<Array> r_args(gc, make_array(gc, 0));

        // TODO: maybe use a local struct which is better suited for construction?
        Root<Code> r_top_level_code(gc,
                                    make_code(gc,
                                              r_module,
                                              /* num_regs */ 0,
                                              /* num_data */ 0,
                                              r_upreg_map,
                                              r_insts,
                                              r_args));

        for (std::unique_ptr<Expr>& top_level_expr : module_top_level_exprs) {
            if (NAryMessageExpr* expr = dynamic_cast<NAryMessageExpr*>(top_level_expr.get())) {
                if (expr->messages.size() == 2 &&
                    std::get<std::string>(expr->messages[0].value) == "method" &&
                    std::get<std::string>(expr->messages[1].value) == "does") {
                    compile_method(gc,
                                   r_module,
                                   "method:does:",
                                   expr->span,
                                   expr->target ? expr->target->get() : nullptr,
                                   *expr->args[0],
                                   *expr->args[1],
                                   nullptr);
                    continue;
                } else if (expr->messages.size() == 3 &&
                           std::get<std::string>(expr->messages[0].value) == "method" &&
                           std::get<std::string>(expr->messages[1].value) == "does" &&
                           std::get<std::string>(expr->messages[2].value) == ":"

                ) {
                    compile_method(gc,
                                   r_module,
                                   "method:does:",
                                   expr->span,
                                   expr->target ? expr->target->get() : nullptr,
                                   *expr->args[0],
                                   *expr->args[1],
                                   expr->args[2].get());
                    continue;
                }
            }
            // Lexical state is maintained through the under-construction module. Each module
            // top-level expression gets the same empty scope on top.
            Scope scope = {.bindings = {}, .base = nullptr};
            compile_expr(gc, r_top_level_code, scope, *top_level_expr);
        }

        return *r_top_level_code;
    }
};

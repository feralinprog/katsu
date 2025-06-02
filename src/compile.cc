#include "compile.h"

#include "assertions.h"
#include "value_utils.h"
#include "vm.h"

#include <cstring>
#include <map>
#include <vector>

#include <iostream>
#include <sstream>

namespace Katsu
{
    struct Binding
    {
        const std::string name;
        bool _mutable;
        uint32_t local_index;
    };

    struct CodeBuilder
    {
        // This has Vector variants of all the Arrays that go into a Code object.
        // This way we can append/grow, then copy into Arrays to finalize the Code.

        Root<Module>& r_module;
        // Number of parameters.
        uint32_t num_params;
        // Number of locals.
        uint32_t num_regs;
        // Max size of data stack (kept up to date using emit_op().)
        uint32_t num_data;
        // Mapping from upreg index to local index, for initializing locals when invoking a closure
        // instance.
        OptionalRoot<Vector>& r_upreg_map;
        // The OpCode instructions...
        Root<Vector>& r_insts;
        // and their arguments.
        Root<Vector>& r_args;

        // Mapping from upreg index to _base's_ local indices, for initializing upreg array when
        // creating a closure instance.
        Root<Vector>& r_upreg_loading;

        std::map<std::string, Binding> bindings;
        CodeBuilder* base;

        uint32_t stack_height;
        void bump_stack(int64_t delta)
        {
            // Don't underflow the stack.
            ASSERT(delta >= 0 || this->stack_height >= -delta);
            this->stack_height += delta;
            if (this->stack_height > this->num_data) {
                this->num_data = this->stack_height;
            }
        }

        void emit_op(GC& gc, OpCode op, int64_t stack_height_delta)
        {
            this->bump_stack(stack_height_delta);
            ValueRoot r_op(gc, Value::fixnum(op));
            append(gc, this->r_insts, r_op);
        }

        void emit_arg(GC& gc, ValueRoot& r_arg)
        {
            append(gc, this->r_args, r_arg);
        }
        void emit_arg(GC& gc, Value arg)
        {
            ValueRoot r_arg(gc, std::move(arg));
            append(gc, this->r_args, r_arg);
        }

        Binding* lookup(const std::string& name, size_t* depth)
        {
            size_t _depth = 0;
            CodeBuilder* cur = this;
            while (cur) {
                auto it = cur->bindings.find(name);
                if (it != cur->bindings.end()) {
                    if (depth) {
                        *depth = _depth;
                    }
                    return &it->second;
                }
                cur = cur->base;
                _depth++;
            }
            return nullptr;
        }
        Binding* lookup(String* name, size_t* depth)
        {
            // TODO: check name->length against size_t?
            size_t _depth = 0;
            CodeBuilder* cur = this;
            while (cur) {
                for (auto& [binding_name, binding] : cur->bindings) {
                    if (!string_eq(name, binding_name)) {
                        continue;
                    }
                    if (depth) {
                        *depth = _depth;
                    }
                    return &binding;
                }
                cur = cur->base;
                _depth++;
            }
            return nullptr;
        }

        Code* finalize(GC& gc)
        {
            Array* maybe_upreg_map;
            if (r_upreg_map) {
                Vector* upreg_map_vec = *this->r_upreg_map;
                Root<Vector> r_upreg_map_vec(gc, std::move(upreg_map_vec));
                maybe_upreg_map = vector_to_array(gc, r_upreg_map_vec);
            } else {
                maybe_upreg_map = nullptr;
            }
            OptionalRoot<Array> r_upreg_map_arr(gc, std::move(maybe_upreg_map));

            Root<Array> r_insts_arr(gc, vector_to_array(gc, this->r_insts));
            Root<Array> r_args_arr(gc, vector_to_array(gc, this->r_args));

            return make_code(gc,
                             this->r_module,
                             this->num_params,
                             this->num_regs,
                             this->num_data,
                             r_upreg_map_arr,
                             r_insts_arr,
                             r_args_arr);
        }
    };

    // TODO: track tail-position and emit INVOKE_TAIL when requested
    void compile_expr(GC& gc, CodeBuilder& builder, Expr& _expr)
    {
        if (UnaryOpExpr* expr = dynamic_cast<UnaryOpExpr*>(&_expr)) {
            const std::string& op_name = std::get<std::string>(expr->op.value);
            Root<String> r_name(gc, make_string(gc, op_name));
            if (!module_lookup(*builder.r_module, *r_name)) {
                throw compile_error("name is not defined in module", expr->op.span);
            }
            compile_expr(gc, builder, *expr->arg);
            // INVOKE: <name>, <num args>
            builder.emit_op(gc, OpCode::INVOKE, /* stack_height_delta */ -1 + 1);
            builder.emit_arg(gc, r_name.value());
            builder.emit_arg(gc, Value::fixnum(1));
        } else if (BinaryOpExpr* expr = dynamic_cast<BinaryOpExpr*>(&_expr)) {
            const std::string& op_name = std::get<std::string>(expr->op.value) + ":";
            Root<String> r_name(gc, make_string(gc, op_name));
            if (!module_lookup(*builder.r_module, *r_name)) {
                throw compile_error("name is not defined in module", expr->op.span);
            }
            compile_expr(gc, builder, *expr->left);
            compile_expr(gc, builder, *expr->right);
            // INVOKE: <name>, <num args>
            builder.emit_op(gc, OpCode::INVOKE, /* stack_height_delta */ -2 + 1);
            builder.emit_arg(gc, r_name.value());
            builder.emit_arg(gc, Value::fixnum(2));
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
            size_t var_depth;
            const Binding* local = builder.lookup(*r_name, &var_depth);
            if (local) {
                // If this is an upvar access, we need to make sure it's tracked in the upreg map
                // and loading vector.
                if (var_depth > 0) {
                    // TODO: handle upvar depth more than 1.
                    ASSERT(var_depth == 1);

                    // Add it for tracking!

                    ValueRoot r_upvar_index(gc, Value::fixnum(local->local_index));
                    append(gc, builder.r_upreg_loading, r_upvar_index);

                    builder.bindings.emplace(name,
                                             Binding{
                                                 .name = name,
                                                 ._mutable = local->_mutable,
                                                 .local_index = builder.num_regs++,
                                             });
                    local = &builder.bindings[name];

                    // If var_depth > 0, we should be compiling a closure, so the builder should
                    // have r_upreg_map populated.
                    ASSERT(builder.r_upreg_map);
                    Root<Vector> r_upreg_map(gc, *builder.r_upreg_map);
                    ValueRoot r_local_index(gc, Value::fixnum(local->local_index));
                    append(gc, r_upreg_map, r_local_index);
                }

                if (local->_mutable) {
                    // LOAD_REF: <local index>
                    builder.emit_op(gc, OpCode::LOAD_REF, /* stack_height_delta */ +1);
                    builder.emit_arg(gc, Value::fixnum(local->local_index));
                } else {
                    // LOAD_REG: <local index>
                    builder.emit_op(gc, OpCode::LOAD_REG, /* stack_height_delta */ +1);
                    builder.emit_arg(gc, Value::fixnum(local->local_index));
                }
            } else if (module_lookup(*builder.r_module, *r_name)) {
                // LOAD_MODULE: <name>
                builder.emit_op(gc, OpCode::LOAD_MODULE, /* stack_height_delta */ +1);
                builder.emit_arg(gc, r_name.value());
            } else {
                throw compile_error("name is not defined in module or in local scope",
                                    expr->name.span);
            }
        } else if (LiteralExpr* expr = dynamic_cast<LiteralExpr*>(&_expr)) {
            switch (expr->literal.type) {
                case TokenType::INTEGER: {
                    // LOAD_VALUE: <value>
                    builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1);
                    builder.emit_arg(gc, Value::fixnum(std::get<long long>(expr->literal.value)));
                    break;
                }
                case TokenType::STRING: {
                    // LOAD_VALUE: <value>
                    builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1);
                    builder.emit_arg(
                        gc,
                        Value::object(make_string(gc, std::get<std::string>(expr->literal.value))));
                    break;
                }
                case TokenType::SYMBOL: {
                    // TODO: implement SYMBOL compilation
                    ASSERT_MSG(false, "not implemented");
                    break;
                }
                default: {
                    ASSERT_MSG(false, "unexpected token type");
                    break;
                }
            }
        } else if (UnaryMessageExpr* expr = dynamic_cast<UnaryMessageExpr*>(&_expr)) {
            const std::string& name = std::get<std::string>(expr->message.value);
            Root<String> r_name(gc, make_string(gc, name));
            if (!module_lookup(*builder.r_module, *r_name)) {
                throw compile_error("name is not defined in module", expr->message.span);
            }
            compile_expr(gc, builder, *expr->target);
            // INVOKE: <name>, <num args>
            builder.emit_op(gc, OpCode::INVOKE, /* stack_height_delta */ -1 + 1);
            builder.emit_arg(gc, r_name.value());
            builder.emit_arg(gc, Value::fixnum(1));
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
                size_t var_depth;
                const Binding* maybe_local = builder.lookup(name, &var_depth);
                if (maybe_local) {
                    // TODO: implement upvar assignment before access
                    ASSERT_MSG(var_depth == 0,
                               "assignment of upvar (before _reading_ upvar) not implemented yet");
                    const Binding& local = *maybe_local;
                    if (local._mutable) {
                        // Ensure there's no target provided.
                        if (expr->target) {
                            throw compile_error("setting mutable variable requires no target",
                                                expr->span);
                        }
                        compile_expr(gc, builder, *expr->args[0]);
                        // STORE_REF: <local index>
                        builder.emit_op(gc, OpCode::STORE_REF, /* stack_height_delta */ -1);
                        builder.emit_arg(gc, Value::fixnum(local.local_index));
                        // LOAD_VALUE: null
                        builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1);
                        builder.emit_arg(gc, Value::null());
                        return;
                    }
                    // TODO: maybe if module lookup fails after this, error message should
                    // include something like "there's a local of this name, did you mean to
                    // make it mutable?".
                }
            }
            // TODO: handle this as a builtin within the module.
            if (expr->messages.size() == 1 &&
                (std::get<std::string>(expr->messages[0].value) == "let" ||
                 std::get<std::string>(expr->messages[0].value) == "mut")) {
                bool _mutable = std::get<std::string>(expr->messages[0].value) == "mut";
                if (expr->target) {
                    throw compile_error("let: / mut: require no target", expr->span);
                }
                if (BinaryOpExpr* b = dynamic_cast<BinaryOpExpr*>(expr->args[0].get())) {
                    if (std::get<std::string>(b->op.value) == "=") {
                        if (NameExpr* n = dynamic_cast<NameExpr*>(b->left.get())) {
                            const std::string& name = std::get<std::string>(n->name.value);
                            Root<String> r_name(gc, make_string(gc, name));
                            if (_mutable && builder.lookup(*r_name, nullptr)) {
                                // TODO: maybe just allow?
                                throw compile_error(
                                    "cannot shadow mut: binding with another mut: binding",
                                    expr->span);
                            }
                            // Compile initial value _without_ the new binding established.
                            compile_expr(gc, builder, *b->right);
                            // Then add the binding.
                            uint32_t local_index = builder.num_regs++;
                            builder.bindings.emplace(name,
                                                     Binding{
                                                         .name = name,
                                                         ._mutable = _mutable,
                                                         .local_index = local_index,
                                                     });
                            // Store the value (generated by the RHS generated code) into the newly
                            // allocated register.
                            if (_mutable) {
                                // INIT_REF: <local index>
                                builder.emit_op(gc,
                                                OpCode::INIT_REF,
                                                /* stack_height_delta */ -1);
                                builder.emit_arg(gc, Value::fixnum(local_index));
                            } else {
                                // STORE_REG: <local index>
                                builder.emit_op(gc,
                                                OpCode::STORE_REG,
                                                /* stack_height_delta */ -1);
                                builder.emit_arg(gc, Value::fixnum(local_index));
                            }
                            // LOAD:VALUE: null
                            builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1);
                            builder.emit_arg(gc, Value::null());
                            return;
                        }
                    }
                }
            }
            if (!module_lookup(*builder.r_module, *r_name)) {
                throw compile_error(
                    "name is not defined in module (and is also not <a mutable local>:)",
                    expr->span);
            }

            if (expr->target) {
                compile_expr(gc, builder, *expr->target.value());
            } else {
                // Load the default receiver, which is always register 0.
                // LOAD_REG: <local index>
                builder.emit_op(gc, OpCode::LOAD_REG, /* stack_height_delta */ +1);
                builder.emit_arg(gc, Value::fixnum(0));
            }
            for (const std::unique_ptr<Expr>& arg : expr->args) {
                compile_expr(gc, builder, *arg);
            }
            // INVOKE: <name>, <num args>
            builder.emit_op(gc,
                            OpCode::INVOKE,
                            /* stack_height_delta */ -(int64_t)expr->args.size());
            builder.emit_arg(gc, r_name.value());
            builder.emit_arg(gc, Value::fixnum(1 + expr->args.size()));
        } else if (ParenExpr* expr = dynamic_cast<ParenExpr*>(&_expr)) {
            compile_expr(gc, builder, *expr->inner);
        } else if (BlockExpr* expr = dynamic_cast<BlockExpr*>(&_expr)) {
            // Block with no parameters still has one implicit parameter: `it`.

            OptionalRoot<Vector> r_upreg_map(gc, make_vector(gc, 0));
            Root<Vector> r_insts(gc, make_vector(gc, 0));
            Root<Vector> r_args(gc, make_vector(gc, 0));
            Root<Vector> r_upreg_loading(gc, make_vector(gc, 0));
            CodeBuilder closure_builder{
                .r_module = builder.r_module,
                .num_params = expr->parameters.empty()
                                  ? 1
                                  : (uint32_t)expr->parameters.size(), // TODO: check size_t?
                .num_regs = expr->parameters.empty()
                                ? 1
                                : (uint32_t)expr->parameters.size(), // TODO: check size_t?
                .num_data = 0,
                .r_upreg_map = r_upreg_map,
                .r_insts = r_insts,
                .r_args = r_args,
                .r_upreg_loading = r_upreg_loading,
                .bindings = {},
                .base = &builder,
            };
            // Add param names as (immutable) bindings.
            uint32_t local_index = 0;
            if (expr->parameters.empty()) {
                closure_builder.bindings.emplace(
                    "it",
                    Binding{.name = "it", ._mutable = false, .local_index = local_index++});
            } else {
                for (const std::string& param_name : expr->parameters) {
                    closure_builder.bindings.emplace(param_name,
                                                     Binding{.name = param_name,
                                                             ._mutable = false,
                                                             .local_index = local_index++});
                }
            }
            compile_expr(gc, closure_builder, *expr->body);
            ValueRoot r_closure_code(gc, Value::object(closure_builder.finalize(gc)));
            uint64_t num_upreg_loads = closure_builder.r_upreg_loading->length;
            ASSERT(num_upreg_loads == closure_builder.r_upreg_map->length);
            for (uint64_t i = 0; i < num_upreg_loads; i++) {
                int64_t load_index =
                    closure_builder.r_upreg_loading->v_array.obj_array()->components()[i].fixnum();
                // TODO: check range
                // LOAD_REG: <local index>
                builder.emit_op(gc, OpCode::LOAD_REG, /* stack_height_delta */ +1);
                builder.emit_arg(gc, Value::fixnum(load_index));
            }
            builder.emit_op(gc,
                            OpCode::MAKE_CLOSURE,
                            /* stack_height_delta */ -(int64_t)num_upreg_loads + 1);
            builder.emit_arg(gc, *r_closure_code);
        } else if (DataExpr* expr = dynamic_cast<DataExpr*>(&_expr)) {
            for (const std::unique_ptr<Expr>& component : expr->components) {
                compile_expr(gc, builder, *component);
            }
            // MAKE_VECTOR: <num components>
            builder.emit_op(gc,
                            OpCode::MAKE_VECTOR,
                            /* stack_height_delta */ -(int64_t)expr->components.size() + 1);
            builder.emit_arg(gc, Value::fixnum(expr->components.size()));
        } else if (SequenceExpr* expr = dynamic_cast<SequenceExpr*>(&_expr)) {
            if (expr->components.empty()) {
                // Empty sequence -> just load null.
                // LOAD_VALUE: <value>
                builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1);
                builder.emit_arg(gc, Value::null());
                return;
            }

            // Drop all but the last component's result.
            for (size_t i = 0; i < expr->components.size(); i++) {
                bool last = i == expr->components.size() - 1;
                compile_expr(gc, builder, *expr->components[i]);
                if (!last) {
                    builder.emit_op(gc, OpCode::DROP, /* stack_height_delta */ -1);
                }
            }
        } else if (TupleExpr* expr = dynamic_cast<TupleExpr*>(&_expr)) {
            for (const std::unique_ptr<Expr>& component : expr->components) {
                compile_expr(gc, builder, *component);
            }
            // MAKE_TUPLE: <num components>
            builder.emit_op(gc,
                            OpCode::MAKE_TUPLE,
                            /* stack_height_delta */ -(int64_t)expr->components.size() + 1);
            builder.emit_arg(gc, Value::fixnum(expr->components.size()));
        } else {
            ASSERT_MSG(false, "forgot an Expr subtype");
        }
    }

    // receiver, attrs are optional
    void compile_method(GC& gc, Root<Module>& r_module, const std::string& message,
                        SourceSpan& span, Expr* receiver, Expr& _decl, Expr& _body, Expr* attrs)
    {
        if (receiver) {
            std::stringstream ss;
            ss << "" << message << " takes no receiver";
            throw compile_error(ss.str(), span);
        }

        Expr* decl = &_decl;

        while (ParenExpr* p = dynamic_cast<ParenExpr*>(decl)) {
            decl = p->inner.get();
        }

        if (BlockExpr* b = dynamic_cast<BlockExpr*>(decl)) {
            if (!b->parameters.empty()) {
                std::stringstream ss;
                ss << "" << message << " 'declaration' argument should not specify any parameters";
                throw compile_error(ss.str(), decl->span);
            }
            decl = b->body.get();
        } else {
            // TODO: implement non-block method declarations
            ASSERT_MSG(false, "non-block decls for method:does: not implemented");
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
            throw compile_error(error_msg, param_decl.span);
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
            throw compile_error(ss.str(), decl->span);
        }

        Root<String> r_method_name(gc, concat_with_suffix(gc, method_name_parts, ":"));

        Expr* body;
        if (BlockExpr* b = dynamic_cast<BlockExpr*>(&_body)) {
            if (!b->parameters.empty()) {
                std::stringstream ss;
                ss << message << "'body' argument should not specify any parameters";
                throw compile_error(ss.str(), _body.span);
            }
            body = b->body.get();
        } else {
            std::stringstream ss;
            ss << message << "'body' argument should be a block";
            throw compile_error(ss.str(), _body.span);
        }

        MultiMethod* multimethod;
        {
            Value* existing = module_lookup(*r_module, *r_method_name);
            if (existing) {
                if (existing->is_obj_multimethod()) {
                    multimethod = existing->obj_multimethod();
                } else {
                    throw compile_error("method name is already defined in the current context, "
                                        "but not as a multimethod",
                                        span);
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
        OptionalRoot<Vector> r_upreg_map(gc, nullptr); // not a closure!
        Root<Vector> r_insts(gc, make_vector(gc, 0));
        Root<Vector> r_args(gc, make_vector(gc, 0));
        Root<Vector> r_upreg_loading(gc, make_vector(gc, 0));
        CodeBuilder builder{
            .r_module = r_module,
            .num_params = (uint32_t)param_names.size(), // TODO: check size_t?
            .num_regs = (uint32_t)param_names.size(),   // TODO: check size_t?
            .num_data = 0,
            .r_upreg_map = r_upreg_map,
            .r_insts = r_insts,
            .r_args = r_args,
            .r_upreg_loading = r_upreg_loading,
            .bindings = {},
            .base = nullptr,
        };
        // Add param names as (immutable) bindings.
        uint32_t local_index = 0;
        for (const std::string& param_name : param_names) {
            builder.bindings.emplace(param_name,
                                     Binding{
                                         .name = param_name,
                                         ._mutable = false,
                                         .local_index = local_index++,
                                     });
        }
        compile_expr(gc, builder, *body);

        // Generate the method.
        ValueRoot r_param_matchers(gc, Value::null()); // TODO
        OptionalRoot<Type> r_return_type(gc, nullptr); // TODO: support return types for methods
        Code* code = builder.finalize(gc);
        OptionalRoot<Code> r_opt_code(gc, std::move(code));
        Root<Vector> r_attributes(gc, make_vector(gc, 0)); // TODO: support attributes for methods
        NativeHandler native_handler = nullptr;            // not a native handler
        IntrinsicHandler intrinsic_handler = nullptr;      // nor an intrinsic handler
        ValueRoot r_method(gc,
                           Value::object(make_method(gc,
                                                     r_param_matchers,
                                                     r_return_type,
                                                     r_opt_code,
                                                     r_attributes,
                                                     native_handler,
                                                     intrinsic_handler)));

        Root<Vector> r_methods(gc, r_multimethod->v_methods.obj_vector());
        // TODO: refactor into util function to add to multimethod. Need to do other things like:
        // * roll up method inlining to multimethod inline-dispatch
        // * check for duplicate signatures
        // * sort methods / generate decision tree
        // * invalidate any methods whose compilation depended on this multimethod not changing
        append(gc, r_methods, r_method);
    }

    Code* compile_into_module(GC& gc, Root<Module>& r_module,
                              std::vector<std::unique_ptr<Expr>>& module_top_level_exprs)
    {
        // TODO: for future -- first find all multimethod definitions, add them to module (with zero
        // methods defined), and then go and compile everything.

        OptionalRoot<Vector> r_upreg_map(gc, nullptr); // not a closure!
        Root<Vector> r_insts(gc, make_vector(gc, 0));
        Root<Vector> r_args(gc, make_vector(gc, 0));
        Root<Vector> r_upreg_loading(gc, make_vector(gc, 0));
        CodeBuilder builder{
            .r_module = r_module,
            .num_regs = 0,
            .num_data = 0,
            .r_upreg_map = r_upreg_map,
            .r_insts = r_insts,
            .r_args = r_args,
            .r_upreg_loading = r_upreg_loading,
            .bindings = {},
            .base = nullptr,
        };
        // TODO: something less hacky? All Code is built assuming that local @0 is the default
        // receiver. For top level code, there isn't really a default receiver (other than null, I
        // suppose).
        builder.num_regs++;
        // The VM loads call frame registers with nulls when setting up a frame, so we don't need to
        // have bytecode actually write a null into local @0.

        for (std::unique_ptr<Expr>& top_level_expr : module_top_level_exprs) {
            // TODO: handle method:does:[::] as a builtin, looked up in module.
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
                } else if (expr->messages.size() == 1 &&
                           std::get<std::string>(expr->messages[0].value) == "let") {
                    if (expr->target) {
                        throw compile_error("let: requires no target", expr->span);
                    }
                    if (BinaryOpExpr* b = dynamic_cast<BinaryOpExpr*>(expr->args[0].get())) {
                        if (std::get<std::string>(b->op.value) == "=") {
                            if (NameExpr* n = dynamic_cast<NameExpr*>(b->left.get())) {
                                const std::string& name = std::get<std::string>(n->name.value);
                                Root<String> r_name(gc, make_string(gc, name));
                                // Compile initial value _without_ the new binding established.
                                compile_expr(gc, builder, *b->right);
                                // Then add the (module) binding.
                                // TODO: this is weird. what value do we put in the module initially
                                // (during compile time)?
                                ValueRoot r_init(gc, Value::null());
                                append(gc, r_module, r_name, r_init);
                                // STORE_MODULE: <name>
                                builder.emit_op(gc,
                                                OpCode::STORE_MODULE,
                                                /* stack_height_delta */ -1);
                                builder.emit_arg(gc, r_name.value());
                                // LOAD:VALUE: null
                                builder.emit_op(gc,
                                                OpCode::LOAD_VALUE,
                                                /* stack_height_delta */ +1);
                                builder.emit_arg(gc, Value::null());
                                continue;
                            }
                        }
                    }
                }
            }
            compile_expr(gc, builder, *top_level_expr);
        }

        // If the top level expressions were all definitions, there may be no code to execute (and
        // to get a value from). In this case, just add stub code to load null.
        if (builder.r_insts->length == 0) {
            // LOAD_VALUE: null
            builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1);
            builder.emit_arg(gc, Value::null());
        }

        return builder.finalize(gc);
    }

    Code* compile_module(GC& gc, OptionalRoot<Module>& r_base,
                         std::vector<std::unique_ptr<Expr>>& module_top_level_exprs,
                         Module*& out_module)
    {
        Root<Module> r_module(gc, make_module(gc, r_base, /* capacity */ 0));
        Code* code = compile_into_module(gc, r_module, module_top_level_exprs);
        out_module = *r_module;
        return code;
    }
};

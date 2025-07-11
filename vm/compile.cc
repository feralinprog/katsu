#include "compile.h"

#include "assertions.h"
#include "span.h"
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
        std::string name;
        bool _mutable;
        uint32_t local_index;
    };

    struct CodeBuilder
    {
        // This has Vector variants of all the Arrays that go into a Code object.
        // This way we can append/grow, then copy into Arrays to finalize the Code.

        Root<Assoc>& r_module;
        Root<Vector>& r_imports;
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
        // their arguments...
        Root<Vector>& r_args;
        // and their source spans (e.g. from convert_span()).
        Root<Vector>& r_inst_spans;

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

        void emit_op(GC& gc, OpCode op, int64_t stack_height_delta, SourceSpan& span)
        {
            // Instruction encoding:
            // <3 bytes arg-offset> <1 byte opcode>
            ASSERT((op & ~0xFF) == 0);
            ASSERT((this->r_args->length & ~0xFFFFFF) == 0);
            uint32_t inst = (this->r_args->length << 8) | op;
            this->bump_stack(stack_height_delta);
            ValueRoot r_op(gc, Value::fixnum(inst));
            append(gc, this->r_insts, r_op);

            ValueRoot r_span(gc, Value::object(convert_span(gc, span)));
            append(gc, this->r_inst_spans, r_span);
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

        Tuple* convert_span(GC& gc, SourceSpan& span)
        {
            // TODO: should intern strings. This is hugely inefficient.
            Root<String> r_source(gc, make_string(gc, *span.file.path));
            Tuple* tuple = make_tuple(gc, 7);
            tuple->components()[0] = r_source.value();
            tuple->components()[1] = Value::fixnum(span.start.index);
            tuple->components()[2] = Value::fixnum(span.start.line);
            tuple->components()[3] = Value::fixnum(span.start.column);
            tuple->components()[4] = Value::fixnum(span.end.index);
            tuple->components()[5] = Value::fixnum(span.end.line);
            tuple->components()[6] = Value::fixnum(span.end.column);
            return tuple;
        }

        Code* finalize(GC& gc, SourceSpan& code_span)
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

            Root<Tuple> r_span(gc, convert_span(gc, code_span));
            Root<Array> r_inst_spans_arr(gc, vector_to_array(gc, this->r_inst_spans));

            return make_code(gc,
                             this->r_module,
                             this->num_params,
                             this->num_regs,
                             this->num_data,
                             r_upreg_map_arr,
                             r_insts_arr,
                             r_args_arr,
                             r_span,
                             r_inst_spans_arr);
        }
    };

    enum LookupResult
    {
        SUCCESS,
        NOT_FOUND,
        AMBIGUOUS,
    };
    // The out-var `result` is only populated on SUCCESS, and can be nullptr if the actual looked-up
    // Value is not needed.
    LookupResult lookup_name(Assoc* module, Vector* imports, String* name, Value* result = nullptr)
    {
        Value* lookup = assoc_lookup(module, name);
        for (Value import : imports) {
            // Silently ignore non-Assoc imports.
            if (!import.is_obj_assoc()) {
                continue;
            }
            Assoc* import_module = import.obj_assoc();
            Value* import_lookup = assoc_lookup(import_module, name);
            if (!import_lookup) {
                continue;
            }

            if (lookup) {
                return AMBIGUOUS;
            } else {
                lookup = import_lookup;
            }
        }
        if (lookup) {
            if (result) {
                *result = *lookup;
            }
            return SUCCESS;
        } else {
            return NOT_FOUND;
        }
    }
    LookupResult lookup_name(CodeBuilder& builder, String* name, Value* result = nullptr)
    {
        return lookup_name(*builder.r_module, *builder.r_imports, name, result);
    }
    // Variants which just throw an appropriate compile_error and return the result value.
    Value lookup_name(Assoc* module, Vector* imports, String* name, const SourceSpan& span)
    {
        Value lookup;
        LookupResult result = lookup_name(module, imports, name, &lookup);
        switch (result) {
            case SUCCESS: return lookup;
            case NOT_FOUND:
                throw compile_error("name not found in module or its current imports", span);
            case AMBIGUOUS:
                throw compile_error("ambiguous lookup for name in module and its current imports",
                                    span);
        }
    }
    Value lookup_name(CodeBuilder& builder, String* name, const SourceSpan& span)
    {
        return lookup_name(*builder.r_module, *builder.r_imports, name, span);
    }

    // Ensures that a name is either a local or in module scope, but is not a positive-depth upvar.
    // If local/upvar, returns the new binding; else returns nullptr.
    const Binding* raise_upvar(GC& gc, CodeBuilder& builder, const std::string& name)
    {
        Root<String> r_name(gc, make_string(gc, name));
        size_t var_depth;
        const Binding* local_or_upvar = builder.lookup(*r_name, &var_depth);
        Value lookup;
        if (local_or_upvar) {
            if (var_depth > 1) {
                raise_upvar(gc, *builder.base, name);
                local_or_upvar = builder.lookup(*r_name, &var_depth);
                ASSERT(var_depth == 1);
            }

            ASSERT(var_depth <= 1);

            // If this is an upvar access, we need to make sure it's tracked in the upreg map
            // and loading vector.
            if (var_depth == 0) {
                const Binding* local = local_or_upvar;
                return local;
            } else {
                // Add it for tracking!
                const Binding* upvar = local_or_upvar;

                ValueRoot r_upvar_index(gc, Value::fixnum(upvar->local_index));
                append(gc, builder.r_upreg_loading, r_upvar_index);

                builder.bindings.emplace(name,
                                         Binding{
                                             .name = name,
                                             ._mutable = upvar->_mutable,
                                             .local_index = builder.num_regs++,
                                         });
                const Binding* local = &builder.bindings[name];

                // If var_depth > 0, we should be compiling a closure, so the builder should
                // have r_upreg_map populated.
                ASSERT(builder.r_upreg_map);
                Root<Vector> r_upreg_map(gc, *builder.r_upreg_map);
                ValueRoot r_local_index(gc, Value::fixnum(local->local_index));
                append(gc, r_upreg_map, r_local_index);

                return local;
            }
        } else {
            return nullptr;
        }
    }

    void compile_expr(GC& gc, CodeBuilder& builder, Expr& _expr, bool tail_position, bool tail_call)
    {
        OpCode invoke_op = tail_call ? OpCode::INVOKE_TAIL : OpCode::INVOKE;
        if (UnaryOpExpr* expr = dynamic_cast<UnaryOpExpr*>(&_expr)) {
            const std::string& op_name = std::get<std::string>(expr->op.value);
            Root<String> r_name(gc, make_string(gc, op_name));
            ValueRoot r_existing(gc, lookup_name(builder, *r_name, expr->op.span));
            compile_expr(gc, builder, *expr->arg, /* tail_position */ false, /* tail_call */ false);
            // INVOKE: <multimethod>, <num args>
            builder.emit_op(gc, invoke_op, /* stack_height_delta */ -1 + 1, _expr.span);
            builder.emit_arg(gc, *r_existing);
            builder.emit_arg(gc, Value::fixnum(1));
        } else if (BinaryOpExpr* expr = dynamic_cast<BinaryOpExpr*>(&_expr)) {
            const std::string& op_name = std::get<std::string>(expr->op.value) + ":";
            Root<String> r_name(gc, make_string(gc, op_name));
            ValueRoot r_existing(gc, lookup_name(builder, *r_name, expr->op.span));
            compile_expr(gc,
                         builder,
                         *expr->left,
                         /* tail_position */ false,
                         /* tail_call */ false);
            compile_expr(gc,
                         builder,
                         *expr->right,
                         /* tail_position */ false,
                         /* tail_call */ false);
            // INVOKE: <multimethod>, <num args>
            builder.emit_op(gc, invoke_op, /* stack_height_delta */ -2 + 1, _expr.span);
            builder.emit_arg(gc, *r_existing);
            builder.emit_arg(gc, Value::fixnum(2));
        } else if (NameExpr* expr = dynamic_cast<NameExpr*>(&_expr)) {
            const std::string& name = std::get<std::string>(expr->name.value);
            Root<String> r_name(gc, make_string(gc, name));
            const Binding* local = raise_upvar(gc, builder, name);
            Value lookup;
            if (local) {
                if (local->_mutable) {
                    // LOAD_REF: <local index>
                    builder.emit_op(gc, OpCode::LOAD_REF, /* stack_height_delta */ +1, _expr.span);
                    builder.emit_arg(gc, Value::fixnum(local->local_index));
                } else {
                    // LOAD_REG: <local index>
                    builder.emit_op(gc, OpCode::LOAD_REG, /* stack_height_delta */ +1, _expr.span);
                    builder.emit_arg(gc, Value::fixnum(local->local_index));
                }
            } else if (lookup_name(builder, *r_name, &lookup) == SUCCESS) {
                ValueRoot r_lookup(gc, std::move(lookup));
                if (r_lookup->is_obj_multimethod()) {
                    Value v_name = r_lookup->obj_multimethod()->v_name;
                    ValueRoot r_name(gc, std::move(v_name));
                    // Load the default receiver, which is always register 0.
                    // LOAD_REG: <local index>
                    builder.emit_op(gc, OpCode::LOAD_REG, /* stack_height_delta */ +1, _expr.span);
                    builder.emit_arg(gc, Value::fixnum(0));
                    // INVOKE: <multimethod>, <num args>
                    builder.emit_op(gc, invoke_op, /* stack_height_delta */ -1 + 1, _expr.span);
                    builder.emit_arg(gc, *r_lookup);
                    builder.emit_arg(gc, Value::fixnum(1));
                } else if (r_lookup->is_obj_ref()) {
                    // LOAD_MODULE: <ref value>
                    builder.emit_op(gc,
                                    OpCode::LOAD_MODULE,
                                    /* stack_height_delta */ +1,
                                    _expr.span);
                    builder.emit_arg(gc, *r_lookup);
                } else {
                    // LOAD_VALUE: <value>
                    builder.emit_op(gc,
                                    OpCode::LOAD_VALUE,
                                    /* stack_height_delta */ +1,
                                    _expr.span);
                    builder.emit_arg(gc, *r_lookup);
                }
            } else {
                throw compile_error("name is not defined in module or in local scope",
                                    expr->name.span);
            }
        } else if (LiteralExpr* expr = dynamic_cast<LiteralExpr*>(&_expr)) {
            switch (expr->literal.type) {
                case TokenType::INTEGER: {
                    // LOAD_VALUE: <value>
                    builder.emit_op(gc,
                                    OpCode::LOAD_VALUE,
                                    /* stack_height_delta */ +1,
                                    _expr.span);
                    builder.emit_arg(gc, Value::fixnum(std::get<long long>(expr->literal.value)));
                    break;
                }
                case TokenType::STRING: {
                    // LOAD_VALUE: <value>
                    builder.emit_op(gc,
                                    OpCode::LOAD_VALUE,
                                    /* stack_height_delta */ +1,
                                    _expr.span);
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
            ValueRoot r_existing(gc, lookup_name(builder, *r_name, expr->message.span));
            compile_expr(gc,
                         builder,
                         *expr->target,
                         /* tail_position */ false,
                         /* tail_call */ false);
            // INVOKE: <multimethod>, <num args>
            builder.emit_op(gc, invoke_op, /* stack_height_delta */ -1 + 1, _expr.span);
            builder.emit_arg(gc, r_existing);
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
            if (expr->messages.size() == 1 && !expr->target) {
                // Check for local mutable variables.
                const std::string& name = std::get<std::string>(expr->messages[0].value);
                const Binding* maybe_local = raise_upvar(gc, builder, name);
                if (maybe_local) {
                    const Binding& local = *maybe_local;
                    if (local._mutable) {
                        compile_expr(gc,
                                     builder,
                                     *expr->args[0],
                                     /* tail_position */ false,
                                     /* tail_call */ false);
                        // STORE_REF: <local index>
                        builder.emit_op(gc,
                                        OpCode::STORE_REF,
                                        /* stack_height_delta */ -1,
                                        _expr.span);
                        builder.emit_arg(gc, Value::fixnum(local.local_index));
                        // LOAD_VALUE: null
                        builder.emit_op(gc,
                                        OpCode::LOAD_VALUE,
                                        /* stack_height_delta */ +1,
                                        _expr.span);
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
                            compile_expr(gc,
                                         builder,
                                         *b->right,
                                         /* tail_position */ false,
                                         /* tail_call */ false);
                            // Then add the binding.
                            uint32_t local_index = builder.num_regs++;
                            builder.bindings[name] = Binding{
                                .name = name,
                                ._mutable = _mutable,
                                .local_index = local_index,
                            };
                            // Store the value (generated by the RHS generated code) into the newly
                            // allocated register.
                            if (_mutable) {
                                // INIT_REF: <local index>
                                builder.emit_op(gc,
                                                OpCode::INIT_REF,
                                                /* stack_height_delta */ -1,
                                                _expr.span);
                                builder.emit_arg(gc, Value::fixnum(local_index));
                            } else {
                                // STORE_REG: <local index>
                                builder.emit_op(gc,
                                                OpCode::STORE_REG,
                                                /* stack_height_delta */ -1,
                                                _expr.span);
                                builder.emit_arg(gc, Value::fixnum(local_index));
                            }
                            // LOAD:VALUE: null
                            builder.emit_op(gc,
                                            OpCode::LOAD_VALUE,
                                            /* stack_height_delta */ +1,
                                            _expr.span);
                            builder.emit_arg(gc, Value::null());
                            return;
                        }
                    }
                }
            }
            if (expr->messages.size() == 1 &&
                std::get<std::string>(expr->messages[0].value) == "TAIL-CALL") {
                if (expr->target) {
                    throw compile_error("TAIL-CALL: requires no target", expr->span);
                }
                if (!tail_position) {
                    throw compile_error("TAIL-CALL: invoked not in tail position", expr->span);
                }
                compile_expr(gc,
                             builder,
                             *expr->args[0],
                             /* tail_position */ tail_position,
                             /* tail_call */ true);
                return;
            }
            Value v_existing;
            LookupResult lookup;
            if ((lookup = lookup_name(builder, *r_name, &v_existing)) != SUCCESS) {
                if (lookup == NOT_FOUND) {
                    throw compile_error(
                        "name is not defined in module (and is also not <a mutable local>:)",
                        expr->span);
                } else {
                    throw compile_error("name is ambiguous in the current module and imports",
                                        expr->span);
                }
            }
            ValueRoot r_existing(gc, std::move(v_existing));

            if (expr->target) {
                compile_expr(gc,
                             builder,
                             *expr->target.value(),
                             /* tail_position */ false,
                             /* tail_call */ false);
            } else {
                // Load the default receiver, which is always register 0.
                // LOAD_REG: <local index>
                builder.emit_op(gc, OpCode::LOAD_REG, /* stack_height_delta */ +1, _expr.span);
                builder.emit_arg(gc, Value::fixnum(0));
            }
            for (const std::unique_ptr<Expr>& arg : expr->args) {
                compile_expr(gc, builder, *arg, /* tail_position */ false, /* tail_call */ false);
            }
            // INVOKE: <multimethod>, <num args>
            builder.emit_op(gc,
                            invoke_op,
                            /* stack_height_delta */ -(int64_t)expr->args.size(),
                            _expr.span);
            builder.emit_arg(gc, *r_existing);
            builder.emit_arg(gc, Value::fixnum(1 + expr->args.size()));
        } else if (ParenExpr* expr = dynamic_cast<ParenExpr*>(&_expr)) {
            compile_expr(gc, builder, *expr->inner, tail_position, tail_call);
        } else if (BlockExpr* expr = dynamic_cast<BlockExpr*>(&_expr)) {
            // Block with no parameters still has one implicit parameter: `it`.

            OptionalRoot<Vector> r_upreg_map(gc, make_vector(gc, 0));
            Root<Vector> r_insts(gc, make_vector(gc, 0));
            Root<Vector> r_args(gc, make_vector(gc, 0));
            Root<Vector> r_inst_spans(gc, make_vector(gc, 0));
            Root<Vector> r_upreg_loading(gc, make_vector(gc, 0));
            CodeBuilder closure_builder{
                .r_module = builder.r_module,
                .r_imports = builder.r_imports,
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
                .r_inst_spans = r_inst_spans,
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
            compile_expr(gc,
                         closure_builder,
                         *expr->body,
                         /* tail_position */ true,
                         /* tail_call */ false);
            ValueRoot r_closure_code(gc, Value::object(closure_builder.finalize(gc, expr->span)));
            uint64_t num_upreg_loads = closure_builder.r_upreg_loading->length;
            ASSERT(num_upreg_loads == closure_builder.r_upreg_map->length);
            for (uint64_t i = 0; i < num_upreg_loads; i++) {
                int64_t load_index =
                    closure_builder.r_upreg_loading->v_array.obj_array()->components()[i].fixnum();
                // TODO: check range
                // LOAD_REG: <local index>
                builder.emit_op(gc, OpCode::LOAD_REG, /* stack_height_delta */ +1, _expr.span);
                builder.emit_arg(gc, Value::fixnum(load_index));
            }
            builder.emit_op(gc,
                            OpCode::MAKE_CLOSURE,
                            /* stack_height_delta */ -(int64_t)num_upreg_loads + 1,
                            _expr.span);
            builder.emit_arg(gc, *r_closure_code);
        } else if (DataExpr* expr = dynamic_cast<DataExpr*>(&_expr)) {
            for (const std::unique_ptr<Expr>& component : expr->components) {
                compile_expr(gc,
                             builder,
                             *component,
                             /* tail_position */ false,
                             /* tail_call */ false);
            }
            // MAKE_VECTOR: <num components>
            builder.emit_op(gc,
                            OpCode::MAKE_VECTOR,
                            /* stack_height_delta */ -(int64_t)expr->components.size() + 1,
                            _expr.span);
            builder.emit_arg(gc, Value::fixnum(expr->components.size()));
        } else if (SequenceExpr* expr = dynamic_cast<SequenceExpr*>(&_expr)) {
            if (expr->components.empty()) {
                // Empty sequence -> just load null.
                // LOAD_VALUE: <value>
                builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1, _expr.span);
                builder.emit_arg(gc, Value::null());
                return;
            }

            // Drop all but the last component's result.
            for (size_t i = 0; i < expr->components.size(); i++) {
                bool last = i == expr->components.size() - 1;
                compile_expr(gc,
                             builder,
                             *expr->components[i],
                             /* tail_position */ tail_position && last,
                             /* tail_call */ false);
                if (!last) {
                    builder.emit_op(gc,
                                    OpCode::DROP,
                                    /* stack_height_delta */ -1,
                                    expr->components[i]->span);
                }
            }
        } else if (TupleExpr* expr = dynamic_cast<TupleExpr*>(&_expr)) {
            for (const std::unique_ptr<Expr>& component : expr->components) {
                compile_expr(gc,
                             builder,
                             *component,
                             /* tail_position */ false,
                             /* tail_call */ false);
            }
            // MAKE_TUPLE: <num components>
            builder.emit_op(gc,
                            OpCode::MAKE_TUPLE,
                            /* stack_height_delta */ -(int64_t)expr->components.size() + 1,
                            _expr.span);
            builder.emit_arg(gc, Value::fixnum(expr->components.size()));
        } else {
            ASSERT_MSG(false, "forgot an Expr subtype");
        }
    }

    // receiver, body, attrs are optional
    void compile_method(GC& gc, CodeBuilder& module_builder, const std::string& message,
                        SourceSpan& span, Expr* receiver, Expr& _decl, Expr* _body, Expr* attrs)
    {
        if (receiver) {
            std::stringstream ss;
            ss << message << " takes no receiver";
            throw compile_error(ss.str(), span);
        }

        Expr* decl = &_decl;

        while (ParenExpr* p = dynamic_cast<ParenExpr*>(decl)) {
            decl = p->inner.get();
        }

        std::vector<std::string> method_name_parts{};
        std::vector<std::string> param_names{};
        // Parameter matchers are evaluated at runtime later; this generates code to evaluate them.

        auto add_param_name_and_matcher =
            [&gc, &module_builder, &param_names](Expr& param_decl,
                                                 const std::string& error_msg) -> void {
            if (NameExpr* d = dynamic_cast<NameExpr*>(&param_decl)) {
                param_names.push_back(std::get<std::string>(d->name.value));
                // Add an any-matcher by loading null.
                // LOAD_VALUE: <value>
                module_builder.emit_op(gc,
                                       OpCode::LOAD_VALUE,
                                       /* stack_height_delta */ +1,
                                       d->span);
                module_builder.emit_arg(gc, Value::null());
                return;
            } else if (ParenExpr* d = dynamic_cast<ParenExpr*>(&param_decl)) {
                if (NAryMessageExpr* n = dynamic_cast<NAryMessageExpr*>(d->inner.get())) {
                    if (!n->target && n->messages.size() == 1) {
                        param_names.push_back(std::get<std::string>(n->messages[0].value));

                        // Add a type-matcher evaluated from n->args[0];
                        compile_expr(gc,
                                     module_builder,
                                     *n->args[0],
                                     /* tail_position */ false,
                                     /* tail_call */ false);
                        // We must ensure it's a Type at runtime.
                        module_builder.emit_op(gc,
                                               OpCode::VERIFY_IS_TYPE,
                                               /* stack_height_delta */ 0,
                                               n->args[0]->span);
                        return;
                    }
                }
            }
            throw compile_error(error_msg, param_decl.span);
        };

        bool unary;

        if (NameExpr* d = dynamic_cast<NameExpr*>(decl)) {
            unary = true;
            method_name_parts.push_back(std::get<std::string>(d->name.value));
            param_names.push_back("self");
            // Add an any-matcher by loading null.
            // LOAD_VALUE: <value>
            module_builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1, d->span);
            module_builder.emit_arg(gc, Value::null());
        } else if (UnaryMessageExpr* d = dynamic_cast<UnaryMessageExpr*>(decl)) {
            unary = true;
            std::stringstream ss;
            ss << "When the " << message << " 'declaration' argument is a unary message, "
               << "it must be a simple unary message of the form [target-name message-name] "
               << "or else a unary message of the form [(target-name: matcher) message-name]";
            const std::string& error_msg = ss.str();
            method_name_parts.push_back(std::get<std::string>(d->message.value));
            add_param_name_and_matcher(*d->target, error_msg);
        } else if (NAryMessageExpr* d = dynamic_cast<NAryMessageExpr*>(decl)) {
            unary = false;
            std::stringstream ss;
            ss << "When the " << message << " 'declaration' argument is an n-ary message, it must"
               << "be a simple n-ary message of the form [target-name message: param-name ...] "
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
                // Add an any-matcher by loading null.
                // LOAD_VALUE: <value>
                module_builder.emit_op(gc,
                                       OpCode::LOAD_VALUE,
                                       /* stack_height_delta */ +1,
                                       d->span);
                module_builder.emit_arg(gc, Value::null());
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

        Root<String> r_method_name(gc,
                                   unary ? make_string(gc, method_name_parts[0])
                                         : concat_with_suffix(gc, method_name_parts, ":"));

        Expr* body;
        if (_body) {
            if (BlockExpr* b = dynamic_cast<BlockExpr*>(_body)) {
                if (!b->parameters.empty()) {
                    std::stringstream ss;
                    ss << message << " 'body' argument should not specify any parameters";
                    throw compile_error(ss.str(), _body->span);
                }
                body = b->body.get();
            } else {
                std::stringstream ss;
                ss << message << " 'body' argument should be a block";
                throw compile_error(ss.str(), _body->span);
            }
        } else {
            body = nullptr;
        }

        MultiMethod* multimethod;
        {
            Value existing;
            LookupResult lookup = lookup_name(module_builder, *r_method_name, &existing);
            if (lookup == SUCCESS) {
                if (existing.is_obj_multimethod()) {
                    if (!body) {
                        throw compile_error("multimethod is already defined in the current context",
                                            span);
                    }
                    multimethod = existing.obj_multimethod();
                } else {
                    throw compile_error("method name is already defined in the current context, "
                                        "but not as a multimethod",
                                        span);
                }
            } else if (lookup == NOT_FOUND) {
                // We know we're about to add at least one method!
                Root<Vector> r_methods(gc, make_vector(gc, 1));
                Root<Vector> r_attributes(gc, make_vector(gc, 0));
                multimethod = make_multimethod(gc,
                                               r_method_name,
                                               param_names.size(),
                                               r_methods,
                                               r_attributes);
                ValueRoot r_multimethod(gc, Value::object(multimethod));
                ValueRoot r_key(gc, r_method_name.value());
                append(gc, module_builder.r_module, r_key, r_multimethod);
                multimethod = r_multimethod->obj_multimethod();
            } else {
                throw compile_error(
                    "ambiguous lookup for multimethod name in module and its current imports",
                    span);
            }
        }

        if (!body) {
            // Early exit; all we needed to do was make a multimethod.
            return;
        }

        Root<MultiMethod> r_multimethod(gc, std::move(multimethod));

        // Compile the body.
        OptionalRoot<Vector> r_upreg_map(gc, nullptr); // not a closure!
        Root<Vector> r_insts(gc, make_vector(gc, 0));
        Root<Vector> r_args(gc, make_vector(gc, 0));
        Root<Vector> r_inst_spans(gc, make_vector(gc, 0));
        Root<Vector> r_upreg_loading(gc, make_vector(gc, 0));
        CodeBuilder builder{
            .r_module = module_builder.r_module,
            .r_imports = module_builder.r_imports,
            .num_params = (uint32_t)param_names.size(), // TODO: check size_t?
            .num_regs = (uint32_t)param_names.size(),   // TODO: check size_t?
            .num_data = 0,
            .r_upreg_map = r_upreg_map,
            .r_insts = r_insts,
            .r_args = r_args,
            .r_inst_spans = r_inst_spans,
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
        compile_expr(gc, builder, *body, /* tail_position */ true, /* tail_call */ false);

        // Generate the method. This has to be at runtime, since parameter matchers are evaluated at
        // runtime. At this point, the matchers have been calculated and are at the top of the data
        // stack. The make-method invocation takes method fields in this order:
        // - (default receiver, ignored)
        // - param matchers array
        // - return type
        // - code
        // - attributes
        // (It does not take a native or intrinsic handler.)
        // We are effectively compiling:
        //     (
        //         <matchers> make-method-with-return-type: null code: <code> attrs: <attrs>
        //     ) add-method-to: <multimethod> require-unique: true
        // Where the <attrs> default to {} if not provided.

        // Parameter matchers:
        // MAKE_ARRAY: <length>
        module_builder.emit_op(gc,
                               OpCode::MAKE_ARRAY,
                               /* stack_height_delta */ -(int64_t)param_names.size() + 1,
                               decl->span);
        module_builder.emit_arg(gc, Value::fixnum(param_names.size()));

        // Return type: (TODO: support return types for methods)
        // LOAD_VALUE: <value>
        module_builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1, span);
        module_builder.emit_arg(gc, Value::null());

        // Code:
        // LOAD_VALUE: <value>
        Root<Code> r_code(gc, builder.finalize(gc, span));
        module_builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1, body->span);
        module_builder.emit_arg(gc, r_code.value());

        // Attributes:
        if (attrs) {
            compile_expr(gc,
                         module_builder,
                         *attrs,
                         /* tail_position */ false,
                         /* tail_call */ false);
        } else {
            // Generate a 0-length vector.
            // MAKE_VECTOR: <length>
            module_builder.emit_op(gc, OpCode::MAKE_VECTOR, /* stack_height_delta */ +1, span);
            module_builder.emit_arg(gc, Value::fixnum(0));
        }

        // Create the method.
        // INVOKE: <multimethod>, <num args>
        module_builder.emit_op(gc, OpCode::INVOKE, /* stack_height_delta */ -4 + 1, span);
        {
            Root<String> r_name(gc, make_string(gc, "make-method-with-return-type:code:attrs:"));
            module_builder.emit_arg(gc, lookup_name(module_builder, *r_name, span));
        }
        module_builder.emit_arg(gc, Value::fixnum(4));

        // Multimethod:
        // LOAD_VALUE: <value>
        module_builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1, decl->span);
        module_builder.emit_arg(gc, r_multimethod.value());

        // Require unique = true
        // TODO: allow user to specify redefinition?
        // LOAD_VALUE: <value>
        module_builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1, span);
        module_builder.emit_arg(gc, Value::_bool(true));

        // Add the method.
        // INVOKE: <multimethod>, <num args>
        module_builder.emit_op(gc, OpCode::INVOKE, /* stack_height_delta */ -3 + 1, span);
        {
            Root<String> r_name(gc, make_string(gc, "add-method-to:require-unique:"));
            module_builder.emit_arg(gc, lookup_name(module_builder, *r_name, span));
        }
        module_builder.emit_arg(gc, Value::fixnum(3));
    }

    void aggregate_slots(GC& gc, Root<Vector>& r_slots, Type* type)
    {
        Root<Type> r_type(gc, std::move(type));

        Root<Array> r_bases(gc, r_type->v_bases.obj_array());
        uint64_t num_bases = r_bases->length;
        for (uint64_t i = 0; i < num_bases; i++) {
            Value v_base = r_bases->components()[i];
            ASSERT(v_base.is_obj_type());
            Type* base = v_base.obj_type();
            if (base->kind == Type::Kind::DATACLASS) {
                aggregate_slots(gc, r_slots, base);
                // `[v_]base` must be assumed invalidated.
            }
        }

        ASSERT(r_type->v_slots.is_obj_array());
        Root<Array> r_type_slots(gc, r_type->v_slots.obj_array());
        uint64_t num_slots = r_type_slots->length;
        for (uint64_t i = 0; i < num_slots; i++) {
            Value slot = r_type_slots->components()[i];
            ValueRoot r_slot(gc, std::move(slot));
            append(gc, r_slots, r_slot);
        }
    }

    // receiver, extends are optional
    void compile_dataclass(GC& gc, Root<Assoc>& r_module, Root<Vector>& r_imports,
                           const std::string& message, SourceSpan& span, Expr* receiver, Expr& name,
                           Expr* extends, Expr& has)
    {
        if (receiver) {
            std::stringstream ss;
            ss << message << " takes no receiver";
            throw compile_error(ss.str(), span);
        }

        NameExpr* name_expr = dynamic_cast<NameExpr*>(&name);
        if (!name_expr) {
            std::stringstream ss;
            ss << message << " 'name' argument must be a name";
            throw compile_error(ss.str(), name.span);
        }
        const std::string& class_name = std::get<std::string>(name_expr->name.value);
        Root<String> r_class_name(gc, make_string(gc, class_name));
        LookupResult lookup = lookup_name(*r_module, *r_imports, *r_class_name);
        if (lookup == SUCCESS || lookup == AMBIGUOUS) {
            std::stringstream ss;
            ss << message << " class name '" << class_name
               << "' already exists in module scope or in imports";
            throw compile_error(ss.str(), name.span);
        }

        Root<Vector> r_extends(gc, make_vector(gc, 0));
        if (extends) {
            bool saw_dataclass = false;
            DataExpr* data_expr = dynamic_cast<DataExpr*>(extends);
            if (!data_expr) {
                std::stringstream ss;
                ss << message << " 'extends' argument must be a vector of names";
                throw compile_error(ss.str(), extends->span);
            }
            for (std::unique_ptr<Expr>& base_expr : data_expr->components) {
                NameExpr* base_name_expr = dynamic_cast<NameExpr*>(base_expr.get());
                if (!base_name_expr) {
                    std::stringstream ss;
                    ss << message << " 'extends' argument must be a sequence of names";
                    throw compile_error(ss.str(), base_expr->span);
                }
                const std::string& base_name = std::get<std::string>(base_name_expr->name.value);
                Root<String> r_base_name(gc, make_string(gc, base_name));
                Value lookup = lookup_name(*r_module, *r_imports, *r_base_name, base_expr->span);
                if (!lookup.is_obj_type()) {
                    std::stringstream ss;
                    ss << "Value '" << base_name << "' must be a Type";
                    throw compile_error(ss.str(), base_expr->span);
                }

                Type* base = lookup.obj_type();
                if (base->sealed) {
                    std::stringstream ss;
                    ss << "Cannot extend from sealed type '" << base_name << "'";
                    throw compile_error(ss.str(), base_expr->span);
                }
                // TODO: This feels a bit hacky. Better way? Maybe separate args.
                if (base->kind == Type::Kind::DATACLASS) {
                    if (saw_dataclass) {
                        throw compile_error("Cannot extend from multiple dataclasses",
                                            base_expr->span);
                    }
                    saw_dataclass = true;
                }
                ValueRoot r_base(gc, Value::object(base));
                append(gc, r_extends, r_base);
            }
        }
        Root<Array> r_bases(gc, vector_to_array(gc, r_extends));
        Type* base_dataclass = nullptr;
        for (Value base : r_bases) {
            if (base.obj_type()->kind == Type::Kind::DATACLASS) {
                base_dataclass = base.obj_type();
                break;
            }
        }
        OptionalRoot<Type> r_base_dataclass(gc, std::move(base_dataclass));

        // All slots, including those derived from base dataclasses.
        Root<Vector> r_all_slots(gc, make_vector(gc, 0));
        uint32_t num_base_slots = 0;
        // Extra slots on top of those from base dataclasses.
        Root<Vector> r_leaf_slots(gc, make_vector(gc, 0));
        // Collect slots from the base dataclass, if it exists.
        if (r_base_dataclass) {
            aggregate_slots(gc, r_all_slots, *r_base_dataclass);
            num_base_slots = r_all_slots->length;
        }
        // Collect slots from the direct dataclass definition.
        {
            DataExpr* data_expr = dynamic_cast<DataExpr*>(&has);
            if (!data_expr) {
                std::stringstream ss;
                ss << message << " 'has' argument must be a vector of names";
                throw compile_error(ss.str(), extends->span);
            }
            for (std::unique_ptr<Expr>& slot_expr : data_expr->components) {
                NameExpr* slot_name_expr = dynamic_cast<NameExpr*>(slot_expr.get());
                if (!slot_name_expr) {
                    std::stringstream ss;
                    ss << message << " 'has' argument must be a sequence of names";
                    throw compile_error(ss.str(), slot_expr->span);
                }
                const std::string& slot_name = std::get<std::string>(slot_name_expr->name.value);
                ValueRoot r_slot_name(gc, Value::object(make_string(gc, slot_name)));
                append(gc, r_all_slots, r_slot_name);
                append(gc, r_leaf_slots, r_slot_name);
            }
        }
        // TODO: warn (or error) if there's a leaf slot shadowing a derived slot.

        OptionalRoot<Array> r_slots(gc, vector_to_array(gc, r_leaf_slots));
        Root<Type> r_type(gc,
                          make_type(gc,
                                    r_class_name,
                                    r_bases,
                                    /* sealed */ false,
                                    Type::Kind::DATACLASS,
                                    r_slots,
                                    /* num_total_slots */ r_all_slots->length));
        ValueRoot rv_type(gc, r_type.value());
        ValueRoot r_key(gc, r_class_name.value());
        append(gc, r_module, r_key, rv_type);

        const auto lookup_or_create =
            [&gc, &r_module, &r_imports](Root<String>& r_name,
                                         uint32_t num_params,
                                         SourceSpan err_span) -> MultiMethod* {
            Value existing;
            LookupResult lookup = lookup_name(*r_module, *r_imports, *r_name, &existing);
            if (lookup == SUCCESS) {
                if (existing.is_obj_multimethod()) {
                    return existing.obj_multimethod();
                } else {
                    std::stringstream ss;
                    ss << "'" << native_str(*r_name)
                       << "' is already defined in module, but is not a multimethod";
                    throw compile_error(ss.str(), err_span);
                }
            } else if (lookup == NOT_FOUND) {
                Root<Vector> r_methods(gc, make_vector(gc, 1));
                Root<Vector> r_attributes(gc, make_vector(gc, 0));
                ValueRoot r_multimethod(
                    gc,
                    Value::object(
                        make_multimethod(gc, r_name, num_params, r_methods, r_attributes)));
                ValueRoot r_key(gc, r_name.value());
                append(gc, r_module, r_key, r_multimethod);
                return r_multimethod->obj_multimethod();
            } else {
                std::stringstream ss;
                ss << "'" << native_str(*r_name) << "' is ambiguous in module and its imports";
                throw compile_error(ss.str(), err_span);
            }
        };

        // Add a predicate method `<class_name>?`.
        // Implementation:
        //   load_reg @0
        //   load_value <the dataclass>
        //   invoke "instance?:" 2
        {
            Root<String> r_method_name(gc, concat(gc, r_class_name, "?"));
            Root<MultiMethod> r_multimethod(
                gc,
                lookup_or_create(r_method_name, /* num_params */ 1, name.span));

            OptionalRoot<Vector> r_upreg_map(gc, nullptr); // not a closure!
            Root<Vector> r_insts(gc, make_vector(gc, 0));
            Root<Vector> r_args(gc, make_vector(gc, 0));
            Root<Vector> r_inst_spans(gc, make_vector(gc, 0));
            Root<Vector> r_upreg_loading(gc, make_vector(gc, 0));
            CodeBuilder builder{
                .r_module = r_module,
                .r_imports = r_imports,
                // Just the default receiver.
                .num_params = 1,
                .num_regs = 1,
                .num_data = 0,
                .r_upreg_map = r_upreg_map,
                .r_insts = r_insts,
                .r_args = r_args,
                .r_inst_spans = r_inst_spans,
                .r_upreg_loading = r_upreg_loading,
                .bindings = {},
                .base = nullptr,
            };
            // LOAD_REG: <local index>
            builder.emit_op(gc, OpCode::LOAD_REG, /* stack_height_delta */ +1, name.span);
            builder.emit_arg(gc, Value::fixnum(0));
            // LOAD_VALUE: <value>
            builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1, name.span);
            builder.emit_arg(gc, rv_type);
            // INVOKE: <multimethod>, <num args>
            builder.emit_op(gc, OpCode::INVOKE, /* stack_height_delta */ -2 + 1, name.span);
            {
                Root<String> r_name(gc, make_string(gc, "instance?:"));
                builder.emit_arg(gc, lookup_name(builder, *r_name, span));
            }
            builder.emit_arg(gc, Value::fixnum(2));

            Root<Array> r_param_matchers(gc, make_array(gc, 1));
            r_param_matchers->components()[0] = Value::null(); // 'any' matcher
            OptionalRoot<Type> r_return_type(gc, nullptr);     // TODO: return type
            OptionalRoot<Code> r_code(gc, builder.finalize(gc, name.span));
            Root<Vector> r_attributes(gc, make_vector(gc, 0));
            Root<Method> r_method(gc,
                                  make_method(gc,
                                              r_param_matchers,
                                              r_return_type,
                                              r_code,
                                              r_attributes,
                                              /* native_handler */ nullptr,
                                              /* intrinsic_handler */ nullptr));
            add_method(gc, r_multimethod, r_method, /* require_unique */ true);
        }

        // Add a constructor method.
        // Implementation:
        //   load_reg @0
        //   ...
        //   load_reg @<full slot count>
        //   make_instance <full slot count>
        {
            uint32_t num_slots = r_all_slots->length;
            Root<String> r_method_name(gc,
                                       r_all_slots->length > 0
                                           ? concat_with_suffix(gc, r_all_slots, ":")
                                           : make_string(gc, "new"));
            Root<MultiMethod> r_multimethod(
                gc,
                lookup_or_create(r_method_name, /* num_params */ 1 + num_slots, name.span));

            Root<Vector> r_imports(gc, make_vector(gc, 0)); // not actually used in this case
            OptionalRoot<Vector> r_upreg_map(gc, nullptr);  // not a closure!
            Root<Vector> r_insts(gc, make_vector(gc, 0));
            Root<Vector> r_args(gc, make_vector(gc, 0));
            Root<Vector> r_inst_spans(gc, make_vector(gc, 0));
            Root<Vector> r_upreg_loading(gc, make_vector(gc, 0));
            CodeBuilder builder{
                .r_module = r_module,
                .r_imports = r_imports,
                .num_params = 1 + num_slots,
                .num_regs = 1 + num_slots,
                .num_data = 0,
                .r_upreg_map = r_upreg_map,
                .r_insts = r_insts,
                .r_args = r_args,
                .r_inst_spans = r_inst_spans,
                .r_upreg_loading = r_upreg_loading,
                .bindings = {},
                .base = nullptr,
            };
            for (uint32_t i = 0; i <= num_slots; i++) {
                // LOAD_REG: <local index>
                builder.emit_op(gc, OpCode::LOAD_REG, /* stack_height_delta */ +1, span);
                builder.emit_arg(gc, Value::fixnum(i));
            }
            // MAKE_INSTANCE: <num slots>
            builder.emit_op(gc,
                            OpCode::MAKE_INSTANCE,
                            /* stack_height_delta */ -1 - (int64_t)num_slots + 1,
                            span);
            builder.emit_arg(gc, Value::fixnum(num_slots));

            Root<Array> r_param_matchers(gc, make_array(gc, 1 + num_slots));
            r_param_matchers->components()[0] =
                Value::object(make_ref(gc, rv_type)); // value matcher on the dataclass
            // TODO: support types for slots. For now, the default 'null' value works for the rest
            // of the matchers: 'any' matcher.
            OptionalRoot<Type> r_return_type(gc, *r_type);
            OptionalRoot<Code> r_code(gc, builder.finalize(gc, span));
            Root<Vector> r_attributes(gc, make_vector(gc, 0));
            Root<Method> r_method(gc,
                                  make_method(gc,
                                              r_param_matchers,
                                              r_return_type,
                                              r_code,
                                              r_attributes,
                                              /* native_handler */ nullptr,
                                              /* intrinsic_handler */ nullptr));
            add_method(gc, r_multimethod, r_method, /* require_unique */ true);
        }

        uint32_t num_leaf_slots = r_leaf_slots->length;
        for (uint32_t i = 0; i < num_leaf_slots; i++) {
            Root<String> r_slot(gc,
                                r_leaf_slots->v_array.obj_array()->components()[i].obj_string());

            // Add a getter method.
            // Implementation:
            //   load_reg @0
            //   get_slot <slot index>
            // TODO: use slot-specific span for opcodes.
            {
                Root<String> r_method_name(gc, concat(gc, ".", r_slot));
                Root<MultiMethod> r_multimethod(
                    gc,
                    lookup_or_create(r_method_name, /* num_params */ 1, name.span));

                Root<Vector> r_imports(gc, make_vector(gc, 0)); // not actually used in this case
                OptionalRoot<Vector> r_upreg_map(gc, nullptr);  // not a closure!
                Root<Vector> r_insts(gc, make_vector(gc, 0));
                Root<Vector> r_args(gc, make_vector(gc, 0));
                Root<Vector> r_inst_spans(gc, make_vector(gc, 0));
                Root<Vector> r_upreg_loading(gc, make_vector(gc, 0));
                CodeBuilder builder{
                    .r_module = r_module,
                    .r_imports = r_imports,
                    .num_params = 1,
                    .num_regs = 1,
                    .num_data = 0,
                    .r_upreg_map = r_upreg_map,
                    .r_insts = r_insts,
                    .r_args = r_args,
                    .r_inst_spans = r_inst_spans,
                    .r_upreg_loading = r_upreg_loading,
                    .bindings = {},
                    .base = nullptr,
                };
                // LOAD_REG: <local index>
                builder.emit_op(gc, OpCode::LOAD_REG, /* stack_height_delta */ +1, span);
                builder.emit_arg(gc, Value::fixnum(0));
                // GET_SLOT: <slot index>
                builder.emit_op(gc, OpCode::GET_SLOT, /* stack_height_delta */ 0, span);
                builder.emit_arg(gc, Value::fixnum(num_base_slots + i));

                Root<Array> r_param_matchers(gc, make_array(gc, 1));
                r_param_matchers->components()[0] = r_type.value(); // type matcher on the dataclass
                OptionalRoot<Type> r_return_type(gc, nullptr);      // TODO: support slot types
                OptionalRoot<Code> r_code(gc, builder.finalize(gc, span));
                Root<Vector> r_attributes(gc, make_vector(gc, 0));
                Root<Method> r_method(gc,
                                      make_method(gc,
                                                  r_param_matchers,
                                                  r_return_type,
                                                  r_code,
                                                  r_attributes,
                                                  /* native_handler */ nullptr,
                                                  /* intrinsic_handler */ nullptr));
                add_method(gc, r_multimethod, r_method, /* require_unique */ true);
            }

            // Add a setter method.
            // Implementation:
            //   load_reg @0
            //   load_reg @1
            //   set_slot <slot index>
            //   load_reg @0    (... I guess ...)
            // TODO: use slot-specific span for opcodes.
            {
                Root<String> r_method_name(gc, concat(gc, r_slot, ":"));
                Root<MultiMethod> r_multimethod(
                    gc,
                    lookup_or_create(r_method_name, /* num_params */ 2, name.span));

                Root<Vector> r_imports(gc, make_vector(gc, 0)); // not actually used in this case
                OptionalRoot<Vector> r_upreg_map(gc, nullptr);  // not a closure!
                Root<Vector> r_insts(gc, make_vector(gc, 0));
                Root<Vector> r_args(gc, make_vector(gc, 0));
                Root<Vector> r_inst_spans(gc, make_vector(gc, 0));
                Root<Vector> r_upreg_loading(gc, make_vector(gc, 0));
                CodeBuilder builder{
                    .r_module = r_module,
                    .r_imports = r_imports,
                    .num_params = 2,
                    .num_regs = 2,
                    .num_data = 0,
                    .r_upreg_map = r_upreg_map,
                    .r_insts = r_insts,
                    .r_args = r_args,
                    .r_inst_spans = r_inst_spans,
                    .r_upreg_loading = r_upreg_loading,
                    .bindings = {},
                    .base = nullptr,
                };
                // LOAD_REG: <local index>
                builder.emit_op(gc, OpCode::LOAD_REG, /* stack_height_delta */ +1, span);
                builder.emit_arg(gc, Value::fixnum(0));
                // LOAD_REG: <local index>
                builder.emit_op(gc, OpCode::LOAD_REG, /* stack_height_delta */ +1, span);
                builder.emit_arg(gc, Value::fixnum(1));
                // GET_SLOT: <slot index>
                builder.emit_op(gc, OpCode::SET_SLOT, /* stack_height_delta */ -2, span);
                builder.emit_arg(gc, Value::fixnum(num_base_slots + i));
                // LOAD_REG: <local index>
                builder.emit_op(gc, OpCode::LOAD_REG, /* stack_height_delta */ +1, span);
                builder.emit_arg(gc, Value::fixnum(0));

                Root<Array> r_param_matchers(gc, make_array(gc, 2));
                r_param_matchers->components()[0] = r_type.value(); // type matcher on the dataclass
                r_param_matchers->components()[1] =
                    Value::null();                             // 'any' matcher (TODO: slot types)
                OptionalRoot<Type> r_return_type(gc, nullptr); // TODO: return type
                OptionalRoot<Code> r_code(gc, builder.finalize(gc, span));
                Root<Vector> r_attributes(gc, make_vector(gc, 0));
                Root<Method> r_method(gc,
                                      make_method(gc,
                                                  r_param_matchers,
                                                  r_return_type,
                                                  r_code,
                                                  r_attributes,
                                                  /* native_handler */ nullptr,
                                                  /* intrinsic_handler */ nullptr));
                add_method(gc, r_multimethod, r_method, /* require_unique */ true);
            }
        }
    }

    void compile_mixin(GC& gc, Root<Assoc>& r_module, Root<Vector>& r_imports,
                       const std::string& message, SourceSpan& span, Expr* receiver, Expr& name)
    {
        if (receiver) {
            std::stringstream ss;
            ss << message << " takes no receiver";
            throw compile_error(ss.str(), span);
        }

        NameExpr* name_expr = dynamic_cast<NameExpr*>(&name);
        if (!name_expr) {
            std::stringstream ss;
            ss << message << " 'name' argument must be a name";
            throw compile_error(ss.str(), name.span);
        }
        const std::string& mixin_name = std::get<std::string>(name_expr->name.value);
        Root<String> r_mixin_name(gc, make_string(gc, mixin_name));
        LookupResult lookup = lookup_name(*r_module, *r_imports, *r_mixin_name);
        if (lookup == SUCCESS || lookup == AMBIGUOUS) {
            std::stringstream ss;
            ss << message << " mixin name '" << mixin_name
               << "' already exists in module scope or in imports";
            throw compile_error(ss.str(), name.span);
        }

        // TODO: allow inheritance?
        // TODO: allow specifying required methods? (i.e. checker for when we are later mixing-in)
        OptionalRoot<Array> r_slots(gc, nullptr);
        Root<Array> r_bases(gc, make_array(gc, 0));
        Root<Type> r_type(gc,
                          make_type(gc,
                                    r_mixin_name,
                                    r_bases,
                                    /* sealed */ false,
                                    Type::Kind::MIXIN,
                                    r_slots,
                                    /* num_total_slots */ std::nullopt));
        ValueRoot rv_type(gc, r_type.value());
        ValueRoot r_key(gc, r_mixin_name.value());
        append(gc, r_module, r_key, rv_type);
    }

    Code* compile_into_module(VM& vm, Root<Assoc>& r_module, Root<Vector>& r_imports,
                              SourceSpan& span,
                              std::vector<std::unique_ptr<Expr>>& module_top_level_exprs)
    {
        // TODO: for future -- first find all multimethod definitions, add them to module (with zero
        // methods defined), and then go and compile everything.
        GC& gc = vm.gc;

        OptionalRoot<Vector> r_upreg_map(gc, nullptr); // not a closure!
        Root<Vector> r_insts(gc, make_vector(gc, 0));
        Root<Vector> r_args(gc, make_vector(gc, 0));
        Root<Vector> r_inst_spans(gc, make_vector(gc, 0));
        Root<Vector> r_upreg_loading(gc, make_vector(gc, 0));
        CodeBuilder builder{
            .r_module = r_module,
            .r_imports = r_imports,
            .num_params = 0,
            .num_regs = 0,
            .num_data = 0,
            .r_upreg_map = r_upreg_map,
            .r_insts = r_insts,
            .r_args = r_args,
            .r_inst_spans = r_inst_spans,
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
            // TODO: handle let:do:[::] as a builtin, looked up in module.
            if (NAryMessageExpr* expr = dynamic_cast<NAryMessageExpr*>(top_level_expr.get())) {
                if (expr->messages.size() == 2 &&
                    std::get<std::string>(expr->messages[0].value) == "let" &&
                    std::get<std::string>(expr->messages[1].value) == "do") {
                    compile_method(gc,
                                   builder,
                                   "let:do:",
                                   expr->span,
                                   expr->target ? expr->target->get() : nullptr,
                                   *expr->args[0],
                                   expr->args[1].get(),
                                   nullptr);
                    continue;
                } else if (expr->messages.size() == 3 &&
                           std::get<std::string>(expr->messages[0].value) == "let" &&
                           std::get<std::string>(expr->messages[1].value) == "do" &&
                           std::get<std::string>(expr->messages[2].value) == ":"

                ) {
                    compile_method(gc,
                                   builder,
                                   "let:do:::",
                                   expr->span,
                                   expr->target ? expr->target->get() : nullptr,
                                   *expr->args[0],
                                   expr->args[1].get(),
                                   expr->args[2].get());
                    continue;
                } else if (expr->messages.size() == 1 &&
                           std::get<std::string>(expr->messages[0].value) == "generic") {
                    compile_method(gc,
                                   builder,
                                   "generic:",
                                   expr->span,
                                   expr->target ? expr->target->get() : nullptr,
                                   *expr->args[0],
                                   nullptr,
                                   nullptr);
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
                                compile_expr(gc,
                                             builder,
                                             *b->right,
                                             /* tail_position */ false,
                                             /* tail_call */ false);
                                // Then add the (module) binding.
                                // TODO: the module append should be done by the compiled code.
                                // also don't make it a Ref, this is more like a module level
                                // `mut:`.
                                ValueRoot r_ref(gc, Value::null());
                                ValueRoot r_init(gc, Value::object(make_ref(gc, r_ref)));
                                ValueRoot r_key(gc, r_name.value());
                                append(gc, r_module, r_key, r_init);
                                // STORE_MODULE: <ref value>
                                builder.emit_op(gc,
                                                OpCode::STORE_MODULE,
                                                /* stack_height_delta */ -1,
                                                n->name.span);
                                builder.emit_arg(gc, r_init);
                                // LOAD:VALUE: null
                                builder.emit_op(gc,
                                                OpCode::LOAD_VALUE,
                                                /* stack_height_delta */ +1,
                                                n->name.span);
                                builder.emit_arg(gc, Value::null());
                                continue;
                            }
                        }
                    }
                } else if (expr->messages.size() == 2 &&
                           std::get<std::string>(expr->messages[0].value) == "data" &&
                           std::get<std::string>(expr->messages[1].value) == "has") {
                    compile_dataclass(gc,
                                      r_module,
                                      r_imports,
                                      "data:extends:has:",
                                      expr->span,
                                      expr->target ? expr->target->get() : nullptr,
                                      *expr->args[0],
                                      nullptr,
                                      *expr->args[1]);
                    continue;
                } else if (expr->messages.size() == 3 &&
                           std::get<std::string>(expr->messages[0].value) == "data" &&
                           std::get<std::string>(expr->messages[1].value) == "extends" &&
                           std::get<std::string>(expr->messages[2].value) == "has") {
                    compile_dataclass(gc,
                                      r_module,
                                      r_imports,
                                      "data:extends:has:",
                                      expr->span,
                                      expr->target ? expr->target->get() : nullptr,
                                      *expr->args[0],
                                      expr->args[1].get(),
                                      *expr->args[2]);
                    continue;
                } else if (expr->messages.size() == 1 &&
                           std::get<std::string>(expr->messages[0].value) == "mixin") {
                    compile_mixin(gc,
                                  r_module,
                                  r_imports,
                                  "mixin:",
                                  expr->span,
                                  expr->target ? expr->target->get() : nullptr,
                                  *expr->args[0]);
                    continue;
                } else if (expr->messages.size() == 1 &&
                           std::get<std::string>(expr->messages[0].value) ==
                               "IMPORT-EXISTING-MODULE") {
                    if (expr->target) {
                        throw compile_error("IMPORT-EXISTING-MODULE: requires no target",
                                            expr->span);
                    }
                    if (LiteralExpr* l = dynamic_cast<LiteralExpr*>(expr->args[0].get())) {
                        const std::string* maybe_module_name =
                            std::get_if<std::string>(&l->literal.value);
                        if (maybe_module_name) {
                            const std::string& module_name = *maybe_module_name;
                            String* name = make_string(gc, module_name);
                            Value* maybe_module = assoc_lookup(vm.modules(), name);
                            if (!maybe_module) {
                                throw compile_error(
                                    "IMPORT-EXISTING-MODULE: could not find existing module",
                                    expr->span);
                            }
                            Value module = *maybe_module;
                            ValueRoot r_module(vm.gc, std::move(module));
                            append(vm.gc, r_imports, r_module);
                            continue;
                        } else {
                            throw compile_error("IMPORT-EXISTING-MODULE: requires a literal string",
                                                expr->span);
                        }
                    } else {
                        throw compile_error("IMPORT-EXISTING-MODULE: requires a literal string",
                                            expr->span);
                    }
                }
            }
            compile_expr(gc,
                         builder,
                         *top_level_expr,
                         /* tail_position */ false,
                         /* tail_call */ false);
        }

        // If the top level expressions were all definitions, there may be no code to execute (and
        // to get a value from). In this case, just add stub code to load null.
        if (builder.r_insts->length == 0) {
            // LOAD_VALUE: null
            builder.emit_op(gc, OpCode::LOAD_VALUE, /* stack_height_delta */ +1, span);
            builder.emit_arg(gc, Value::null());
        }

        return builder.finalize(gc, span);
    }
};

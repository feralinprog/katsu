from dataclasses import dataclass
from parser import (
    BinaryOpExpr,
    DataExpr,
    Expr,
    LiteralExpr,
    NameExpr,
    NAryMessageExpr,
    ParenExpr,
    SequenceExpr,
    TupleExpr,
    UnaryMessageExpr,
    UnaryOpExpr,
)
from typing import Optional, Tuple, Union

from interpreter import (
    BytecodeOp,
    BytecodeSequence,
    CompiledBody,
    CompileTimeHandler,
    Context,
    IntrinsicMethodBody,
    JumpIndex,
    Method,
    MultiMethod,
    NativeMethodBody,
    NumberValue,
    ParameterAnyMatcher,
    ParameterTypeMatcher,
    ParameterValueMatcher,
    QuoteExpr,
    QuoteMethodBody,
    QuoteValue,
    StringValue,
    SymbolValue,
    Value,
)
from lexer import TokenType
from span import SourceSpan


@dataclass
class UndeterminedValue:
    # For debug only.
    name: str


@dataclass
class UndeterminedMethod:
    # For debug only.
    name: str


@dataclass
class CompilationContext:
    slots: dict[str, Union[Value, MultiMethod, UndeterminedValue, UndeterminedMethod]]
    base: Optional[Union[Context, "CompilationContext"]]

    def lookup(
        self, slot: str
    ) -> Optional[Union[Value, MultiMethod, UndeterminedValue, UndeterminedMethod]]:
        ctxt = self
        while ctxt:
            if slot in ctxt.slots:
                return ctxt.slots[slot]
            ctxt = ctxt.base
        return None


def should_inline_multimethod_dispatch(multimethod: MultiMethod) -> bool:
    # TODO: Inline also if heuristics suggest it should be inlined.
    return multimethod.inline_dispatch


def should_inline_quote_method(parent: MultiMethod, method: Method) -> bool:
    # TODO: Also inline if heuristics suggest it should be inlined.
    # TODO: support inlining after figuring out how to handle recursive methods.
    return False
    return method.inline


# Compiler for a specific method / quote body in a particular CompilationContext.
class Compiler:
    ctxt: CompilationContext
    sequence: BytecodeSequence
    # List of multimethods which, if changed, invalidate the compilation.
    multimethod_deps: list[MultiMethod]
    # Stack of quote-method-bodies we have inlined so far; when inlining a new quote body, a new
    # Compiler is produced with one more entry on this stack. Each method has associated the
    # bytecode index of the inlined-multimethod starting point.
    inlining_stack: list[Tuple[QuoteMethodBody, int]]

    # Internal use only.
    def __init__(self, ctxt, sequence, multimethod_deps, inlining_stack):
        self.ctxt = ctxt
        self.sequence = sequence
        self.multimethod_deps = multimethod_deps
        self.inlining_stack = inlining_stack

    @classmethod
    def for_context(cls, context: CompilationContext) -> "Compiler":
        # Make a shallow copy so that re-compilation doesn't end up reusing the same
        # already-added (say) local variables.
        # TODO: deep copy..?
        return cls(
            ctxt=CompilationContext(slots=dict(context.slots), base=context.base),
            sequence=BytecodeSequence(code=[]),
            multimethod_deps=[],
            inlining_stack=[],
        )

    def add_bytecode(self, op: str, args: Tuple, span: SourceSpan):
        self.sequence.code.append(BytecodeOp(op=op, args=args, span=span))

    def compile_expr(self, expr: Expr):
        try:
            self._compile_expr(expr)
        except Exception as e:
            print(f"Compilation error while compiling expression: {expr}")
            print(f"Error: {e}")
            raise e

    def _compile_expr(self, expr: Expr):
        assert expr is not None

        def add(op: str, *args, span: SourceSpan):
            self.add_bytecode(op, args, span)

        # TODO: make this less hacky -- should be part of macro / AST rewrite system.
        tail_call = False
        if isinstance(expr, NAryMessageExpr) and [message.value for message in expr.messages] == [
            "TAIL-CALL"
        ]:
            if expr.target is not None:
                raise ValueError("TAIL-CALL: requires no receiver")
            call = expr.args[0]
            while isinstance(call, ParenExpr):
                call = call.inner
            if not (
                isinstance(call, UnaryOpExpr)
                or isinstance(call, BinaryOpExpr)
                or isinstance(call, NameExpr)
                or isinstance(call, UnaryMessageExpr)
                or isinstance(call, NAryMessageExpr)
            ):
                raise ValueError(
                    "TAIL-CALL: must be applied to a unary/binary op or a unary/n-ary message"
                )
            tail_call = True
            expr = call

        # Only the first of the args is actually optional.
        def compile_invocation(
            message: str, args: list[Optional[Expr]], tail_call: bool, span: SourceSpan
        ) -> None:
            assert len(args) > 0
            assert all(arg is not None for arg in args[1:])

            maybe_tail = "tail-" if tail_call else ""

            slot = self.ctxt.lookup(message)
            if not slot:
                raise ValueError(f"Could not compile: unknown slot '{message}'.")

            if isinstance(slot, CompileTimeHandler):
                slot.handler(self, span, *args)
                return
            elif isinstance(slot, UndeterminedValue):
                receiver, args = args[0], args[1:]
                if receiver or args:
                    raise ValueError(
                        f"Could not compile: local slot is an undetermined-value, so does not need receiver or arguments: {message}."
                    )
                add("get-slot", message, span=span)
                return
            elif isinstance(slot, UndeterminedMethod) or isinstance(slot, MultiMethod):
                # You might think we want to compile evaluations of the method's arguments here.
                # However, if one of the multimethod's methods is an intrinsic (for instance if:then:else:),
                # then we need to allow the intrinsic to handle compilation (being given the argument expressions).
                # Wait until (possibly) later to evaluate arguments.
                # EDIT: ^^^ is not really correct. Inline intrinsics can't necessarily do much just with the call-site
                # expression arguments; e.g. if:then: is implemented in terms of if:then:else:, but then we need to
                # inline an invocation of `if: ... then: [...]`, and also apply some optimizations to move the quote
                # closer to the call site in the implementation of if:then:, and _then_ inline the quote...
                for arg in args:
                    if arg is None:
                        add("push-default-receiver", span=span)
                    else:
                        self._compile_expr(arg)

                if isinstance(slot, MultiMethod):
                    if slot not in self.multimethod_deps:
                        self.multimethod_deps.append(slot)
                    if should_inline_multimethod_dispatch(slot):
                        method_args = [
                            (list(method.param_matchers), JumpIndex(None))
                            for method in slot.methods
                        ]
                        jump_index_on_failure = JumpIndex(None)
                        add(
                            "multimethod-dispatch",
                            message,
                            len(args),
                            method_args,
                            jump_index_on_failure,
                            span=span,
                        )
                        return_jump_indices = []
                        for i, method in enumerate(slot.methods):
                            _, entry_jump = method_args[i]
                            entry_jump.index = len(self.sequence.code)
                            if isinstance(method.body, QuoteMethodBody):
                                if should_inline_quote_method(slot, method):
                                    # If already in the inlining stack, then we can handle this as a tail call
                                    # to where we already inlined this method body.
                                    # EDIT: need to get fancier here, since this only works if directly recursively
                                    # calling without any quotes getting in the way (i.e. not even if:then:else:).
                                    # We need to inline quotes at their call sites (including if:then:else:) before
                                    # a check like this would help much.
                                    if any(
                                        method.body == inlined_method
                                        for inlined_method, _ in self.inlining_stack
                                    ):
                                        inlined_start = next(
                                            index
                                            for inlined_method, index in self.inlining_stack
                                            if method.body == inlined_method
                                        )
                                        add("jump", JumpIndex(inlined_start), span=span)
                                    else:
                                        # TODO: need to handle name conflicts (e.g. parameter of inlined function
                                        # has same name as local slot). Well, TODO is to do this in a better way.
                                        # This is pretty hacky.
                                        add("push-context", span=span)
                                        for param in reversed(method.body.param_names):
                                            add("create-slot", param, span=span)
                                            add("drop", span=span)
                                        # Refer to the same bytecode sequence and multimethod deps, so
                                        # that when compiling the inlined body, we add to the outer
                                        # bytecode and outer deps.
                                        # TODO: handle tail-calls within the inlined function (i.e. replace with
                                        # non-tail-call variant, unless the call to this inlined function was a
                                        # tail call in the first place; it's ok to have pop-context afterwards).
                                        inline_compiler = Compiler(
                                            ctxt=CompilationContext(
                                                slots={
                                                    param: UndeterminedValue(param)
                                                    for param in method.body.param_names
                                                },
                                                base=method.body.context,
                                            ),
                                            sequence=self.sequence,
                                            multimethod_deps=self.multimethod_deps,
                                            inlining_stack=self.inlining_stack
                                            + [(method.body, entry_jump.index)],
                                        )
                                        inline_compiler._compile_expr(
                                            method.body.compiled_body.body
                                        )
                                        add("pop-context", span=span)
                                else:
                                    add(
                                        maybe_tail + "invoke-quote",
                                        message,
                                        method.body,
                                        len(args),
                                        span=span,
                                    )
                            elif isinstance(method.body, IntrinsicMethodBody):
                                if method.body.compile_inline is not None and False:
                                    # Ask the intrinsic to compile inline!
                                    method.body.compile_inline(self, tail_call, span)
                                else:
                                    # Just invoke the instrinsic.
                                    add(
                                        maybe_tail + "invoke-intrinsic",
                                        message,
                                        method.body.handler,
                                        len(args),
                                        span=span,
                                    )
                            elif isinstance(method.body, NativeMethodBody):
                                # TODO: inline compile natives?
                                add(
                                    maybe_tail + "invoke-native",
                                    message,
                                    method.body.handler,
                                    len(args),
                                    span=span,
                                )
                            else:
                                raise AssertionError(
                                    f"forgot a method body type: {type(method.body)}"
                                )
                            # Last method can fall through.
                            if i < len(slot.methods) - 1:
                                # The jump arg will be filled in with bytecode index to indicate where to jump after the method body.
                                return_jump_index = JumpIndex(None)
                                return_jump_indices.append(return_jump_index)
                                add("jump", return_jump_index, span=span)
                        return_index = len(self.sequence.code)
                        jump_index_on_failure.index = return_index
                        for return_jump in return_jump_indices:
                            return_jump.index = return_index
                        return
                # Default: just add an invocation.
                add(maybe_tail + "invoke", message, len(args), span=span)
            elif isinstance(slot, Value):
                receiver, args = args[0], args[1:]
                if receiver or args:
                    raise ValueError(
                        f"Could not compile: slot is not a method, so does not need receiver or arguments: {message}."
                    )
                # Don't use the value directly. Look it up at runtime later (it could very well be a local variable).
                # add("push-value", slot, span=span)
                add("get-slot", message, span=span)
            else:
                raise AssertionError(f"forgot a slot type? {type(slot)}: {slot}")

        if isinstance(expr, UnaryOpExpr):
            compile_invocation(expr.op.value, [expr.arg], tail_call=tail_call, span=expr.span)
        elif isinstance(expr, BinaryOpExpr):
            compile_invocation(
                expr.op.value + ":", [expr.left, expr.right], tail_call=tail_call, span=expr.span
            )
        elif isinstance(expr, NameExpr):
            compile_invocation(expr.name.value, [None], tail_call=tail_call, span=expr.span)
        elif isinstance(expr, LiteralExpr):
            literal = expr.literal
            if literal._type == TokenType.SYMBOL:
                add("push-value", SymbolValue(literal.value), span=literal.span)
            elif literal._type == TokenType.NUMBER:
                add("push-value", NumberValue(literal.value), span=literal.span)
            elif literal._type == TokenType.STRING:
                add("push-value", StringValue(literal.value), span=literal.span)
            else:
                raise AssertionError(f"Forgot a literal token type! {literal._type} ({literal})")
        elif isinstance(expr, UnaryMessageExpr):
            compile_invocation(
                expr.message.value, [expr.target], tail_call=tail_call, span=expr.span
            )
        elif isinstance(expr, NAryMessageExpr):
            assert len(expr.messages) == len(expr.args)
            message = "".join(message.value + ":" for message in expr.messages)
            compile_invocation(
                message, [expr.target] + expr.args, tail_call=tail_call, span=expr.span
            )
        elif isinstance(expr, ParenExpr):
            self._compile_expr(expr.inner)
        elif isinstance(expr, QuoteExpr):
            # TODO: this is where `it` default param needs to be added (or not).
            if not expr.parameters:
                param_names = ["it"]
            else:
                param_names = expr.parameters
            body_comp_ctxt = CompilationContext(
                slots={param: UndeterminedValue(param) for param in param_names}, base=self.ctxt
            )
            quote = QuoteValue(
                parameters=expr.parameters,
                compiled_body=CompiledBody(expr.body, bytecode=None, comp_ctxt=body_comp_ctxt),
                context=None,  # will be filled in later during evaluation
                span=expr.span,
            )
            add("push-closure", quote, span=expr.span)
        elif isinstance(expr, DataExpr):
            for component in expr.components:
                self._compile_expr(component)
            add("components>vector", len(expr.components), span=expr.span)
        elif isinstance(expr, SequenceExpr):
            assert expr.sequence != []
            for i, part in enumerate(expr.sequence):
                self._compile_expr(part)
                is_last = i == len(expr.sequence) - 1
                if not is_last:
                    add("drop", span=part.span)
        elif isinstance(expr, TupleExpr):
            for component in expr.components:
                self._compile_expr(component)
            add("components>tuple", len(expr.components), span=expr.span)
        else:
            raise AssertionError(f"Forgot an expression type! {type(expr)}")


should_show_compiler_output = True


def show_compiler_output(
    expr: Expr,
    sequence: BytecodeSequence,
    multimethod_deps: list[MultiMethod],
    is_recompilation: bool,
):
    if not should_show_compiler_output:
        return

    print("===== COMPILER OUTPUT =====")
    if is_recompilation:
        print("(RECOMPILATION)")
    print("INPUT EXPRESSION:", expr)

    def print_op(index, bytecode, depth):
        level = "    "
        indent = level * depth

        if depth >= 5:
            print(indent + "(WARNING: recursion depth exceeded")
            return

        def prefix():
            print(indent + f"{index}: {bytecode.op}", end="")

        def basic_print_op():
            prefix()
            print("".join(" " + str(arg) for arg in bytecode.args))

        if bytecode.op == "multimethod-dispatch":
            message, nargs, methods, jump_index_on_failure = bytecode.args
            prefix()
            print(f" {message} {nargs}")
            for matchers, jump in methods:

                def matcher_str(matcher):
                    if isinstance(matcher, ParameterAnyMatcher):
                        return "<any>"
                    elif isinstance(matcher, ParameterTypeMatcher):
                        return matcher.param_type.name
                    elif isinstance(matcher, ParameterValueMatcher):
                        return f"eq({matcher.param_value})"

                print(
                    indent
                    + level
                    + f"->{jump.index} on match: {', '.join(matcher_str(matcher) for matcher in matchers)}"
                )
            print(indent + level + f"->{jump_index_on_failure.index} on failure")
        # elif bytecode.op in ["invoke-quote", "tail-invoke-quote"]:
        #     prefix()
        #     message = bytecode.args[0]
        #     quote: QuoteMethodBody = bytecode.args[1]
        #     nargs: int = bytecode.args[2]
        #     print(f" {message} {quote.compiled_body.body} {nargs}")
        #     if quote.compiled_body.bytecode:
        #         for i, c in enumerate(quote.compiled_body.bytecode.code):
        #             print_op(i, c, depth + 1)
        #     else:
        #         print(indent + level + "<invalidated, needs recompilation>")
        # elif bytecode.op == "push-closure":
        #     basic_print_op()
        #     quote: QuoteValue = bytecode.args[0]
        #     if quote.compiled_body.bytecode:
        #         for i, c in enumerate(quote.compiled_body.bytecode.code):
        #             print_op(i, c, depth + 1)
        #     else:
        #         print(indent + level + "<invalidated, needs recompilation>")
        else:
            basic_print_op()

    print("BYTECODE:")
    for i, c in enumerate(sequence.code):
        print_op(i, c, depth=1)
    print("MULTIMETHOD DEPS:")
    for multimethod in multimethod_deps:
        print("  " + multimethod.name)
    print("=========== END ===========")

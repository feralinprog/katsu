from dataclasses import dataclass
from parser import (
    BinaryOpExpr,
    DataExpr,
    Expr,
    LiteralExpr,
    NameExpr,
    NAryMessageExpr,
    ParenExpr,
    QuoteExpr,
    SequenceExpr,
    TupleExpr,
    UnaryMessageExpr,
    UnaryOpExpr,
)
from typing import Callable, Optional, Tuple, Union

from error import RunError, SignalError
from lexer import TokenType
from span import SourceSpan

#################################################
# Object Model
#################################################


class Value:
    pass


@dataclass
class NumberValue(Value):
    value: int

    def __str__(self):
        return str(self.value)


@dataclass
class StringValue(Value):
    value: str

    def __str__(self):
        return self.value


@dataclass
class BoolValue(Value):
    value: bool

    def __str__(self):
        return "t" if self.value else "f"


@dataclass
class NullValue(Value):
    def __str__(self):
        return "null"


@dataclass
class SymbolValue(Value):
    symbol: str

    def __str__(self):
        return ":" + self.symbol


@dataclass
class TupleValue(Value):
    components: list[Value]

    def __str__(self):
        return "(" + ", ".join(str(component) for component in self.components) + ")"


@dataclass
class VectorValue(Value):
    components: list[Value]

    def __str__(self):
        return "{ " + "; ".join(str(component) for component in self.components) + " }"


@dataclass
class QuoteValue(Value):
    parameters: list[str]
    body: Expr
    context: "Context"
    # Source span, to allow for useful error messages.
    span: Optional[SourceSpan]

    def __str__(self):
        # TODO: pprint, make less verbose
        return (
            ("\\" + " ".join(self.parameters) + " " if self.parameters else "")
            + "[ "
            + str(self.body)
            + " ]"
        )


@dataclass
class Block:
    parameters: list[str]
    body: Expr
    # Source span, to allow for informative error messages.
    span: Optional[SourceSpan]

    def __str__(self):
        # TODO: pprint, make less verbose
        return (
            ("\\" + " ".join(self.parameters) + " " if self.parameters else "")
            + "[ "
            + str(self.body)
            + " ]"
        )


@dataclass
class ContinuationValue(Value):
    # Must be a deep copy (up to Values / Contexts) of a runtime state.
    state: "RuntimeState"

    def __str__(self):
        return "<continuation>"


@dataclass
class ReturnContinuationValue(Value):
    # Uses reference equality to determine if the return_to frame is still
    # in the current dynamic scope (i.e. on the call stack).
    # TODO: make this work with continuations (since those are implemented
    # as wholesale copying call frames...)
    return_to: "CallFrame"

    def __str__(self):
        return "<return-continuation>"


@dataclass
class TypeValue(Value):
    name: str
    bases: list["TypeValue"]

    def __str__(self):
        return f"<class {self.name}>"


@dataclass
class DataclassTypeValue(TypeValue):
    slots: list[str]

    def __str__(self):
        return f"<dataclass {self.name}: {', '.join(self.slots)}>"


@dataclass
class DataclassValue(Value):
    type: DataclassTypeValue
    values: list[Value]

    def __str__(self):
        if not self.values:
            return self.type.name + " new"
        else:
            return self.type.name + "".join(
                " " + slot + ": " + str(value) for slot, value in zip(self.type.slots, self.values)
            )


NumberType = TypeValue("Number", bases=[])
StringType = TypeValue("String", bases=[])
BoolType = TypeValue("Bool", bases=[])
NullType = TypeValue("Null", bases=[])
SymbolType = TypeValue("Symbol", bases=[])
VectorType = TypeValue("Vector", bases=[])
TupleType = TypeValue("Tuple", bases=[])
QuoteType = TypeValue("Quote", bases=[])
ContinuationType = TypeValue("Continuation", bases=[])
ReturnContinuationType = TypeValue("ReturnContinuation", bases=[])
TypeType = TypeValue("Type", bases=[])
DataclassTypeType = TypeValue("DataclassType", bases=[TypeType])


def type_of(value: Value) -> TypeValue:
    if isinstance(value, NumberValue):
        return NumberType
    elif isinstance(value, StringValue):
        return StringType
    elif isinstance(value, BoolValue):
        return BoolType
    elif isinstance(value, NullValue):
        return NullType
    elif isinstance(value, SymbolValue):
        return SymbolType
    elif isinstance(value, TupleValue):
        return TupleType
    elif isinstance(value, VectorValue):
        return VectorType
    elif isinstance(value, QuoteValue):
        return QuoteType
    elif isinstance(value, ContinuationValue):
        return ContinuationType
    elif isinstance(value, ReturnContinuationValue):
        return ReturnContinuationType
    elif isinstance(value, TypeValue):
        return TypeType
    elif isinstance(value, DataclassTypeValue):
        return DataclassTypeType
    elif isinstance(value, DataclassValue):
        return value.type


def handle__type(ctxt: "Context", receiver: Value) -> Value:
    return type_of(receiver)


def is_subtype(a: TypeValue, b: TypeValue) -> bool:
    # TODO: check for no recursion... either here or whenever creating new types
    # also make this waaaay more efficient (use cached linearizations?)
    if a == b:
        return True
    for base in a.bases:
        if is_subtype(base, b):
            return True
    return False


#################################################
# Runtime State Model
#################################################


@dataclass
class IntrinsicHandler:
    # Doesn't have to immediately produce a result; the handler is provided
    # the current runtime state, a receiver, and arguments, and may update
    # the runtime state arbitrarily. This handler must also update the state's
    # top of stack frame position as necessary; the interpreter gives up this
    # responsibility. On entry, the data stack has the receiver and arguments
    # already removed. If the handler raises an exception, it must ensure that
    # the data stack is left in a state where pushing one more value to the
    # stack allows that value to be treated as the result of the handler
    # invocation. (For instance, not modifying the data stack at all meets
    # this criterion.)
    handler: Callable[["RuntimeState", Value, list[Value]], None]


@dataclass
class NativeHandler:
    # A callable of the form
    #   (ctxt: Context, receiver: Value, *args: list[Value]) -> Value
    # Should avoid using any `eval` functions; those should be implemented using
    # an IntrinsicHandler instead, so as to reify the call stack.
    handler: Callable[["Context", Value, list[Value]], Value]


@dataclass
class Context:
    # TODO: add back IntrinsicHandler / NativeHandler? Maybe multimethods should just
    # be implemented in-language instead of being so intrinsic...
    slots: dict[str, Union[Value, "MultiMethod"]]
    base: Optional["Context"]


@dataclass
class BytecodeOp:
    # TODO: switch to enum
    op: str
    # TODO: make subclasses per op for better type safety
    args: Tuple
    # For stack traces / debugging
    span: SourceSpan


# List of bytecode operations:
# push-default-receiver
# push-number <number>
# push-string <str>
# push-symbol <str>
# push-block <Block>
# push-current-context
# context+block>quote
# invoke <message: str> <nargs: int>      (nargs includes the receiver)
# components>vector <length: int>
# components>tuple <length: int>
# drop


@dataclass
class BytecodeSequence:
    code: list[BytecodeOp]


@dataclass
class CallFrame:
    sequence: BytecodeSequence
    # Next index to execute (or == len(sequence.code) if the next operation
    # is to return.
    spot: int
    data_stack: list[Union[Value, Expr, Context]]
    context: Context
    # Receiver to use if none is explicitly provided in an invocation.
    default_receiver: Value
    # Optional value to call when unwinding this call frame.
    cleanup: Optional[Value]
    # Is this frame the invocation of a cleanup action?
    is_cleanup: bool
    # If is_cleanup, this holds the value being returned to the first non-is_cleanup
    # frame in the call stack.
    cleanup_retain: Optional[Value]
    # If true, when we next attempt to execute any bytecode in this frame,
    # the frame will be immediately unwound instead.
    force_unwind: bool

    def copy(self) -> "CallFrame":
        return CallFrame(
            sequence=self.sequence,
            spot=self.spot,
            data_stack=list(self.data_stack),
            context=self.context,
            default_receiver=self.default_receiver,
            cleanup=self.cleanup,
            is_cleanup=self.is_cleanup,
            cleanup_retain=self.cleanup_retain,
            force_unwind=self.force_unwind,
        )


@dataclass
class RuntimeState:
    call_stack: list[CallFrame]


@dataclass
class ParameterMatcher:
    def matches(self, arg: Value) -> bool:
        raise NotImplementedError("Subclasses must implement.")


@dataclass
class ParameterAnyMatcher(ParameterMatcher):
    def matches(self, arg: Value) -> bool:
        # Matches any value!
        return True

    def __str__(self):
        return "<match:any>"


@dataclass
class ParameterTypeMatcher(ParameterMatcher):
    param_type: TypeValue

    def matches(self, arg: Value) -> bool:
        return is_subtype(type_of(arg), self.param_type)

    def __str__(self):
        return f"<match:type<={self.param_type}>"


@dataclass
class ParameterValueMatcher(ParameterMatcher):
    param_value: Value

    def matches(self, arg: Value) -> bool:
        # TODO: structural (==) equality instead?
        return arg is self.param_value

    def __str__(self):
        return f"<match:value={self.param_value}>"


@dataclass
class MethodBody:
    pass


@dataclass
class QuoteMethodBody(MethodBody):
    context: Context
    # This includes the receiver name.
    param_names: list[str]
    body_expr: Expr
    body: BytecodeSequence


@dataclass
class IntrinsicMethodBody(MethodBody):
    handler: IntrinsicHandler


@dataclass
class NativeMethodBody(MethodBody):
    handler: NativeHandler


@dataclass
class Method:
    # This also includes a matcher for the receiver.
    param_matchers: list[ParameterMatcher]
    body: MethodBody


# a <= b if a is at least as specific as b.
# (Think of the sets of matched values.)
def matcher_lte(a: ParameterMatcher, b: ParameterMatcher):
    # A value matcher is more specific than a type matcher, which is more specific than an 'any' matcher.
    # Value matchers are unordered.
    # Type matchers are partially ordered by their parameter types.
    # 'Any' matchers are all equal.
    if isinstance(b, ParameterAnyMatcher):
        return True
    elif isinstance(b, ParameterTypeMatcher):
        if isinstance(a, ParameterAnyMatcher):
            return False
        elif isinstance(a, ParameterTypeMatcher):
            return is_subtype(a.param_type, b.param_type)
        elif isinstance(a, ParameterValueMatcher):
            return True
        else:
            raise AssertionError(f"Forgot matcher type: {a}")
    elif isinstance(b, ParameterValueMatcher):
        if isinstance(a, ParameterAnyMatcher):
            return False
        elif isinstance(a, ParameterTypeMatcher):
            return False
        elif isinstance(a, ParameterValueMatcher):
            # TODO: structural (==) equality instead?
            return a.param_value is b.param_value
        else:
            raise AssertionError(f"Forgot matcher type: {a}")
    else:
        raise AssertionError(f"Forgot matcher type: {b}")


# a <= b if a is at least as specific as b.
# (Think of the sets of matched values-tuples.)
def method_lte(a: Method, b: Method):
    assert len(a.param_matchers) == len(b.param_matchers)
    return all(matcher_lte(ma, mb) for ma, mb in zip(a.param_matchers, b.param_matchers))


@dataclass
class MultiMethod:
    # Just for debug / error messages.
    name: str
    methods: list[Method]

    def add_method(self, method: Method) -> None:
        assert isinstance(method, Method)
        assert isinstance(method.param_matchers, list) or isinstance(method.param_matchers, tuple)
        for matcher in method.param_matchers:
            assert isinstance(matcher, ParameterMatcher)

        # Make sure the method signature doesn't duplicate a previously added method.
        for existing in self.methods:
            if method.param_matchers == existing.param_matchers:
                # TODO: improve error message
                raise ValueError(f"Method signature already exists for '{self.name}'.")
        # TODO: come back to this if deciding to allow ambiguous multimethod resolution;
        # this would require more constraints on the sort ordering.
        # Sort so that if method A < method B, then A is before B in the list.
        # (A <= B if for each parameter index, the parameter matcher in A is at least as
        # specific as the parameter matcher in B, i.e. matcher-from-A <= matcher-from-B.
        # Then we define A < B if A <= B and A != B.)
        found_spot = False
        for i in range(len(self.methods)):
            if method_lte(method, self.methods[i]):
                found_spot = True
                break
        if found_spot:
            self.methods.insert(i, method)
        else:
            self.methods.append(method)

    def select_method(self, args: list[Value]) -> Method:
        options = []
        for method in self.methods:
            assert len(args) == len(
                method.param_matchers
            ), f"Multimethod {self.name} has method {method} not matching argument count for {args}."
            if all(matcher.matches(arg) for arg, matcher in zip(args, method.param_matchers)):
                options.append(method)
        if len(options) == 1:
            return options[0]
        elif len(options) == 0:
            raise SignalError(
                condition_name="no-matching-method",
                message=f"No matching method found for multi-method {self.name} "
                f"with values: {', '.join(str(arg) for arg in args)}.",
            )
        else:
            # Determine if there is a single most-specific option. Otherwise the resolution
            # is ambiguous.
            # Note that self.methods is pre-sorted to allow these comparisons.
            candidate = min(options, key=self.methods.index)
            if all(method_lte(candidate, option) for option in options):
                return candidate
            raise SignalError(
                condition_name="ambiguous-method-resolution",
                message=f"There were multiple matching methods found for multi-method {self.name} "
                f"with values: {', '.join(str(arg) for arg in args)}.",
            )


#################################################
# "Compiler"
#################################################


def compile_into(expr: Expr, sequence: BytecodeSequence):
    assert expr is not None

    def add(op: str, *args, span: SourceSpan):
        sequence.code.append(BytecodeOp(op=op, args=args, span=span))

    if isinstance(expr, UnaryOpExpr):
        compile_into(expr.arg, sequence)
        add("invoke", expr.op.value, 1, span=expr.span)
    elif isinstance(expr, BinaryOpExpr):
        add("push-default-receiver", span=expr.op.span)
        compile_into(expr.left, sequence)
        compile_into(expr.right, sequence)
        add("invoke", expr.op.value + ":_:", 3, span=expr.span)
    elif isinstance(expr, NameExpr):
        add("push-default-receiver", span=expr.name.span)
        add("invoke", expr.name.value, 1, span=expr.span)
    elif isinstance(expr, LiteralExpr):
        literal = expr.literal
        if literal._type == TokenType.SYMBOL:
            add("push-symbol", literal.value, span=literal.span)
        elif literal._type == TokenType.NUMBER:
            add("push-number", literal.value, span=literal.span)
        elif literal._type == TokenType.STRING:
            add("push-string", literal.value, span=literal.span)
        else:
            raise AssertionError(f"Forgot a literal token type! {literal._type} ({literal})")
    elif isinstance(expr, UnaryMessageExpr):
        compile_into(expr.target, sequence)
        add("invoke", expr.message.value, 1, span=expr.span)
    elif isinstance(expr, NAryMessageExpr):
        if expr.target is None:
            # TODO: use smaller span?
            add("push-default-receiver", span=expr.span)
        else:
            compile_into(expr.target, sequence)
        for arg in expr.args:
            compile_into(arg, sequence)
        assert len(expr.messages) == len(expr.args)
        message = "".join(message.value + ":" for message in expr.messages)
        add("invoke", message, 1 + len(expr.args), span=expr.span)
    elif isinstance(expr, ParenExpr):
        compile_into(expr.inner, sequence)
    elif isinstance(expr, QuoteExpr):
        add("push-current-context", span=expr.span)
        add("push-block", Block(expr.parameters, expr.body, span=expr.span), span=expr.span)
        add("context+block>quote", span=expr.span)
    elif isinstance(expr, DataExpr):
        for component in expr.components:
            compile_into(component, sequence)
        add("components>vector", len(expr.components), span=expr.span)
    elif isinstance(expr, SequenceExpr):
        assert expr.sequence != []
        for i, part in enumerate(expr.sequence):
            compile_into(part, sequence)
            is_last = i == len(expr.sequence) - 1
            if not is_last:
                add("drop", span=part.span)
    elif isinstance(expr, TupleExpr):
        for component in expr.components:
            compile_into(component, sequence)
        add("components>tuple", len(expr.components), span=expr.span)
    else:
        raise AssertionError(f"Forgot an expression type! {type(expr)}")


def compile(expr: Expr) -> BytecodeSequence:
    assert expr is not None
    sequence = BytecodeSequence(code=[])
    compile_into(expr, sequence)
    return sequence


#################################################
# Runtime Interpreter / Evaluation
#################################################

should_log_states = False


def debug_log_state(state: RuntimeState) -> None:
    if not should_log_states:
        return

    print("============ RUNTIME STATE ===============")

    for depth, frame in enumerate(state.call_stack):
        if frame.spot == len(frame.sequence.code):
            location_msg = f"just after "
            bytecode = frame.sequence.code[-1]
        else:
            location_msg = f"at "
            bytecode = frame.sequence.code[frame.spot]
        location_msg += f"{repr(bytecode.span.file.source[bytecode.span.start.index:bytecode.span.end.index])} (at {bytecode.span})"
        print(f"[{depth}] call frame: {location_msg}")

        flags = []
        if frame.is_cleanup:
            flags.append("is-cleanup")
        if frame.force_unwind:
            flags.append("force-unwind")
        if flags:
            print("  " + " ".join(flags))

        print("  bytecode:")
        for spot, bytecode in enumerate(frame.sequence.code):
            prefix = "  -> " if spot == frame.spot else "     "
            print(
                f"{prefix}{bytecode.op}{''.join(' ' + (repr(arg) if isinstance(arg, str) else str(arg)) for arg in bytecode.args)}"
            )
        if frame.spot == len(frame.sequence.code):
            print("  -> [about to return]")

        print(f"  default-receiver: {frame.default_receiver}")

        print("  context:" + ("" if frame.context.base else " <global>"))
        if frame.context.base:
            context = frame.context
            print("    slots:")
            while context.base:
                for slot, value in context.slots.items():
                    if isinstance(value, Context):
                        value_str = "<a context> (!!!!!!)"
                    else:
                        value_str = str(value)
                    print(f"      {slot} -> {value_str}")
                context = context.base
            print("    <global>")

        if frame.cleanup:
            print(f"  cleanup-action: {frame.cleanup}")

        if frame.cleanup_retain:
            if isinstance(frame.cleanup_retain, Context):
                value_str = "<a context> (!!!!!!)"
            else:
                value_str = str(frame.cleanup_retain)
            print(f"  cleanup-action retain value: {value_str}")

        print("  data stack:")
        for value in frame.data_stack:
            if isinstance(value, Context):
                value_str = "<a context>"
            elif isinstance(value, Expr):
                value_str = f"<expr {value}>"
            else:
                value_str = str(value)
            print(f"  >> {value_str}")

    print("================ END =====================")


def eval_one_op(state: RuntimeState) -> None:
    assert state
    assert state.call_stack
    frame = state.call_stack[-1]
    assert 0 <= frame.spot <= len(frame.sequence.code)
    assert not (frame.is_cleanup and frame.force_unwind)

    debug_log_state(state)

    if frame.spot == len(frame.sequence.code) or frame.force_unwind:
        # Return from invocation. The current frame's top-of-data-stack holds the return value,
        # so push this to the next lower call frame's data stack. (Well, that's mostly true.
        # If the current frame is_cleanup, then the return value is held in cleanup_retain, so
        # pass this down to the next frame instead. Also, if the next frame _also_ is_cleanup,
        # then we don't want to set its cleanup_retain at all; this is just a regular return
        # to an in-progress cleanup action which already has a return value retained.)

        if frame.is_cleanup:
            assert frame.cleanup_retain
            return_value = frame.cleanup_retain
        else:
            # The frame may have multiple values in the data stack, for instance if it is
            # performing a non-local return from within a subexpression.
            return_value = frame.data_stack.pop()

        cleanup_action = frame.cleanup

        state.call_stack.pop()

        if cleanup_action:
            call_impl(
                "call",
                state,
                receiver=cleanup_action,
                args=[],
                cleanup=None,
                is_cleanup=True,
                cleanup_retain=return_value,
            )
        else:
            next_frame = state.call_stack[-1]
            # Sanity check:
            if next_frame.is_cleanup:
                assert next_frame.cleanup_retain
            next_frame.data_stack.append(return_value)

        return

    bytecode = frame.sequence.code[frame.spot]
    op = bytecode.op

    if op == "push-default-receiver":
        assert not bytecode.args
        frame.data_stack.append(frame.default_receiver)
        frame.spot += 1
    elif op == "push-number":
        (v,) = bytecode.args
        assert isinstance(v, int)
        frame.data_stack.append(NumberValue(v))
        frame.spot += 1
    elif op == "push-string":
        (v,) = bytecode.args
        assert isinstance(v, str)
        frame.data_stack.append(StringValue(v))
        frame.spot += 1
    elif op == "push-symbol":
        (v,) = bytecode.args
        assert isinstance(v, str)
        frame.data_stack.append(SymbolValue(v))
        frame.spot += 1
    elif op == "push-block":
        (v,) = bytecode.args
        assert isinstance(v, Block)
        frame.data_stack.append(v)
        frame.spot += 1
    elif op == "push-current-context":
        assert not bytecode.args
        frame.data_stack.append(frame.context)
        frame.spot += 1
    elif op == "context+block>quote":
        assert not bytecode.args
        block = frame.data_stack.pop()
        assert isinstance(block, Block)
        ctxt = frame.data_stack.pop()
        assert isinstance(ctxt, Context)
        frame.data_stack.append(QuoteValue(block.parameters, block.body, ctxt, span=block.span))
        frame.spot += 1
    elif op == "invoke":
        message, nargs = bytecode.args
        assert isinstance(message, str)
        assert isinstance(nargs, int)
        assert nargs > 0
        assert len(frame.data_stack) >= nargs

        def signal_error(
            condition_name: str,
            error_message: str,
            state: RuntimeState,
            source_exception: Optional[Exception],
            already_signaled: bool,
        ):
            # Don't infinitely recurse if condition-handling itself signals a condition.
            if already_signaled:
                # TODO: spin up a debugger or something.
                raise RunError(
                    f"Evaluation error while attempting to signal ({condition_name}): {error_message}",
                    runtime_state=state,
                ) from source_exception

            # Call into language-space to handle the condition.
            signal_message = "handle-signal:"
            ctxt = frame.context
            slot = None
            while ctxt is not None:
                if signal_message in ctxt.slots:
                    slot = ctxt.slots[signal_message]
                    break
                ctxt = ctxt.base

            if not slot:
                # TODO: spin up a debugger or something.
                raise RunError(
                    f"Could not handle-signal: evaluation error ({condition_name}): {error_message}",
                    runtime_state=state,
                ) from source_exception

            signal_arg = TupleValue(
                components=[SymbolValue(condition_name), StringValue(error_message)]
            )
            frame.data_stack.append(frame.context)
            frame.data_stack.append(signal_arg)
            _do_invoke(state, frame, signal_message, slot, 2, already_signaled=True)

        def _do_invoke(state, frame, message, slot, nargs, already_signaled):
            if isinstance(slot, Value):
                frame.data_stack = frame.data_stack[:-nargs]
                frame.data_stack.append(slot)
                frame.spot += 1
            else:
                stack_len = len(frame.data_stack)
                # Note: args includes the receiver.
                args = frame.data_stack[stack_len - nargs :]
                frame.data_stack = frame.data_stack[:-nargs]
                if isinstance(slot, MultiMethod):
                    multimethod = slot

                    try:
                        method = multimethod.select_method(args)
                    except SignalError as e:
                        signal_error(
                            condition_name=e.condition_name,
                            error_message=str(e),
                            state=state,
                            source_exception=None,
                            already_signaled=already_signaled,
                        )
                        return

                    method_body = method.body
                    if isinstance(method_body, QuoteMethodBody):
                        if not method_body.body:
                            # TODO: do this earlier, within method:does:.
                            method_body.body = compile(method_body.body_expr)

                        body_ctxt = Context(slots={}, base=method_body.context)
                        assert len(args) == len(
                            method_body.param_names
                        ), f"{len(args)} != len({method_body.param_names})"
                        for param_name, arg in zip(method_body.param_names, args):
                            body_ctxt.slots[param_name] = arg

                        # TODO: use reified context as default receiver?
                        invoke_compiled(
                            state,
                            method_body.body,
                            body_ctxt,
                            default_receiver=NullValue(),
                            cleanup=None,
                            is_cleanup=False,
                            cleanup_retain=None,
                        )
                    elif isinstance(method_body, IntrinsicMethodBody):
                        try:
                            handler = method_body.handler
                            # Allow the intrinsic handler to take arbitrary control of the runtime. It also takes
                            # responsibility for updating the frame as necessary.
                            handler.handler(state, *args)
                        except Exception as e:
                            # TODO: allow handlers to raise more targeted exceptions that already include a condition name
                            signal_error(
                                condition_name="internal-error",
                                error_message=f"error from intrinsic handler: {e}",
                                state=state,
                                source_exception=e,
                                already_signaled=already_signaled,
                            )
                            return
                    elif isinstance(method_body, NativeMethodBody):
                        try:
                            handler = method_body.handler
                            # handler is something which can be called with a context, receiver, and arguments.
                            # It should not call evaluation functions. (It can, but the stack is not reified and
                            # uses host language stack instead).
                            result = handler.handler(frame.context, *args)
                        except Exception as e:
                            # TODO: allow handlers to raise more targeted exceptions that already include a condition name
                            signal_error(
                                condition_name="internal-error",
                                error_message=f"error from builtin handler: {e}",
                                state=state,
                                source_exception=e,
                                already_signaled=already_signaled,
                            )
                            return
                        assert isinstance(
                            result, Value
                        ), f"Result from builtin handler '{message}' must be a Value; got '{result}'."
                        frame.data_stack.append(result)
                        frame.spot += 1
                    else:
                        raise AssertionError(f"Unexpected method body '{method_body}'")
                else:
                    raise AssertionError(f"Unexpected slot value '{slot}'")

        # Find the slot value:
        ctxt = frame.context
        slot = None
        while ctxt is not None:
            if message in ctxt.slots:
                slot = ctxt.slots[message]
                break
            ctxt = ctxt.base
        if not slot:
            signal_error(
                condition_name="undefined-slot",
                error_message=f"Could not invoke '{message}'; slot is not defined.",
                state=state,
                source_exception=None,
                already_signaled=False,
            )
            return

        _do_invoke(state, frame, message, slot, nargs, already_signaled=False)
    elif op == "components>vector":
        (length,) = bytecode.args
        assert isinstance(length, int)
        assert 0 <= length <= len(frame.data_stack)
        components = frame.data_stack[len(frame.data_stack) - length :]
        frame.data_stack = frame.data_stack[: len(frame.data_stack) - length]
        frame.data_stack.append(VectorValue(components))
        frame.spot += 1
    elif op == "components>tuple":
        (length,) = bytecode.args
        assert isinstance(length, int)
        assert 0 <= length <= len(frame.data_stack)
        components = frame.data_stack[len(frame.data_stack) - length :]
        frame.data_stack = frame.data_stack[: len(frame.data_stack) - length]
        frame.data_stack.append(TupleValue(components))
        frame.spot += 1
    elif op == "drop":
        assert not bytecode.args
        frame.data_stack.pop()
        frame.spot += 1
    else:
        raise AssertionError(f"Forgot a bytecode op! {op}")


def invoke_compiled(
    state: RuntimeState,
    code: BytecodeSequence,
    ctxt: Context,
    default_receiver: Value,
    cleanup: Optional[Value],
    is_cleanup: bool,
    cleanup_retain: Optional[Value],
) -> None:
    if not is_cleanup:
        assert not cleanup_retain
    assert not (cleanup and is_cleanup)

    # There may not be any call frame active, for instance if we are invoking the
    # cleanup action of some top-level block with tail-call optimization in effect.
    if state.call_stack:
        frame = state.call_stack[-1]

        if is_cleanup:
            # This could be a tail-call cleanup-action invocation.
            assert 0 <= frame.spot <= len(frame.sequence.code)
        else:
            assert 0 <= frame.spot < len(frame.sequence.code)

        # Tail-call optimization! Don't tail-call if the current frame has a cleanup
        # action or if the current frame _is_ a cleanup action, though, to keep things
        # simpler.
        # TODO: support tail-call if current frame and even next frame both have cleanup
        # actions required.
        if frame.spot == len(frame.sequence.code) - 1 and not (frame.is_cleanup or frame.cleanup):
            # Unwind the frame. It hasn't yet produced a return value; that's what
            # we are about to calculate with a new frame.
            state.call_stack.pop()
        # Don't shift the frame if is_cleanup, since in this case we are setting
        # up a frame to handle a cleanup action, and this frame was _not_ created
        # due to explicitly invoking some callable object (i.e. as a normal part
        # of the frame's bytecode sequence), but rather automatically while unwinding
        # a frame.
        elif not is_cleanup:
            frame.spot += 1

    state.call_stack.append(
        CallFrame(
            sequence=code,
            spot=0,
            data_stack=[],
            context=ctxt,
            default_receiver=default_receiver,
            cleanup=cleanup,
            is_cleanup=is_cleanup,
            cleanup_retain=cleanup_retain,
            force_unwind=False,
        )
    )


def shift_frame(state: RuntimeState) -> None:
    assert state.call_stack
    frame = state.call_stack[-1]
    frame.spot += 1
    assert 0 <= frame.spot <= len(frame.sequence.code)


def eval_toplevel(expr: Expr, context: Context) -> Value:
    bytecode = compile(expr)
    state = RuntimeState(
        # TODO: maybe default_receiver should be a reified global context instead?
        call_stack=[
            CallFrame(
                bytecode,
                spot=0,
                data_stack=[],
                context=context,
                default_receiver=NullValue(),
                cleanup=None,
                is_cleanup=False,
                cleanup_retain=None,
                force_unwind=False,
            )
        ],
    )
    while True:
        if len(state.call_stack) == 1:
            frame = state.call_stack[0]
            if frame.spot == len(frame.sequence.code) and not frame.cleanup:
                break
        eval_one_op(state)
    debug_log_state(state)
    frame = state.call_stack[0]
    assert len(state.call_stack[0].data_stack) == 1
    if frame.is_cleanup:
        assert frame.cleanup_retain
        return frame.cleanup_retain
    else:
        return state.call_stack[0].data_stack[0]


#################################################
# Intrinsic Handlers
#################################################
# These are generally message handlers which require control of the runtime state beyond being a
# function from value inputs to value output.
# The `builtin` module imports these and adds to the default global context.


def intrinsic__if_then_else(
    state: RuntimeState, receiver: Value, cond: Value, tbody: Value, fbody: Value
) -> None:
    body = tbody if isinstance(cond, BoolValue) and cond.value else fbody
    if isinstance(body, QuoteValue):
        # TODO: use reified context as default receiver?
        # TODO: compile ahead of time somehow...
        invoke_compiled(
            state,
            compile(body.body),
            body.context,
            default_receiver=NullValue(),
            cleanup=None,
            is_cleanup=False,
            cleanup_retain=None,
        )
    else:
        state.call_stack[-1].data_stack.append(body)
        shift_frame(state)


def call_impl(
    message: str,
    state: RuntimeState,
    receiver: Value,
    args: list[Value],
    cleanup: Optional[Value],
    is_cleanup: bool,
    cleanup_retain: Optional[Value],
):
    if not is_cleanup:
        assert not cleanup_retain
    assert not (cleanup and is_cleanup)

    if isinstance(receiver, QuoteValue):
        # Special-case no parameters with one argument.
        if not receiver.parameters and len(args) == 1:
            param_names = ["it"]
            default_receiver = args[0]
        elif len(receiver.parameters) != len(args):
            raise ValueError(
                f"{message} receiver (a quote) has {len(receiver.parameters)} parameter(s), but is being provided {len(args)} argument(s)"
            )
        else:
            param_names = receiver.parameters
            # TODO: use reified context as default receiver?
            default_receiver = NullValue()
        new_slots = {name: value for name, value in zip(param_names, args)}
        # TODO: compile ahead of time somehow...
        invoke_compiled(
            state,
            compile(receiver.body),
            Context(slots=new_slots, base=receiver.context),
            default_receiver=default_receiver,
            cleanup=cleanup,
            is_cleanup=is_cleanup,
            cleanup_retain=cleanup_retain,
        )
    elif isinstance(receiver, ContinuationValue):
        if cleanup or is_cleanup:
            raise NotImplementedError()
        if len(args) != 1:
            raise ValueError(
                f"{message} receiver (a continuation) requires 1 parameter, but is being provided {len(args)} argument(s)"
            )
        continuation = receiver.state
        state.call_stack = [frame.copy() for frame in continuation.call_stack]
        state.call_stack[-1].data_stack.append(args[0])
    elif isinstance(receiver, ReturnContinuationValue):
        if cleanup or is_cleanup:
            raise NotImplementedError()
        if len(args) != 1:
            raise ValueError(
                f"{message} receiver (a return-continuation) requires 1 parameter, but is being provided {len(args)} argument(s)"
            )
        return_to_frame = receiver.return_to
        # Look for the frame in anything below the current top of stack.
        # (This only supports unwinding to a strictly lower point in the stack;
        # no sideways motion allowed in the current frame.)
        found_frame = False
        for frame in reversed(state.call_stack[:-1]):
            if frame is return_to_frame:
                found_frame = True
                break
        if not found_frame:
            raise ValueError(f"{message} receiver (a return-continuation) is no longer in scope")
        # Force frames from top-of-stack down to just above the `return_to_frame` to exit early
        # when we next get around to running any bytecode. Leave is_cleanup frames alone, though;
        # we still want to execute those!
        for frame in reversed(state.call_stack):
            if frame is return_to_frame:
                break
            if not frame.is_cleanup:
                frame.force_unwind = True
        state.call_stack[-1].data_stack.append(args[0])
        assert state.call_stack
        shift_frame(state)
    else:
        # Calling a non-callable value; the result is simply the value.
        if cleanup:
            # Someone called `cleanup:` on a non-callable value. Kinda pointless, but run the
            # cleanup action regardless.
            call_impl(
                "call",
                state,
                receiver=cleanup,
                args=[],
                cleanup=None,
                is_cleanup=True,
                cleanup_retain=receiver,
            )
        else:
            state.call_stack[-1].data_stack.append(receiver)
            shift_frame(state)


def intrinsic__call(state: RuntimeState, receiver: Value) -> None:
    call_impl("call", state, receiver, args=[], cleanup=None, is_cleanup=False, cleanup_retain=None)


def intrinsic__call_(state: RuntimeState, receiver: Value, value: Value) -> None:
    call_impl(
        "call:", state, receiver, args=[value], cleanup=None, is_cleanup=False, cleanup_retain=None
    )


def intrinsic__call_star_(state: RuntimeState, receiver: Value, value: Value) -> None:
    if not isinstance(value, TupleValue):
        raise ValueError("call*: value must be a tuple")
    call_impl(
        "call*:",
        state,
        receiver,
        args=value.components,
        cleanup=None,
        is_cleanup=False,
        cleanup_retain=None,
    )


def intrinsic__call_cc(state: RuntimeState, receiver: Value) -> None:
    current_continuation = ContinuationValue(
        state=RuntimeState(
            call_stack=[frame.copy() for frame in state.call_stack],
        )
    )
    call_impl(
        "call/cc",
        state,
        receiver,
        args=[current_continuation],
        cleanup=None,
        is_cleanup=False,
        cleanup_retain=None,
    )


def intrinsic__cleanup_(state: RuntimeState, receiver: Value, cleanup: Value) -> None:
    call_impl(
        "cleanup:", state, receiver, args=[], cleanup=cleanup, is_cleanup=False, cleanup_retain=None
    )


def intrinsic__call_rc(state: RuntimeState, receiver: Value) -> None:
    return_continuation = ReturnContinuationValue(return_to=state.call_stack[-1])
    call_impl(
        "call/rc",
        state,
        receiver,
        args=[return_continuation],
        cleanup=None,
        is_cleanup=False,
        cleanup_retain=None,
    )


intrinsic_handlers = {
    "if:then:else:": (
        (
            ParameterAnyMatcher(),
            ParameterTypeMatcher(BoolType),
            ParameterAnyMatcher(),
            ParameterAnyMatcher(),
        ),
        IntrinsicHandler(intrinsic__if_then_else),
    ),
    "call": ((ParameterAnyMatcher(),), IntrinsicHandler(intrinsic__call)),
    "call:": ((ParameterAnyMatcher(), ParameterAnyMatcher()), IntrinsicHandler(intrinsic__call_)),
    "call*:": (
        (ParameterAnyMatcher(), ParameterTypeMatcher(TupleType)),
        IntrinsicHandler(intrinsic__call_star_),
    ),
    "call/cc": ((ParameterAnyMatcher(),), IntrinsicHandler(intrinsic__call_cc)),
    "cleanup:": (
        (ParameterAnyMatcher(), ParameterTypeMatcher(QuoteType)),
        IntrinsicHandler(intrinsic__cleanup_),
    ),
    # call/return-continuation
    "call/rc": ((ParameterAnyMatcher(),), IntrinsicHandler(intrinsic__call_rc)),
}

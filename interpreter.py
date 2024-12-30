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

from error import RunError
from lexer import Token, TokenType
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
        return self.value


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
            ("\\" + " ".join(self.parameters) if self.parameters else "")
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


@dataclass
class ContinuationValue(Value):
    # Must be a deep copy (up to Values / Contexts) of a runtime state.
    state: "RuntimeState"

    def __str__(self):
        return "<continuation>"


@dataclass
class TypeValue(Value):
    name: str

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
    # already removed.
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
    # Each slot value is either a Value, or a python callable of the form
    #   (self, ctxt: Context, receiver: Value, *args: list[Value]) -> Value
    slots: dict[str, Union[Value, Callable[["Context", Value, list[Value]], Value]]]
    base: Optional["Context"]


@dataclass
class BytecodeOp:
    # TODO: switch to enum
    op: str
    # For stack traces / debugging
    span: SourceSpan
    # TODO: make subclasses per op for better type safety
    args: Optional[Tuple] = None


# List of bytecode operations:
# push-default-receiver
# push-number <number>
# push-string <str>
# push-symbol <str>
# push-block <Block>
# push-current-context
# context+block>quote
# invoke <message: str> <nargs: int>      (nargs does not include receiver)
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
    context: Context
    # Receiver to use if none is explicitly provided in an invocation.
    default_receiver: Value

    def copy(self) -> "CallFrame":
        return CallFrame(
            sequence=self.sequence,
            spot=self.spot,
            context=self.context,
            default_receiver=self.default_receiver,
        )


@dataclass
class RuntimeState:
    call_stack: list[CallFrame]
    data_stack: list[Union[Value, Expr, Context]]


@dataclass
class Method:
    context: Context
    receiver_name: str
    param_names: list[str]
    body_expr: Expr
    body: BytecodeSequence


#################################################
# "Compiler"
#################################################


def compile_into(expr: Expr, sequence: BytecodeSequence):
    assert expr is not None

    def add(op: str, *args, span: SourceSpan):
        sequence.code.append(BytecodeOp(op, span, args))

    if isinstance(expr, UnaryOpExpr):
        compile_into(expr.arg, sequence)
        add("invoke", expr.op.value, 0, span=expr.span)
    elif isinstance(expr, BinaryOpExpr):
        add("push-default-receiver", span=expr.op.span)
        compile_into(expr.left, sequence)
        compile_into(expr.right, sequence)
        add("invoke", expr.op.value + ":_:", 2, span=expr.span)
    elif isinstance(expr, NameExpr):
        add("push-default-receiver", span=expr.name.span)
        add("invoke", expr.name.value, 0, span=expr.span)
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
        add("invoke", expr.message.value, 0, span=expr.span)
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
        add("invoke", message, len(expr.args), span=expr.span)
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


def eval_one_op(state: RuntimeState) -> None:
    try:
        assert state
        assert state.call_stack
        frame = state.call_stack[-1]
        assert 0 <= frame.spot <= len(frame.sequence.code)

        if frame.spot == len(frame.sequence.code):
            # Return from invocation.
            state.call_stack.pop()
            return

        bytecode = frame.sequence.code[frame.spot]
        op = bytecode.op

        if op == "push-default-receiver":
            assert not bytecode.args
            state.data_stack.append(frame.default_receiver)
            frame.spot += 1
        elif op == "push-number":
            (v,) = bytecode.args
            assert isinstance(v, int)
            state.data_stack.append(NumberValue(v))
            frame.spot += 1
        elif op == "push-string":
            (v,) = bytecode.args
            assert isinstance(v, str)
            state.data_stack.append(StringValue(v))
            frame.spot += 1
        elif op == "push-symbol":
            (v,) = bytecode.args
            assert isinstance(v, str)
            state.data_stack.append(SymbolValue(v))
            frame.spot += 1
        elif op == "push-block":
            (v,) = bytecode.args
            assert isinstance(v, Block)
            state.data_stack.append(v)
            frame.spot += 1
        elif op == "push-current-context":
            assert not bytecode.args
            state.data_stack.append(frame.context)
            frame.spot += 1
        elif op == "context+block>quote":
            assert not bytecode.args
            block = state.data_stack.pop()
            assert isinstance(block, Block)
            ctxt = state.data_stack.pop()
            assert isinstance(ctxt, Context)
            state.data_stack.append(QuoteValue(block.parameters, block.body, ctxt, span=block.span))
            frame.spot += 1
        elif op == "invoke":
            message, nargs = bytecode.args
            assert isinstance(message, str)
            assert isinstance(nargs, int)
            assert len(state.data_stack) >= nargs + 1

            # Find the handler:
            ctxt = frame.context
            handler = None
            while ctxt is not None:
                if message in ctxt.slots:
                    handler = ctxt.slots[message]
                    break
                ctxt = ctxt.base
            if not handler:
                raise RunError(f"Could not invoke message; no slot defined for '{message}'.", state)

            if isinstance(handler, Value):
                state.data_stack = state.data_stack[: -(nargs + 1)]
                state.data_stack.append(handler)
                frame.spot += 1
            else:
                stack_len = len(state.data_stack)
                receiver = state.data_stack[stack_len - 1 - nargs]
                args = state.data_stack[stack_len - nargs :]
                state.data_stack = state.data_stack[: -(nargs + 1)]
                if isinstance(handler, Method):
                    method = handler

                    if not method.body:
                        # TODO: do this earlier, within method:does:.
                        method.body = compile(method.body_expr)

                    body_ctxt = Context(slots={}, base=method.context)
                    body_ctxt.slots[method.receiver_name] = receiver or NullValue()
                    assert len(args) == len(
                        method.param_names
                    ), f"{len(args)} != len({method.param_names})"
                    for param_name, arg in zip(method.param_names, args):
                        body_ctxt.slots[param_name] = arg

                    # TODO: use reified context as default receiver?
                    invoke_compiled(state, method.body, body_ctxt, default_receiver=NullValue())
                elif isinstance(handler, IntrinsicHandler):
                    # Allow the intrinsic handler to take arbitrary control of the runtime. It also takes
                    # responsibility for updating the frame as necessary.
                    handler.handler(state, receiver, *args)
                else:
                    # handler is something which can be called with a context, receiver, and arguments.
                    # It should not call evaluation functions. (It can, but the stack is not reified and
                    # uses host language stack instead).
                    result = handler(frame.context, receiver, *args)
                    assert isinstance(
                        result, Value
                    ), f"Result from builtin handler '{message}' must be a Value; got '{result}'."
                    state.data_stack.append(result)
                    frame.spot += 1
        elif op == "components>vector":
            (length,) = bytecode.args
            assert isinstance(length, int)
            assert 0 <= length <= len(state.data_stack)
            components = state.data_stack[len(state.data_stack) - length :]
            state.data_stack = state.data_stack[: len(state.data_stack) - length]
            state.data_stack.append(VectorValue(components))
            frame.spot += 1
        elif op == "components>tuple":
            (length,) = bytecode.args
            assert isinstance(length, int)
            assert 0 <= length <= len(state.data_stack)
            components = state.data_stack[len(state.data_stack) - length :]
            state.data_stack = state.data_stack[: len(state.data_stack) - length]
            state.data_stack.append(TupleValue(components))
            frame.spot += 1
        elif op == "drop":
            assert not bytecode.args
            state.data_stack.pop()
            frame.spot += 1
        else:
            raise AssertionError(f"Forgot a bytecode op! {op}")
    except Exception as e:
        raise RunError("Error!", state) from e


def invoke_compiled(
    state: RuntimeState, code: BytecodeSequence, ctxt: Context, default_receiver: Value
) -> None:
    assert state.call_stack
    frame = state.call_stack[-1]
    # Tail-call optimization!
    if frame.spot == len(frame.sequence.code) - 1:
        state.call_stack.pop()
    else:
        frame.spot += 1
    state.call_stack.append(
        CallFrame(sequence=code, spot=0, context=ctxt, default_receiver=default_receiver)
    )


def shift_frame(state: RuntimeState) -> None:
    assert state.call_stack
    frame = state.call_stack[-1]
    frame.spot += 1


def eval_toplevel(expr: Expr, context: Context) -> Value:
    bytecode = compile(expr)
    state = RuntimeState(
        # TODO: maybe default_receiver should be a reified global context instead?
        call_stack=[CallFrame(bytecode, spot=0, context=context, default_receiver=NullValue())],
        data_stack=[],
    )
    while state.call_stack:
        eval_one_op(state)
    assert len(state.data_stack) == 1
    return state.data_stack.pop()


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
        invoke_compiled(state, compile(body.body), body.context, default_receiver=NullValue())
    else:
        state.data_stack.append(body)
        shift_frame(state)


def call_impl(message: str, state: RuntimeState, receiver: Value, args: list[Value]):
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
        )
    elif isinstance(receiver, ContinuationValue):
        if len(args) != 1:
            raise ValueError(
                f"{message} receiver (a continuation) requires 1 parameter, but is being provided {len(args)} argument(s)"
            )
        continuation = receiver.state
        state.call_stack = [frame.copy() for frame in continuation.call_stack]
        state.data_stack = list(continuation.data_stack)
        state.data_stack.append(args[0])
    else:
        state.data_stack.append(receiver)
        shift_frame(state)


def intrinsic__call(state: RuntimeState, receiver: Value) -> None:
    call_impl("call", state, receiver, args=[])


def intrinsic__call_(state: RuntimeState, receiver: Value, value: Value) -> None:
    call_impl("call:", state, receiver, args=[value])


def intrinsic__call_star_(state: RuntimeState, receiver: Value, value: Value) -> None:
    if not isinstance(value, TupleValue):
        raise ValueError("call*: value must be a tuple")
    call_impl("call*:", state, receiver, args=value.components)


def intrinsic__call_cc(state: RuntimeState, receiver: Value) -> None:
    current_continuation = ContinuationValue(
        state=RuntimeState(
            call_stack=[frame.copy() for frame in state.call_stack],
            data_stack=list(state.data_stack),
        )
    )
    call_impl("call/cc", state, receiver, args=[current_continuation])


intrinsic_handlers = {
    "if:then:else:": IntrinsicHandler(intrinsic__if_then_else),
    "call": IntrinsicHandler(intrinsic__call),
    "call:": IntrinsicHandler(intrinsic__call_),
    "call*:": IntrinsicHandler(intrinsic__call_star_),
    "call/cc": IntrinsicHandler(intrinsic__call_cc),
}

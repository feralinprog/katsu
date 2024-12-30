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


#################################################
# Runtime State Model
#################################################


@dataclass
class IntrinsicHandler:
    # Doesn't have to immediately produce a result; the handler is provided
    # the current runtime state, a receiver, and arguments, and may update
    # the runtime state arbitrarily. This handler must also update the state's
    # top of stack cursor position as necessary; the interpreter gives up this
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
class BytecodeCursor:
    sequence: BytecodeSequence
    # Next index to execute (or == len(sequence.code) if the next operation
    # is to return.
    spot: int
    context: Context
    # Receiver to use if none is explicitly provided in an invocation.
    default_receiver: Value


@dataclass
class RuntimeState:
    call_stack: list[BytecodeCursor]
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
                # TODO: more narrow span?
                add("drop", span=expr.span)
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
        cursor = state.call_stack[-1]
        assert 0 <= cursor.spot <= len(cursor.sequence.code)

        if cursor.spot == len(cursor.sequence.code):
            # Return from invocation.
            state.call_stack.pop()
            return

        bytecode = cursor.sequence.code[cursor.spot]
        op = bytecode.op

        if op == "push-default-receiver":
            assert not bytecode.args
            state.data_stack.append(cursor.default_receiver)
            cursor.spot += 1
        elif op == "push-number":
            (v,) = bytecode.args
            assert isinstance(v, int)
            state.data_stack.append(NumberValue(v))
            cursor.spot += 1
        elif op == "push-string":
            (v,) = bytecode.args
            assert isinstance(v, str)
            state.data_stack.append(StringValue(v))
            cursor.spot += 1
        elif op == "push-symbol":
            (v,) = bytecode.args
            assert isinstance(v, str)
            state.data_stack.append(SymbolValue(v))
            cursor.spot += 1
        elif op == "push-block":
            (v,) = bytecode.args
            assert isinstance(v, Block)
            state.data_stack.append(v)
            cursor.spot += 1
        elif op == "push-current-context":
            assert not bytecode.args
            state.data_stack.append(cursor.context)
            cursor.spot += 1
        elif op == "context+block>quote":
            assert not bytecode.args
            block = state.data_stack.pop()
            assert isinstance(block, Block)
            ctxt = state.data_stack.pop()
            assert isinstance(ctxt, Context)
            state.data_stack.append(QuoteValue(block.parameters, block.body, ctxt, span=block.span))
            cursor.spot += 1
        elif op == "invoke":
            message, nargs = bytecode.args
            assert isinstance(message, str)
            assert isinstance(nargs, int)
            assert len(state.data_stack) >= nargs + 1

            # Find the handler:
            ctxt = cursor.context
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
                cursor.spot += 1
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

                    invoke_compiled(state, method.body, body_ctxt, default_receiver=NullValue())
                elif isinstance(handler, IntrinsicHandler):
                    # Allow the intrinsic handler to take arbitrary control of the runtime. It also takes
                    # responsibility for updating the cursor as necessary.
                    handler.handler(state, receiver, *args)
                else:
                    # handler is something which can be called with a context, receiver, and arguments.
                    # It should not call evaluation functions. (It can, but the stack is not reified and
                    # uses host language stack instead).
                    result = handler(cursor.context, receiver, *args)
                    assert isinstance(
                        result, Value
                    ), f"Result from builtin handler '{message}' must be a Value; got '{result}'."
                    state.data_stack.append(result)
                    cursor.spot += 1
        elif op == "components>vector":
            (length,) = bytecode.args
            assert isinstance(length, int)
            assert 0 <= length <= len(state.data_stack)
            components = state.data_stack[len(state.data_stack) - length :]
            state.data_stack = state.data_stack[: len(state.data_stack) - length]
            state.data_stack.append(VectorValue(components))
            cursor.spot += 1
        elif op == "components>tuple":
            (length,) = bytecode.args
            assert isinstance(length, int)
            assert 0 <= length <= len(state.data_stack)
            components = state.data_stack[len(state.data_stack) - length :]
            state.data_stack = state.data_stack[: len(state.data_stack) - length]
            state.data_stack.append(TupleValue(components))
            cursor.spot += 1
        elif op == "drop":
            assert not bytecode.args
            state.data_stack.pop()
            cursor.spot += 1
        else:
            raise AssertionError(f"Forgot a bytecode op! {op}")
    except Exception as e:
        raise RunError("Error!", state) from e


def invoke_compiled(
    state: RuntimeState, code: BytecodeSequence, ctxt: Context, default_receiver: Value
) -> None:
    assert state.call_stack
    cursor = state.call_stack[-1]
    # Tail-call optimization!
    if cursor.spot == len(cursor.sequence.code) - 1:
        state.call_stack.pop()
    else:
        cursor.spot += 1
    state.call_stack.append(
        BytecodeCursor(sequence=code, spot=0, context=ctxt, default_receiver=default_receiver)
    )


def shift_cursor(state: RuntimeState) -> None:
    assert state.call_stack
    cursor = state.call_stack[-1]
    cursor.spot += 1


def eval_toplevel(expr: Expr, context: Context) -> Value:
    bytecode = compile(expr)
    state = RuntimeState(
        # TODO: maybe default_receiver should be a reified global context instead?
        call_stack=[
            BytecodeCursor(bytecode, spot=0, context=context, default_receiver=NullValue())
        ],
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
        # TODO: compile ahead of time somehow...
        invoke_compiled(state, compile(body.body), body.context, default_receiver=NullValue())
    else:
        state.data_stack.append(body)
        shift_cursor(state)


def intrinsic__call(state: RuntimeState, receiver: Value) -> None:
    assert receiver
    if isinstance(receiver, QuoteValue):
        if len(receiver.parameters) != 0:
            raise ValueError("call receiver requires parameter(s)")
        # TODO: compile ahead of time somehow...
        invoke_compiled(
            state,
            compile(receiver.body),
            Context(slots={}, base=receiver.context),
            default_receiver=NullValue(),
        )
    else:
        state.data_stack.append(receiver)
        shift_cursor(state)


def intrinsic__call_(state: RuntimeState, receiver: Value, value: Value) -> None:
    if isinstance(receiver, QuoteValue):
        if len(receiver.parameters) == 0:
            new_slots = {"it": value}
            default_receiver = value
        elif len(receiver.parameters) == 1:
            new_slots = {receiver.parameters[0]: value}
            default_receiver = NullValue()
        else:
            raise ValueError("call: receiver requires multiple parameters")
        # TODO: compile ahead of time somehow...
        invoke_compiled(
            state,
            compile(receiver.body),
            Context(slots=new_slots, base=receiver.context),
            default_receiver=default_receiver,
        )
    else:
        state.data_stack.append(receiver)
        shift_cursor(state)


def intrinsic__call_star_(state: RuntimeState, receiver: Value, value: Value) -> None:
    if not isinstance(value, TupleValue):
        raise ValueError("call*: value must be a tuple")
    if isinstance(receiver, QuoteValue):
        if len(value.components) != len(receiver.parameters):
            raise ValueError(
                f"call*: receiver takes parameters '{' '.join(receiver.parameters)}', but provided tuple has {len(value.components)} component(s)"
            )
        new_slots = {
            parameter: component
            for parameter, component in zip(receiver.parameters, value.components)
        }
        # TODO: compile ahead of time somehow...
        invoke_compiled(
            state,
            compile(receiver.body),
            Context(slots=new_slots, base=receiver.context),
            default_receiver=NullValue(),
        )
    else:
        state.data_stack.append(receiver)
        shift_cursor(state)


intrinsic_handlers = {
    "if:then:else:": IntrinsicHandler(intrinsic__if_then_else),
    "call": IntrinsicHandler(intrinsic__call),
    "call:": IntrinsicHandler(intrinsic__call_),
    "call*:": IntrinsicHandler(intrinsic__call_star_),
}

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
    expr: Expr
    context: "Context"

    def __str__(self):
        # TODO: pprint, make less verbose
        return "{ " + str(self.expr) + " }"


#################################################
# Runtime State Model
#################################################


@dataclass
class Context:
    # Each definition is either a Value, or a python callable of the form
    #   (self, ctxt: Context, receiver: Optional[Value], *args: list[Value]) -> Value
    definitions: dict[str, Union[Value, Callable[["Context", Optional[Value], list[Value]], Value]]]
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
# push-None                               (need to rethink this)
# push-number <number>
# push-string <str>
# push-symbol <str>
# push-expr <Expr>
# push-current-context
# context+expr>quote
# invoke <message: str> <nargs: int>      (nargs does not include receiver)
# components>vector <length: int>
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


@dataclass
class RuntimeState:
    control_stack: list[BytecodeCursor]
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
        # TODO: None vs. NullValue() seems a bit awkward.
        add("push-None", span=expr.op.span)
        compile_into(expr.left, sequence)
        compile_into(expr.right, sequence)
        add("invoke", expr.op.value + ":_:", 2, span=expr.span)
    elif isinstance(expr, NameExpr):
        add("push-None", span=expr.name.span)
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
            add("push-None", span=expr.span)
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
        add("push-expr", expr.inner, span=expr.inner.span)
        add("context+expr>quote", span=expr.span)
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
        raise NotImplementedError()
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
    assert state
    assert state.control_stack
    cursor = state.control_stack[-1]
    assert 0 <= cursor.spot <= len(cursor.sequence.code)

    if cursor.spot == len(cursor.sequence.code):
        # Return from invocation.
        state.control_stack.pop()
        return

    bytecode = cursor.sequence.code[cursor.spot]
    op = bytecode.op

    if op == "push-None":
        assert not bytecode.args
        state.data_stack.append(None)
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
    elif op == "push-expr":
        (v,) = bytecode.args
        assert isinstance(v, Expr)
        state.data_stack.append(v)
        cursor.spot += 1
    elif op == "push-current-context":
        assert not bytecode.args
        state.data_stack.append(cursor.context)
        cursor.spot += 1
    elif op == "context+expr>quote":
        assert not bytecode.args
        expr = state.data_stack.pop()
        assert isinstance(expr, Expr)
        ctxt = state.data_stack.pop()
        assert isinstance(ctxt, Context)
        state.data_stack.append(QuoteValue(expr, ctxt))
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
            if message in ctxt.definitions:
                handler = ctxt.definitions[message]
            ctxt = ctxt.base
        if not handler:
            raise RunError(
                f"Could not invoke message; no handler defined for '{message}'.", bytecode.span
            )

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

                body_ctxt = Context(definitions={}, base=method.context)
                body_ctxt.definitions[method.receiver_name] = receiver or NullValue()
                assert len(args) == len(
                    method.param_names
                ), f"{len(args)} != len({method.param_names})"
                for param_name, arg in zip(method.param_names, args):
                    body_ctxt.definitions[param_name] = arg

                # Tail-call optimization!
                if cursor.spot == len(cursor.sequence.code) - 1:
                    state.control_stack.pop()
                else:
                    cursor.spot += 1
                state.control_stack.append(
                    BytecodeCursor(sequence=method.body, spot=0, context=body_ctxt)
                )

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
    elif op == "drop":
        assert not bytecode.args
        state.data_stack.pop()
        cursor.spot += 1
    else:
        raise AssertionError(f"Forgot a bytecode op! {op}")


def eval(expr: Expr, context: Context) -> Value:
    bytecode = compile(expr)
    state = RuntimeState(
        control_stack=[BytecodeCursor(bytecode, spot=0, context=context)],
        data_stack=[],
    )
    while state.control_stack:
        eval_one_op(state)
    assert len(state.data_stack) == 1
    return state.data_stack.pop()

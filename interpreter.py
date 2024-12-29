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
from typing import Callable, Optional, Union

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


#################################################
# Runtime Interpreter / Evaluation
#################################################


def eval(expr: Expr, ctxt: Context) -> Value:
    assert expr is not None
    if isinstance(expr, UnaryOpExpr):
        return message_invoke(expr.span, ctxt, expr.op.value + ":", eval(expr.arg, ctxt), [])
    elif isinstance(expr, BinaryOpExpr):
        return message_invoke(
            expr.span, ctxt, expr.op.value, eval(expr.left, ctxt), [eval(expr.right, ctxt)]
        )
    elif isinstance(expr, NameExpr):
        return message_invoke(expr.span, ctxt, expr.name.value, None, [])
    elif isinstance(expr, LiteralExpr):
        return literal_to_value(expr.literal)
    elif isinstance(expr, UnaryMessageExpr):
        return message_invoke(expr.span, ctxt, expr.message.value, eval(expr.target, ctxt), [])
    elif isinstance(expr, NAryMessageExpr):
        return message_invoke(
            expr.span,
            ctxt,
            "".join(message.value + ":" for message in expr.messages),
            eval(expr.target, ctxt) if expr.target is not None else None,
            [eval(arg, ctxt) for arg in expr.args],
        )
    elif isinstance(expr, ParenExpr):
        return eval(expr.inner, ctxt)
    elif isinstance(expr, QuoteExpr):
        return QuoteValue(expr=expr.inner, context=ctxt)
    elif isinstance(expr, DataExpr):
        return VectorValue([eval(component, ctxt) for component in expr.components])
    elif isinstance(expr, SequenceExpr):
        assert expr.sequence != []
        for part in expr.sequence:
            last_value = eval(part, ctxt)
        return last_value
    elif isinstance(expr, TupleExpr):
        return TupleValue([eval(component, ctxt) for component in expr.components])
    else:
        raise AssertionError(f"Forgot an expression type! {type(expr)}")


def literal_to_value(literal: Token) -> Value:
    if literal._type == TokenType.SYMBOL:
        return SymbolValue(literal.value)
    elif literal._type == TokenType.NUMBER:
        return NumberValue(literal.value)
    elif literal._type == TokenType.STRING:
        return StringValue(literal.value)
    else:
        raise AssertionError(f"Forgot a literal token type! {literal._type} ({literal})")


def message_invoke(
    source_span: SourceSpan,
    ctxt: Context,
    message: str,
    receiver: Optional[Value],
    args: list[Value],
) -> Value:
    try:
        handler = lookup_handler(ctxt, message)
        if isinstance(handler, Value):
            return handler
        else:
            # handler is something which can be called with a context, receiver, and arguments
            result = handler(ctxt, receiver, *args)
            assert isinstance(result, Value), f"Message result '{result}' is not a Value!"
            return result
    except Exception as exc:
        raise RunError("Couldn't evaluate message.", span=source_span) from exc


def lookup_handler(ctxt: Context, message: str):
    while ctxt is not None:
        if message in ctxt.definitions:
            return ctxt.definitions[message]
        ctxt = ctxt.base
    raise KeyError(f"No handler for {message} found.")

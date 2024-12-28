from dataclasses import dataclass
from parser import (
    BinaryOpExpr,
    BlockExpr,
    Expr,
    LiteralExpr,
    NameExpr,
    NAryMessageExpr,
    ParenExpr,
    SequenceExpr,
    UnaryMessageExpr,
    UnaryOpExpr,
)
from typing import Any, Callable, Optional, Tuple, Type, Union

from error import RunError
from lexer import Token, TokenType
from span import SourceSpan


@dataclass
class Context:
    # Each definition is either a Value, or a python callable of the form
    #   (self, ctxt: Context, receiver: Optional[Value], *args: list[Value]) -> Value
    definitions: dict[
        str, Union["Value", Callable[["Context", Optional["Value"], list["Value"]], "Value"]]
    ]
    base: Optional["Context"]


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
class ContextValue(Value):
    context: Context

    def __str__(self):
        return "<a context>"


@dataclass
class ExprValue(Value):
    expr: Expr
    context: Context

    def __str__(self):
        # TODO: pprint, make less verbose
        return "{ " + str(self.expr) + " }"


def eval(expr: Expr, ctxt: Context) -> Value:
    assert expr is not None
    if isinstance(expr, UnaryOpExpr):
        return message_invoke(expr.span, ctxt, expr.op.value, eval(expr.arg, ctxt), [])
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
    elif isinstance(expr, BlockExpr):
        return ExprValue(expr=expr.inner, context=ctxt)
    elif isinstance(expr, SequenceExpr):
        assert expr.sequence != []
        for part in expr.sequence:
            last_value = eval(part, ctxt)
        return last_value
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


##################################

global_context = Context(definitions={}, base=None)


def builtin(name: str, handler):
    assert name not in global_context.definitions, f"{name} already is defined as a builtin."
    global_context.definitions[name] = handler


@dataclass
class Method:
    context: Context
    message: str
    receiver_name: str
    param_names: list[str]
    body: Expr

    def __call__(self, ctxt: Context, receiver: Optional[Value], *args: list[Value]) -> Value:
        body_ctxt = Context(definitions={}, base=ctxt)
        body_ctxt.definitions[self.receiver_name] = receiver or NullValue()
        if ":" in self.message:
            parts = self.message.split(":")
            assert parts[-1] == ""
            parts = parts[:-1]
            assert len(self.param_names) == len(parts)
        else:
            assert len(self.param_names) == 1
            body_ctxt.definitions[self.param_names[0]]
        assert len(args) == len(self.param_names)
        for param_name, arg in zip(self.param_names, args):
            body_ctxt.definitions[param_name] = arg
        return eval(self.body, body_ctxt)


def handle__method_does_(
    ctxt: Context, receiver: Optional[Value], decl: Value, body: Value
) -> Value:
    # print(ctxt, receiver, decl, body)
    if receiver:
        # TODO: set ctxt to the receiver if the receiver is a ContextValue?
        raise ValueError("method:does: does not take a receiver")
    if isinstance(decl, ExprValue):
        if isinstance(decl.expr, NameExpr):
            message = decl.expr.name.value
            receiver_name = "self"
            param_names = []
        elif isinstance(decl.expr, UnaryMessageExpr):
            if isinstance(decl.expr.target, NameExpr):
                message = decl.expr.message.value
                receiver_name = decl.expr.target.name.value
                param_names = []
            else:
                raise ValueError(
                    "When the method:does: 'declaration' argument is a unary message, it must be a simple unary message of the form {target-name message-name}"
                )
        elif isinstance(decl.expr, NAryMessageExpr):
            if decl.expr.target:
                if not isinstance(decl.expr.target, NameExpr):
                    raise ValueError(
                        "When the method:does: 'declaration' argument is an n-ary message, it must be a simple n-ary message of the form {[target-name] message: param-name ...}"
                    )
            for arg in decl.expr.args:
                if not isinstance(arg, NameExpr):
                    raise ValueError(
                        "When the method:does: 'declaration' argument is an n-ary message, it must be a simple n-ary message of the form {[target-name] message: param-name ...}"
                    )
            message = "".join(message.value + ":" for message in decl.expr.messages)
            receiver_name = decl.expr.target.name.value if decl.expr.target else "self"
            param_names = [arg.name.value for arg in decl.expr.args]
        else:
            raise ValueError(
                f"method:does: 'declaration' argument should be a quoted name or message; got {decl.expr}"
            )

        # Fill in the body and context, then define the method.
        if not isinstance(body, ExprValue):
            raise ValueError("method:does: 'body' argument should be a block")

        # print("MESSAGE:", message)

        method = Method(body.context, message, receiver_name, param_names, body.expr)
        if message in ctxt.definitions:
            raise ValueError(f"Message '{message}' is already defined.")
        ctxt.definitions[message] = method
        return NullValue()
    else:
        raise ValueError("method:does: 'declaration' argument should be a quoted name or message")


builtin("method:does:", handle__method_does_)


def handle__local_is_(ctxt: Context, receiver: Optional[Value], decl: Value, value: Value) -> Value:
    if receiver:
        raise ValueError("local:is: does not take a receiver")
    if isinstance(decl, ExprValue):
        if isinstance(decl.expr, NameExpr):
            local_name = decl.expr.name.value
        else:
            raise ValueError(
                f"local:is: 'declaration' argument should be a symbol or quoted name; got {decl}"
            )
    elif isinstance(decl, SymbolValue):
        local_name = decl.symbol
    else:
        raise ValueError("local:is: 'declaration' argument should be a symbol or quoted name")

    if local_name in ctxt.definitions:
        raise ValueError(f"Message '{local_name}' is already defined.")
    ctxt.definitions[local_name] = value
    return value


builtin("local:is:", handle__local_is_)


def generic_binary_op_handler(
    op: str,
    handlers: list[Tuple[Type, Type, Callable[[Any, Any], Value]]],
    default_handler: Optional[Callable[[Value, Value], Value]] = None,
):
    def handle__generic_binary_op(ctxt: Context, left: Optional[Value], right: Value) -> Value:
        assert left
        for left_type, right_type, handler in handlers:
            if isinstance(left, left_type) and isinstance(right, right_type):
                return handler(left, right)
        if default_handler:
            return default_handler(left, right)
        else:
            raise ValueError(f"Invalid input types for '{op}': {left}, {right}")

    return handle__generic_binary_op


def builtin_binary_op(op, handlers, default_handler=None):
    builtin(op, generic_binary_op_handler(op, handlers, default_handler))


builtin_binary_op(
    "~",
    [
        (StringValue, StringValue, (lambda a, b: StringValue(a.value + b.value))),
    ],
)
builtin_binary_op(
    "+",
    [
        (NumberValue, NumberValue, (lambda a, b: NumberValue(a.value + b.value))),
    ],
)
builtin_binary_op(
    "-",
    [
        (NumberValue, NumberValue, (lambda a, b: NumberValue(a.value - b.value))),
    ],
)
builtin_binary_op(
    "*",
    [
        (NumberValue, NumberValue, (lambda a, b: NumberValue(a.value * b.value))),
    ],
)
builtin_binary_op(
    "/",
    [
        (NumberValue, NumberValue, (lambda a, b: NumberValue(a.value / b.value))),
    ],
)

builtin_binary_op(
    "==",
    [
        (Value, Value, (lambda a, b: BoolValue(a.value == b.value))),
    ],
)
builtin_binary_op(
    "!=",
    [
        (Value, Value, (lambda a, b: BoolValue(a.value == b.value))),
    ],
)

builtin_binary_op(
    "<",
    [
        (NumberValue, NumberValue, (lambda a, b: BoolValue(a.value < b.value))),
    ],
)
builtin_binary_op(
    "<=",
    [
        (NumberValue, NumberValue, (lambda a, b: BoolValue(a.value <= b.value))),
    ],
)
builtin_binary_op(
    ">",
    [
        (NumberValue, NumberValue, (lambda a, b: BoolValue(a.value > b.value))),
    ],
)
builtin_binary_op(
    ">=",
    [
        (NumberValue, NumberValue, (lambda a, b: BoolValue(a.value >= b.value))),
    ],
)


def handle__print_(ctxt: Context, receiver: Optional[Value], value: Value) -> Value:
    if receiver:
        raise ValueError("print: does not take a receiver")
    print(value)
    return NullValue()


builtin("print:", handle__print_)

builtin("t", BoolValue(True))
builtin("f", BoolValue(False))
builtin("null", NullValue())


def handle__to_string(ctxt: Context, value: Value) -> Value:
    return StringValue(str(value))


builtin(">string", handle__to_string)


# def handle__if_then_(ctxt: Context, receiver: Optional[Value], cond: Value, tbody: Value) -> Value:
#    if receiver:
#        raise ValueError("if:then: does not take a receiver")
#    if isinstance(cond, BoolValue) and cond.value:
#        if isinstance(tbody, ExprValue):
#            return eval(tbody.expr, tbody.context)
#        else:
#            return tbody
#    else:
#        return NullValue()
def handle__if_then_else_(
    ctxt: Context, receiver: Optional[Value], cond: Value, tbody: Value, fbody: Value
) -> Value:
    if receiver:
        raise ValueError("if:then:else: does not take a receiver")
    if isinstance(cond, BoolValue) and cond.value:
        if isinstance(tbody, ExprValue):
            return eval(tbody.expr, tbody.context)
        else:
            return tbody
    else:
        if isinstance(fbody, ExprValue):
            return eval(fbody.expr, fbody.context)
        else:
            return fbody


builtin("if:then:else:", handle__if_then_else_)

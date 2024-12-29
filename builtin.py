from parser import NameExpr, NAryMessageExpr, UnaryMessageExpr
from typing import Any, Callable, Optional, Tuple, Type

from interpreter import (
    BoolValue,
    Context,
    Method,
    NullValue,
    NumberValue,
    QuoteValue,
    StringValue,
    Value,
    VectorValue,
    eval,
)

global_context = Context(slots={}, base=None)


def builtin(name: str, handler):
    assert name not in global_context.slots, f"{name} already is defined as a builtin."
    global_context.slots[name] = handler


def handle__method_does_(
    ctxt: Context, receiver: Optional[Value], decl: Value, body: Value
) -> Value:
    # print(ctxt, receiver, decl, body)
    if receiver:
        # TODO: set ctxt to the receiver if the receiver is some sort of reified context value?
        raise ValueError("method:does: does not take a receiver")
    if isinstance(decl, QuoteValue):
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
        if not isinstance(body, QuoteValue):
            raise ValueError("method:does: 'body' argument should be a block")

        method = Method(
            context=body.context,
            receiver_name=receiver_name,
            param_names=param_names,
            body_expr=body.expr,
            body=None,
        )
        if message in ctxt.slots:
            raise ValueError(f"Message '{message}' is already defined.")
        ctxt.slots[message] = method
        return NullValue()
    else:
        raise ValueError("method:does: 'declaration' argument should be a quoted name or message")


builtin("method:does:", handle__method_does_)


def handle__local_is_(ctxt: Context, receiver: Optional[Value], decl: Value, value: Value) -> Value:
    if receiver:
        raise ValueError("local:is: does not take a receiver")
    if isinstance(decl, QuoteValue):
        if isinstance(decl.expr, NameExpr):
            local_name = decl.expr.name.value
        else:
            raise ValueError(
                f"local:is: 'declaration' argument should be a quoted name; got {decl}"
            )
    else:
        raise ValueError("local:is: 'declaration' argument should be a quoted name")

    if local_name in ctxt.slots:
        raise ValueError(f"Message '{local_name}' is already defined.")
    ctxt.slots[local_name] = value
    return value


builtin("local:is:", handle__local_is_)


# TODO: alias? or just fully rename...
def handle__let_eq_(ctxt: Context, receiver: Optional[Value], decl: Value, value: Value) -> Value:
    if receiver:
        raise ValueError("let:=: does not take a receiver")
    if isinstance(decl, QuoteValue):
        if isinstance(decl.expr, NameExpr):
            local_name = decl.expr.name.value
        else:
            raise ValueError(f"let:=: 'declaration' argument should be a quoted name; got {decl}")
    else:
        raise ValueError("let:=: 'declaration' argument should be a quoted name")

    if local_name in ctxt.slots:
        raise ValueError(f"Message '{local_name}' is already defined.")
    ctxt.slots[local_name] = value
    return value


builtin("let:=:", handle__local_is_)


def handle__set(ctxt: Context, receiver: Optional[Value], slot: Value, value: Value) -> Value:
    if receiver:
        raise ValueError("=:_: takes no receiver")
    if isinstance(slot, QuoteValue):
        if isinstance(slot.expr, NameExpr):
            slot = slot.expr.name.value
        else:
            raise ValueError(f"=:_: 'slot' argument should be a quoted name; got {slot}")
    else:
        raise ValueError("=:_: receiver should be a quoted name")

    while ctxt and slot not in ctxt.slots:
        ctxt = ctxt.base
    if not ctxt:
        raise ValueError(f"'{slot}' is not yet defined.")
    ctxt.slots[slot] = value
    return value


builtin("=:_:", handle__set)


def generic_unary_op_handler(
    op: str,
    handlers: list[Tuple[Type, Callable[[Any], Value]]],
    default_handler: Optional[Callable[[Value], Value]] = None,
):
    def handle__generic_unary_op(ctxt: Context, receiver: Optional[Value]) -> Value:
        assert receiver
        for receiver_type, handler in handlers:
            if isinstance(receiver, receiver_type):
                return handler(receiver)
        if default_handler:
            return default_handler(receiver)
        else:
            raise ValueError(f"Invalid input types for '{op}': {receiver}")

    return handle__generic_unary_op


def builtin_unary_op(op, handlers, default_handler=None):
    builtin(op, generic_unary_op_handler(op, handlers, default_handler))


def generic_binary_op_handler(
    op: str,
    handlers: list[Tuple[Type, Type, Callable[[Any, Any], Value]]],
    default_handler: Optional[Callable[[Value, Value], Value]] = None,
):
    def handle__generic_binary_op(
        ctxt: Context, receiver: Optional[Value], left: Value, right: Value
    ) -> Value:
        if receiver:
            raise ValueError(f"{op} takes no receiver")
        for left_type, right_type, handler in handlers:
            if isinstance(left, left_type) and isinstance(right, right_type):
                return handler(left, right)
        if default_handler:
            return default_handler(left, right)
        else:
            raise ValueError(f"Invalid input types for '{op}': {left}, {right}")

    return handle__generic_binary_op


def builtin_binary_op(op, handlers, default_handler=None):
    builtin(op + ":_:", generic_binary_op_handler(op, handlers, default_handler))


builtin_binary_op(
    "~",
    [
        (StringValue, StringValue, (lambda a, b: StringValue(a.value + b.value))),
    ],
)

builtin_binary_op(
    "and",
    [
        (BoolValue, BoolValue, (lambda a, b: BoolValue(a.value and b.value))),
    ],
)
builtin_binary_op(
    "or",
    [
        (BoolValue, BoolValue, (lambda a, b: BoolValue(a.value or b.value))),
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

builtin_unary_op(
    "not",
    [
        (BoolValue, (lambda v: BoolValue(not v.value))),
    ],
)
builtin_unary_op(
    "+",
    [
        (NumberValue, (lambda v: NumberValue(+v.value))),
    ],
)
builtin_unary_op(
    "-",
    [
        (NumberValue, (lambda v: NumberValue(-v.value))),
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
#        if isinstance(tbody, QuoteValue):
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
        if isinstance(tbody, QuoteValue):
            return eval(tbody.expr, tbody.context)
        else:
            return tbody
    else:
        if isinstance(fbody, QuoteValue):
            return eval(fbody.expr, fbody.context)
        else:
            return fbody


builtin("if:then:else:", handle__if_then_else_)


def handle__eval(ctxt: Context, receiver: Optional[Value]) -> Value:
    assert receiver
    if isinstance(receiver, QuoteValue):
        return eval(receiver.expr, receiver.context)
    else:
        return receiver


builtin("eval", handle__eval)


def handle__eval_with_eq_(
    ctxt: Context, receiver: Optional[Value], slot: Value, value: Value
) -> Value:
    if isinstance(slot, QuoteValue):
        if isinstance(slot.expr, NameExpr):
            slot = slot.expr.name.value
        else:
            raise ValueError(f"eval-with:=: 'slot' should be a quoted name; got {slot}")
    else:
        raise ValueError(f"eval-with:=: 'slot' should be or quoted name; got {slot}")

    if isinstance(receiver, QuoteValue):
        return eval(receiver.expr, Context(slots={slot: value}, base=receiver.context))
    else:
        return receiver


builtin("eval-with:=:", handle__eval_with_eq_)


def handle__each_(ctxt: Context, receiver: Optional[Value], action: Value) -> Value:
    if not receiver:
        raise ValueError("each: requires a receiver")
    if isinstance(receiver, VectorValue):
        if isinstance(action, QuoteValue):
            last = NullValue()
            for component in receiver.components:
                last = eval(action.expr, Context({"it": component}, base=action.context))
            return last
        else:
            raise ValueError(f"each: action must be a block; got {action}")
    else:
        raise ValueError(f"each: requires a vector; got {receiver}")


builtin("each:", handle__each_)


def handle__at_(ctxt: Context, receiver: Optional[Value], index: Value) -> Value:
    if not receiver:
        raise ValueError("at: requires a receiver")
    if isinstance(receiver, VectorValue):
        if isinstance(index, NumberValue):
            return receiver.components[index.value]
        else:
            raise ValueError(f"at: index must be a number; got {index}")
    else:
        raise ValueError(f"at: requires a vector; got {receiver}")


builtin("at:", handle__at_)


def handle__at_eq_(ctxt: Context, receiver: Optional[Value], index: Value, value: Value) -> Value:
    if not receiver:
        raise ValueError("at:=: requires a receiver")
    if isinstance(receiver, VectorValue):
        if isinstance(index, NumberValue):
            receiver.components[index.value] = value
            return value
        else:
            raise ValueError(f"at: index must be a number; got {index}")
    else:
        raise ValueError(f"at: requires a vector; got {receiver}")


builtin("at:=:", handle__at_eq_)


def handle__append_(ctxt: Context, receiver: Optional[Value], value: Value) -> Value:
    if not receiver:
        raise ValueError("append: requires a receiver")
    if isinstance(receiver, VectorValue):
        receiver.components.append(value)
        return NullValue()
    else:
        raise ValueError(f"append: requires a vector; got {receiver}")


builtin("append:", handle__append_)

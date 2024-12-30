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
    intrinsic_handlers,
)

global_context = Context(slots={}, base=None)
for slot, handler in intrinsic_handlers.items():
    global_context.slots[slot] = handler


def builtin(name: str, handler):
    assert name not in global_context.slots, f"{name} already is defined as a builtin."
    global_context.slots[name] = handler


def handle__method_does_(ctxt: Context, receiver: Value, decl: Value, body: Value) -> Value:
    # TODO: set ctxt to the receiver if the receiver is some sort of reified context value?
    if isinstance(decl, QuoteValue):
        if isinstance(decl.body, NameExpr):
            message = decl.body.name.value
            receiver_name = "self"
            param_names = []
        elif isinstance(decl.body, UnaryMessageExpr):
            if isinstance(decl.body.target, NameExpr):
                message = decl.body.message.value
                receiver_name = decl.body.target.name.value
                param_names = []
            else:
                raise ValueError(
                    "When the method:does: 'declaration' argument is a unary message, it must be a simple unary message of the form {target-name message-name}"
                )
        elif isinstance(decl.body, NAryMessageExpr):
            if decl.body.target:
                if not isinstance(decl.body.target, NameExpr):
                    raise ValueError(
                        "When the method:does: 'declaration' argument is an n-ary message, it must be a simple n-ary message of the form {[target-name] message: param-name ...}"
                    )
            for arg in decl.body.args:
                if not isinstance(arg, NameExpr):
                    raise ValueError(
                        "When the method:does: 'declaration' argument is an n-ary message, it must be a simple n-ary message of the form {[target-name] message: param-name ...}"
                    )
            message = "".join(message.value + ":" for message in decl.body.messages)
            receiver_name = decl.body.target.name.value if decl.body.target else "self"
            param_names = [arg.name.value for arg in decl.body.args]
        else:
            raise ValueError(
                f"method:does: 'declaration' argument should be a quoted name or message; got {decl.body}"
            )

        if decl.parameters:
            raise ValueError(
                "method:does: 'declaration' argument should not specify any parameters"
            )

        # Fill in the body and context, then define the method.
        if not isinstance(body, QuoteValue):
            raise ValueError("method:does: 'body' argument should be a quote")
        if body.parameters:
            raise ValueError("method:does: 'body' argument should not specify any parameters")

        # TODO: compile here instead of on-demand in the 'invoke' bytecode evaluator.
        method = Method(
            context=body.context,
            receiver_name=receiver_name,
            param_names=param_names,
            body_expr=body.body,
            body=None,
        )
        if message in ctxt.slots:
            raise ValueError(f"Message '{message}' is already defined.")
        ctxt.slots[message] = method
        return NullValue()
    else:
        raise ValueError("method:does: 'declaration' argument should be a quoted name or message")


builtin("method:does:", handle__method_does_)


def handle__local_is_(ctxt: Context, receiver: Value, decl: Value, value: Value) -> Value:
    # TODO: set ctxt to the receiver if the receiver is some sort of reified context value?
    if isinstance(decl, QuoteValue):
        if isinstance(decl.body, NameExpr):
            local_name = decl.body.name.value
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
def handle__let_eq_(ctxt: Context, receiver: Value, decl: Value, value: Value) -> Value:
    # TODO: set ctxt to the receiver if the receiver is some sort of reified context value?
    if isinstance(decl, QuoteValue):
        if isinstance(decl.body, NameExpr):
            local_name = decl.body.name.value
        else:
            raise ValueError(f"let:=: 'declaration' argument should be a quoted name; got {decl}")
    else:
        raise ValueError("let:=: 'declaration' argument should be a quoted name")

    if local_name in ctxt.slots:
        raise ValueError(f"Message '{local_name}' is already defined.")
    ctxt.slots[local_name] = value
    return value


builtin("let:=:", handle__local_is_)


def handle__set(ctxt: Context, receiver: Value, slot: Value, value: Value) -> Value:
    # TODO: set ctxt to the receiver if the receiver is some sort of reified context value?
    if isinstance(slot, QuoteValue):
        if isinstance(slot.body, NameExpr):
            slot = slot.body.name.value
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
    def handle__generic_unary_op(ctxt: Context, receiver: Value) -> Value:
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
        ctxt: Context, receiver: Value, left: Value, right: Value
    ) -> Value:
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


def handle__print(ctxt: Context, receiver: Value) -> Value:
    print(receiver)
    return NullValue()


builtin("print", handle__print)


def handle__print_(ctxt: Context, receiver: Value, value: Value) -> Value:
    print(value)
    return NullValue()


builtin("print:", handle__print_)

builtin("t", BoolValue(True))
builtin("f", BoolValue(False))
builtin("null", NullValue())


def handle__to_string(ctxt: Context, receiver: Value) -> Value:
    return StringValue(str(receiver))


builtin(">string", handle__to_string)


def handle__at_(ctxt: Context, receiver: Value, index: Value) -> Value:
    if isinstance(receiver, VectorValue):
        if isinstance(index, NumberValue):
            return receiver.components[index.value]
        else:
            raise ValueError(f"at: index must be a number; got {index}")
    else:
        raise ValueError(f"at: requires a vector; got {receiver}")


builtin("at:", handle__at_)


def handle__at_eq_(ctxt: Context, receiver: Value, index: Value, value: Value) -> Value:
    if isinstance(receiver, VectorValue):
        if isinstance(index, NumberValue):
            receiver.components[index.value] = value
            return value
        else:
            raise ValueError(f"at: index must be a number; got {index}")
    else:
        raise ValueError(f"at: requires a vector; got {receiver}")


builtin("at:=:", handle__at_eq_)


def handle__append_(ctxt: Context, receiver: Value, value: Value) -> Value:
    if isinstance(receiver, VectorValue):
        receiver.components.append(value)
        return receiver
    else:
        raise ValueError(f"append: requires a vector; got {receiver}")


builtin("append:", handle__append_)


def handle__length(ctxt: Context, receiver: Value) -> Value:
    if isinstance(receiver, VectorValue):
        return NumberValue(len(receiver.components))
    elif isinstance(receiver, StringValue):
        return NumberValue(len(receiver.value))
    else:
        raise ValueError(f"length requires a vector or string; got {receiver}")


builtin("length", handle__length)


def handle__show_current_context(ctxt: Context, receiver: Value) -> Value:
    print("==== showing current context ====")
    while ctxt.base:
        print("context:")
        for slot, value in ctxt.slots.items():
            print(f":: {slot} = {value}")
        ctxt = ctxt.base
    print("<global context>")
    print("============== done =============")
    return NullValue()


builtin("<show-current-context>", handle__show_current_context)

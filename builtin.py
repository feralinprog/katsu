from parser import NameExpr, NAryMessageExpr, ParenExpr, UnaryMessageExpr
from typing import Any, Callable, Optional, Tuple, Type

from interpreter import (
    BoolValue,
    Context,
    ContinuationValue,
    DataclassTypeValue,
    DataclassValue,
    Method,
    NativeHandler,
    NullValue,
    NumberValue,
    QuoteValue,
    StringValue,
    SymbolValue,
    TupleValue,
    TypeValue,
    Value,
    VectorValue,
    eval_toplevel,
    intrinsic_handlers,
)

global_context = Context(slots={}, base=None)
for slot, handler in intrinsic_handlers.items():
    global_context.slots[slot] = handler


def builtin_value(name: str, value):
    assert name not in global_context.slots, f"{name} already is defined as a builtin."
    global_context.slots[name] = value


def builtin(name: str, handler):
    builtin_value(name, NativeHandler(handler))


ObjectType = TypeValue("Object", bases=[])
NumberType = TypeValue("Number", bases=[ObjectType])
StringType = TypeValue("String", bases=[ObjectType])
BoolType = TypeValue("Bool", bases=[ObjectType])
NullType = TypeValue("Null", bases=[ObjectType])
SymbolType = TypeValue("Symbol", bases=[ObjectType])
VectorType = TypeValue("Vector", bases=[ObjectType])
TupleType = TypeValue("Tuple", bases=[ObjectType])
QuoteType = TypeValue("Quote", bases=[ObjectType])
ContinuationType = TypeValue("Continuation", bases=[ObjectType])
TypeType = TypeValue("Type", bases=[ObjectType])
DataclassTypeType = TypeValue("DataclassType", bases=[TypeType])

builtin_value("Object", ObjectType)
builtin_value("Number", NumberType)
builtin_value("String", StringType)
builtin_value("Bool", BoolType)
builtin_value("Null", NullType)
builtin_value("Symbol", SymbolType)
builtin_value("Vector", VectorType)
builtin_value("Tuple", TupleType)
builtin_value("Quote", QuoteType)
builtin_value("Continuation", ContinuationType)
builtin_value("Type", TypeType)
builtin_value("DataclassType", DataclassTypeType)


def handle__method_does_(ctxt: Context, receiver: Value, decl: Value, body: Value) -> Value:
    # TODO: set ctxt to the receiver if the receiver is some sort of reified context value?
    # TODO: support return type declaration? (how can we enforce that...?)
    if not isinstance(decl, QuoteValue):
        raise ValueError("method:does: 'declaration' argument should be a quoted name or message")

    def param_name_and_type(expr, error_msg):
        if isinstance(expr, NameExpr):
            name = expr.name.value
            _type = ObjectType
            return (name, _type)
        elif (
            isinstance(expr, ParenExpr)
            and isinstance(expr.inner, NAryMessageExpr)
            and expr.inner.target is None
            and len(expr.inner.messages) == 1
        ):
            name = expr.inner.messages[0].value
            # TODO: don't use eval_toplevel.
            _type = eval_toplevel(expr.inner.args[0], ctxt)
            return (name, _type)
        else:
            raise ValueError(error_msg)

    if isinstance(decl.body, NameExpr):
        message = decl.body.name.value
        param_names = ["self"]
        param_types = [ObjectType]
    elif isinstance(decl.body, UnaryMessageExpr):
        format_msg = (
            "When the method:does: 'declaration' argument is a unary message, "
            "it must be a simple unary message of the form [target-name message-name] "
            "or else a unary message of the form [(target-name: type) message-name]"
        )
        message = decl.body.message.value
        param_name, param_type = param_name_and_type(decl.body.target, format_msg)
        param_names = [param_name]
        param_types = [param_type]
    elif isinstance(decl.body, NAryMessageExpr):
        format_msg = (
            "When the method:does: 'declaration' argument is an n-ary message, "
            "it must be a simple n-ary message of the form [target-name message: param-name ...] "
            "or else an n-ary message of the form [(target-name: type) message: (param-name: type) ...] "
            "(the target-name is optional, as is each parameter type declaration)"
        )
        message = "".join(message.value + ":" for message in decl.body.messages)
        param_names = []
        param_types = []

        if decl.body.target:
            param_name, param_type = param_name_and_type(decl.body.target, format_msg)
            param_names.append(param_name)
            param_types.append(param_type)
        else:
            param_names.append("self")
            param_types.append(ObjectType)

        for arg in decl.body.args:
            param_name, param_type = param_name_and_type(arg, format_msg)
            param_names.append(param_name)
            param_types.append(param_type)
    else:
        raise ValueError(
            f"method:does: 'declaration' argument should be a quoted name or message; got {decl.body}"
        )

    if decl.parameters:
        raise ValueError("method:does: 'declaration' argument should not specify any parameters")

    # Fill in the body and context, then define the method.
    if not isinstance(body, QuoteValue):
        raise ValueError("method:does: 'body' argument should be a quote")
    if body.parameters:
        raise ValueError("method:does: 'body' argument should not specify any parameters")

    # TODO: compile here instead of on-demand in the 'invoke' bytecode evaluator.
    method = Method(
        context=body.context,
        param_names=param_names,
        param_types=param_types,
        body_expr=body.body,
        body=None,
    )
    if message in ctxt.slots:
        raise ValueError(f"Message '{message}' is already defined.")
    ctxt.slots[message] = method
    return NullValue()


builtin("method:does:", handle__method_does_)


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
    elif isinstance(value, TypeValue):
        return TypeType
    elif isinstance(value, DataclassTypeValue):
        return DataclassTypeType
    elif isinstance(value, DataclassValue):
        return value.type


def handle__type(ctxt: Context, receiver: Value) -> Value:
    return type_of(receiver)


builtin("type", handle__type)


def is_subtype(a: TypeValue, b: TypeValue) -> bool:
    # TODO: check for no recursion... either here or whenever creating new types
    if a == b:
        return True
    for base in a.bases:
        if is_subtype(base, b):
            return True
    return False


def handle_is_type(_type: TypeValue):
    def handler(ctxt: Context, receiver: Value) -> Value:
        return BoolValue(is_subtype(type_of(receiver), _type))

    return handler


builtin("Object?", handle_is_type(ObjectType))
builtin("Number?", handle_is_type(NumberType))
builtin("String?", handle_is_type(StringType))
builtin("Bool?", handle_is_type(BoolType))
builtin("Null?", handle_is_type(NullType))
builtin("Symbol?", handle_is_type(SymbolType))
builtin("Tuple?", handle_is_type(TupleType))
builtin("Vector?", handle_is_type(VectorType))
builtin("Quote?", handle_is_type(QuoteType))
builtin("Continuation?", handle_is_type(ContinuationType))
builtin("Type?", handle_is_type(TypeType))
builtin("DataclassType?", handle_is_type(DataclassTypeType))


def handle__data_has_(ctxt: Context, receiver: Value, decl: Value, slots: Value) -> Value:
    # TODO: set ctxt to the receiver if the receiver is some sort of reified context value?
    if isinstance(decl, QuoteValue):
        if isinstance(decl.body, NameExpr):
            class_name = decl.body.name.value
        else:
            raise ValueError(
                f"data:has: 'declaration' argument should be a quoted name; got {decl}"
            )
    else:
        raise ValueError("data:has: 'declaration' argument should be a quoted name")

    # TODO: change format? maybe also should parse an AST node...
    # also consider adding "parsing methods", which have a property indicating that evaluation
    # of this form should always just pass in AST nodes (+ surrounding lexical context)
    if not isinstance(slots, VectorValue):
        raise ValueError("data:has: 'slots' argument should be a vector of symbols")
    for slot in slots.components:
        if not isinstance(slot, SymbolValue):
            raise ValueError("data:has: 'slots' argument should be a vector of symbols")
    slots = [slot.symbol for slot in slots.components]

    if class_name in ctxt.slots:
        raise ValueError(f"'{class_name}' is already defined")
    # TODO: allow inheritance
    _class = DataclassTypeValue(name=class_name, bases=[ObjectType], slots=slots)
    ctxt.slots[class_name] = _class

    if class_name + "?" in ctxt.slots:
        raise ValueError(f"'{class_name}?' is already defined")
    ctxt.slots[class_name + "?"] = NativeHandler(handle_is_type(_class))

    # TODO: This is all very hacky. Should just use multimethod dispatch instead of hardcoding.
    ctor_message = "".join(slot + ":" for slot in slots) if slots else "new"
    if ctor_message not in ctxt.slots:
        ctxt.slots[ctor_message] = handle_generic_dataclass_constructor(ctor_message)

    for slot in slots:
        get_msg = "." + slot
        if get_msg not in ctxt.slots:
            ctxt.slots[get_msg] = handle_generic_dataclass_get(get_msg, slot)
        set_msg = "set-" + slot + ":"
        if set_msg not in ctxt.slots:
            ctxt.slots[set_msg] = handle_generic_dataclass_set(set_msg, slot)

    return NullValue()


def handle_generic_dataclass_constructor(message: str) -> NativeHandler:
    def handler(ctxt: Context, receiver: Value, *values: list[Value]) -> Value:
        values = list(values)  # was a tuple
        if not isinstance(receiver, DataclassTypeValue):
            raise ValueError(f"{message} expects a dataclass type as receiver")
        assert len(values) == len(receiver.slots)
        return DataclassValue(type=receiver, values=values)

    return NativeHandler(handler)


def handle_generic_dataclass_get(message: str, slot: str) -> NativeHandler:
    def handler(ctxt: Context, receiver: Value) -> Value:
        if not isinstance(receiver, DataclassValue):
            raise ValueError(f"{message} expects a dataclass value as receiver")
        if slot in receiver.type.slots:
            return receiver.values[receiver.type.slots.index(slot)]
        else:
            raise ValueError(f"dataclass '{receiver.type.name}' has no slot '{slot}'")

    return NativeHandler(handler)


def handle_generic_dataclass_set(message: str, slot: str) -> NativeHandler:
    def handler(ctxt: Context, receiver: Value, value: Value) -> Value:
        if not isinstance(receiver, DataclassValue):
            raise ValueError(f"{message} expects a dataclass value as receiver")
        if slot in receiver.type.slots:
            receiver.values[receiver.type.slots.index(slot)] = value
            return value
        else:
            raise ValueError(f"dataclass '{receiver.type.name}' has no slot '{slot}'")

    return NativeHandler(handler)


builtin("data:has:", handle__data_has_)


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


builtin("let:=:", handle__let_eq_)


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
        (NumberValue, NumberValue, (lambda a, b: BoolValue(a.value == b.value))),
        (StringValue, StringValue, (lambda a, b: BoolValue(a.value == b.value))),
        (BoolValue, BoolValue, (lambda a, b: BoolValue(a.value == b.value))),
        (NullValue, NullValue, (lambda a, b: BoolValue(True))),
        (SymbolValue, SymbolValue, (lambda a, b: BoolValue(a.value == b.value))),
        # TODO: deep equality
        (TupleValue, TupleValue, (lambda a, b: BoolValue(a == b))),
        # TODO: deep equality
        (VectorValue, VectorValue, (lambda a, b: BoolValue(a == b))),
        (Value, Value, (lambda a, b: BoolValue(a == b))),
    ],
)
# TODO: fix this operator
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
        (NumberValue, NumberValue, (lambda a, b: NumberValue(a.value // b.value))),
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

builtin_value("t", BoolValue(True))
builtin_value("f", BoolValue(False))
builtin_value("null", NullValue())


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

from parser import NameExpr, NAryMessageExpr, ParenExpr, UnaryMessageExpr
from typing import Callable, Tuple, Union

from interpreter import (
    BoolType,
    BoolValue,
    Context,
    ContinuationType,
    DataclassTypeType,
    DataclassTypeValue,
    DataclassValue,
    IntrinsicMethodBody,
    Method,
    MethodBody,
    MultiMethod,
    NativeHandler,
    NativeMethodBody,
    NullType,
    NullValue,
    NumberType,
    NumberValue,
    ParameterAnyMatcher,
    ParameterMatcher,
    ParameterTypeMatcher,
    QuoteMethodBody,
    QuoteType,
    QuoteValue,
    StringType,
    StringValue,
    SymbolType,
    SymbolValue,
    TupleType,
    TupleValue,
    TypeType,
    TypeValue,
    Value,
    VectorType,
    VectorValue,
    eval_toplevel,
    intrinsic_handlers,
    is_subtype,
    type_of,
)

# def add_method_to_slot(slot: Value, param_matchers: list[ParameterMatcher], body: MethodBody) -> None:
#    if not isinstance(slot, MultiMethod):
#        raise ValueError(f"Could not add method: slot value {slot} is not a multimethod.")
#    multimethod = slot
#
#    method = Method(param_matchers=param_matchers, body=body)
#    multimethod.add_method(method)


def create_or_add_method(ctxt: Context, slot: str, method: Method):
    if slot in ctxt.slots:
        multimethod = ctxt.slots[slot]
        if not isinstance(multimethod, MultiMethod):
            raise ValueError(f"Slot '{slot}' is already defined and is not a multi-method.")
    else:
        multimethod = MultiMethod(name=slot, methods=[])
        ctxt.slots[slot] = multimethod
    multimethod.add_method(method)


global_context = Context(slots={}, base=None)
for slot, (param_matchers, handler) in intrinsic_handlers.items():
    create_or_add_method(
        global_context,
        slot,
        Method(param_matchers=param_matchers, body=IntrinsicMethodBody(handler)),
    )


def builtin_value(name: str, value: Value) -> None:
    assert name not in global_context.slots, f"{name} already is defined as a builtin."
    global_context.slots[name] = value


# Add a method to a multimethod.
# For convenience in specifying parameter matchers:
# * None -> an 'any' matcher
# * TypeValue -> a type matcher
# * otherwise, must be a full ParameterMatcher
def builtin_method(
    name: str, param_matchers: list[Union[ParameterMatcher, TypeValue, None]], handler
) -> None:
    matchers = []
    for matcher in param_matchers:
        if matcher is None:
            matchers.append(ParameterAnyMatcher())
        elif isinstance(matcher, TypeValue):
            matchers.append(ParameterTypeMatcher(matcher))
        elif isinstance(matcher, ParameterMatcher):
            matchers.append(matcher)
        else:
            raise AssertionError(f"Unexpected param matcher '{matcher}'.")

    create_or_add_method(
        global_context, name, Method(matchers, body=NativeMethodBody(NativeHandler(handler)))
    )


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

    def param_name_and_matcher(expr, error_msg):
        # TODO: parse value matchers as well.
        if isinstance(expr, NameExpr):
            name = expr.name.value
            matcher = ParameterAnyMatcher()
            return (name, matcher)
        elif (
            isinstance(expr, ParenExpr)
            and isinstance(expr.inner, NAryMessageExpr)
            and expr.inner.target is None
            and len(expr.inner.messages) == 1
        ):
            name = expr.inner.messages[0].value
            # TODO: don't use eval_toplevel.
            _type = eval_toplevel(expr.inner.args[0], ctxt)
            matcher = ParameterTypeMatcher(_type)
            return (name, matcher)
        else:
            raise ValueError(error_msg)

    if isinstance(decl.body, NameExpr):
        message = decl.body.name.value
        param_names = ["self"]
        param_matchers = [ParameterAnyMatcher()]
    elif isinstance(decl.body, UnaryMessageExpr):
        format_msg = (
            "When the method:does: 'declaration' argument is a unary message, "
            "it must be a simple unary message of the form [target-name message-name] "
            "or else a unary message of the form [(target-name: matcher) message-name]"
        )
        message = decl.body.message.value
        param_name, param_matcher = param_name_and_matcher(decl.body.target, format_msg)
        param_names = [param_name]
        param_matchers = [param_matcher]
    elif isinstance(decl.body, NAryMessageExpr):
        format_msg = (
            "When the method:does: 'declaration' argument is an n-ary message, "
            "it must be a simple n-ary message of the form [target-name message: param-name ...] "
            "or else an n-ary message of the form [(target-name: matcher) message: (param-name: matcher) ...] "
            "(the target-name is optional, as is each parameter matcher declaration)"
        )
        message = "".join(message.value + ":" for message in decl.body.messages)
        param_names = []
        param_matchers = []

        if decl.body.target:
            param_name, param_matcher = param_name_and_matcher(decl.body.target, format_msg)
            param_names.append(param_name)
            param_matchers.append(param_matcher)
        else:
            param_names.append("self")
            param_matchers.append(ParameterAnyMatcher())

        for arg in decl.body.args:
            param_name, param_matcher = param_name_and_matcher(arg, format_msg)
            param_names.append(param_name)
            param_matchers.append(param_matcher)
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
        param_matchers=param_matchers,
        body=QuoteMethodBody(
            context=body.context,
            param_names=param_names,
            body_expr=body.body,
            body=None,
        ),
    )

    # Add it to a multimethod, or create a new multimethod if the slot isn't yet defined.
    create_or_add_method(ctxt, message, method)

    return NullValue()


builtin_method("method:does:", (None, QuoteType, QuoteType), handle__method_does_)


def handle__type(ctxt: Context, receiver: Value) -> Value:
    return type_of(receiver)


builtin_method("type", (None,), handle__type)


def handle_is_type(_type: TypeValue):
    def handler(ctxt: Context, receiver: Value) -> Value:
        return BoolValue(is_subtype(type_of(receiver), _type))

    return handler


builtin_method("Number?", (None,), handle_is_type(NumberType))
builtin_method("String?", (None,), handle_is_type(StringType))
builtin_method("Bool?", (None,), handle_is_type(BoolType))
builtin_method("Null?", (None,), handle_is_type(NullType))
builtin_method("Symbol?", (None,), handle_is_type(SymbolType))
builtin_method("Tuple?", (None,), handle_is_type(TupleType))
builtin_method("Vector?", (None,), handle_is_type(VectorType))
builtin_method("Quote?", (None,), handle_is_type(QuoteType))
builtin_method("Continuation?", (None,), handle_is_type(ContinuationType))
builtin_method("Type?", (None,), handle_is_type(TypeType))
builtin_method("DataclassType?", (None,), handle_is_type(DataclassTypeType))


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
    # make sure to disallow diamond inheritance
    # and also calculate the C3 linearization while we're here
    _class = DataclassTypeValue(name=class_name, bases=[], slots=slots)
    ctxt.slots[class_name] = _class

    create_or_add_method(
        ctxt,
        class_name + "?",
        Method(
            param_matchers=[ParameterAnyMatcher()],
            body=NativeMethodBody(NativeHandler(handle_is_type(_class))),
        ),
    )

    ctor_message = "".join(slot + ":" for slot in slots) if slots else "new"
    create_or_add_method(
        ctxt,
        ctor_message,
        Method(
            # TODO: allow specifying types for slots
            param_matchers=[ParameterAnyMatcher()] * (1 + len(slots)),
            body=NativeMethodBody(handle_generic_dataclass_constructor(ctor_message)),
        ),
    )

    for slot in slots:
        get_msg = "." + slot
        create_or_add_method(
            ctxt,
            get_msg,
            Method(
                param_matchers=[ParameterTypeMatcher(_class)],
                body=NativeMethodBody(handle_generic_dataclass_get(get_msg, slot)),
            ),
        )

        set_msg = "set-" + slot + ":"
        # TODO: allow specifying types for slots
        create_or_add_method(
            ctxt,
            set_msg,
            Method(
                param_matchers=[ParameterTypeMatcher(_class), ParameterAnyMatcher()],
                body=NativeMethodBody(handle_generic_dataclass_set(set_msg, slot)),
            ),
        )

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


builtin_method("data:has:", (None, QuoteType, VectorType), handle__data_has_)


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


builtin_method("let:=:", (None, QuoteType, None), handle__let_eq_)


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


builtin_method("=:_:", (None, QuoteType, None), handle__set)


# Each handler is a function from value -> value, where the input value satisfies the provided receiver matcher.
def builtin_unary_op(
    op, methods: list[Tuple[Union[ParameterTypeMatcher, TypeValue, None], Callable[[Value], Value]]]
):
    for receiver_matcher, handler in methods:
        builtin_method(
            op,
            param_matchers=[receiver_matcher],
            handler=(lambda ctxt, receiver: handler(receiver)),
        )


# Each handler is a function from (value, value) -> value, where the input components satisfies the provided left_matcher / right_matcher.
def builtin_binary_op(
    op: str,
    methods: list[
        Tuple[
            Union[ParameterTypeMatcher, TypeValue, None],
            Union[ParameterTypeMatcher, TypeValue, None],
            Callable[[Value, Value], Value],
        ]
    ],
):
    for left_matcher, right_matcher, handler in methods:
        builtin_method(
            op + ":_:",
            param_matchers=[None, left_matcher, right_matcher],
            handler=(lambda ctxt, receiver, left, right: handler(left, right)),
        )


builtin_binary_op(
    "~",
    [
        (StringType, StringType, (lambda a, b: StringValue(a.value + b.value))),
    ],
)

builtin_binary_op(
    "and",
    [
        (BoolType, BoolType, (lambda a, b: BoolValue(a.value and b.value))),
    ],
)
builtin_binary_op(
    "or",
    [
        (BoolType, BoolType, (lambda a, b: BoolValue(a.value or b.value))),
    ],
)

builtin_binary_op(
    "==",
    [
        (NumberType, NumberType, (lambda a, b: BoolValue(a.value == b.value))),
        (StringType, StringType, (lambda a, b: BoolValue(a.value == b.value))),
        (BoolType, BoolType, (lambda a, b: BoolValue(a.value == b.value))),
        (NullType, NullType, (lambda a, b: BoolValue(True))),
        (SymbolType, SymbolType, (lambda a, b: BoolValue(a.value == b.value))),
        # TODO: deep equality
        (TupleType, TupleType, (lambda a, b: BoolValue(a == b))),
        # TODO: deep equality
        (VectorType, VectorType, (lambda a, b: BoolValue(a == b))),
        (None, None, (lambda a, b: BoolValue(a == b))),
    ],
)
# TODO: fix this operator
builtin_binary_op(
    "!=",
    [
        (None, None, (lambda a, b: BoolValue(a.value == b.value))),
    ],
)

builtin_binary_op(
    "<",
    [
        (NumberType, NumberType, (lambda a, b: BoolValue(a.value < b.value))),
    ],
)
builtin_binary_op(
    "<=",
    [
        (NumberType, NumberType, (lambda a, b: BoolValue(a.value <= b.value))),
    ],
)
builtin_binary_op(
    ">",
    [
        (NumberType, NumberType, (lambda a, b: BoolValue(a.value > b.value))),
    ],
)
builtin_binary_op(
    ">=",
    [
        (NumberType, NumberType, (lambda a, b: BoolValue(a.value >= b.value))),
    ],
)

builtin_binary_op(
    "+",
    [
        (NumberType, NumberType, (lambda a, b: NumberValue(a.value + b.value))),
    ],
)
builtin_binary_op(
    "-",
    [
        (NumberType, NumberType, (lambda a, b: NumberValue(a.value - b.value))),
    ],
)
builtin_binary_op(
    "*",
    [
        (NumberType, NumberType, (lambda a, b: NumberValue(a.value * b.value))),
    ],
)
builtin_binary_op(
    "/",
    [
        (NumberType, NumberType, (lambda a, b: NumberValue(a.value // b.value))),
    ],
)

builtin_unary_op(
    "not",
    [
        (BoolType, (lambda v: BoolValue(not v.value))),
    ],
)
builtin_unary_op(
    "+",
    [
        (NumberType, (lambda v: NumberValue(+v.value))),
    ],
)
builtin_unary_op(
    "-",
    [
        (NumberType, (lambda v: NumberValue(-v.value))),
    ],
)


def handle__print(ctxt: Context, receiver: Value) -> Value:
    print(receiver)
    return NullValue()


builtin_method("print", (None,), handle__print)


def handle__print_(ctxt: Context, receiver: Value, value: Value) -> Value:
    print(value)
    return NullValue()


builtin_method("print:", (None, None), handle__print_)

builtin_value("t", BoolValue(True))
builtin_value("f", BoolValue(False))
builtin_value("null", NullValue())


def handle__to_string(ctxt: Context, receiver: Value) -> Value:
    return StringValue(str(receiver))


builtin_method(">string", (None,), handle__to_string)


def handle__at_(ctxt: Context, receiver: Value, index: Value) -> Value:
    if isinstance(receiver, VectorValue):
        if isinstance(index, NumberValue):
            return receiver.components[index.value]
        else:
            raise ValueError(f"at: index must be a number; got {index}")
    else:
        raise ValueError(f"at: requires a vector; got {receiver}")


builtin_method("at:", (VectorType, NumberType), handle__at_)


def handle__at_eq_(ctxt: Context, receiver: Value, index: Value, value: Value) -> Value:
    if isinstance(receiver, VectorValue):
        if isinstance(index, NumberValue):
            receiver.components[index.value] = value
            return value
        else:
            raise ValueError(f"at: index must be a number; got {index}")
    else:
        raise ValueError(f"at: requires a vector; got {receiver}")


builtin_method("at:=:", (VectorType, NumberType, None), handle__at_eq_)


def handle__append_(ctxt: Context, receiver: Value, value: Value) -> Value:
    if isinstance(receiver, VectorValue):
        receiver.components.append(value)
        return receiver
    else:
        raise ValueError(f"append: requires a vector; got {receiver}")


builtin_method("append:", (VectorType, None), handle__append_)


def handle__length(ctxt: Context, receiver: Value) -> Value:
    if isinstance(receiver, VectorValue):
        return NumberValue(len(receiver.components))
    elif isinstance(receiver, StringValue):
        return NumberValue(len(receiver.value))
    else:
        raise ValueError(f"length requires a vector or string; got {receiver}")


builtin_method("length", (VectorType,), handle__length)
builtin_method("length", (StringType,), handle__length)


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


builtin_method("<show-current-context>", (None,), handle__show_current_context)

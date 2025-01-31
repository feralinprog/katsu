from parser import NameExpr, NAryMessageExpr, ParenExpr, UnaryMessageExpr
from typing import Callable, Optional, Tuple, Union

from termcolor import colored

from interpreter import (
    BoolType,
    BoolValue,
    CompilationContext,
    CompiledBody,
    Compiler,
    CompileTimeHandler,
    Context,
    ContinuationType,
    DataclassTypeType,
    DataclassTypeValue,
    DataclassValue,
    Expr,
    IntrinsicMethodBody,
    Method,
    MixinTypeValue,
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
    ParameterValueMatcher,
    QuoteExpr,
    QuoteMethodBody,
    QuoteType,
    QuoteValue,
    ReturnContinuationType,
    StringType,
    StringValue,
    SymbolType,
    SymbolValue,
    TupleType,
    TypeType,
    TypeValue,
    UndeterminedMethod,
    UndeterminedValue,
    Value,
    VectorType,
    VectorValue,
    eval_toplevel,
    intrinsic_handlers,
    is_subtype,
    type_of,
)
from span import SourceSpan

# TODO: go through all handlers, delete unnecessary type checks (since multimethod dispatch guarantees correct types)


def create_or_add_method(ctxt: Context, slot: str, method: Method):
    if slot in ctxt.slots:
        multimethod = ctxt.slots[slot]
        if not isinstance(multimethod, MultiMethod):
            raise ValueError(f"Slot '{slot}' is already defined and is not a multi-method.")
    else:
        multimethod = MultiMethod(name=slot, methods=[], compilations_to_invalidate=[])
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


def builtin_compile_time(name: str, handler) -> None:
    assert name not in global_context.slots, f"{name} already is defined as a builtin."
    global_context.slots[name] = CompileTimeHandler(handler)


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
builtin_value("ReturnContinuation", ReturnContinuationType)
builtin_value("Type", TypeType)
builtin_value("DataclassType", DataclassTypeType)


def handle__method_does_(
    compiler: Compiler, span: SourceSpan, receiver: Optional[Expr], decl: Expr, body: Expr
) -> None:
    if receiver:
        raise ValueError("method:does: does not need a receiver")

    # TODO: support return type declaration? (how can we enforce that...?)

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
            # TODO: this is so wrong, don't directly use the CompilationContext
            _type = eval_toplevel(expr.inner.args[0], compiler.ctxt)
            matcher = ParameterTypeMatcher(_type)
            return (name, matcher)
        else:
            raise ValueError(error_msg)

    # TODO: maybe we need some preprocessing step to just get rid of ParenExpr...
    while isinstance(decl, ParenExpr):
        decl = decl.inner

    if isinstance(decl, QuoteExpr):
        if decl.parameters:
            raise ValueError(
                "method:does: 'declaration' argument should not specify any parameters"
            )
        decl = decl.body
    else:
        # TODO: implement non-quote decls for method:does:.
        raise NotImplementedError("non-quote decls for method:does:")

    if isinstance(decl, NameExpr):
        message = decl.name.value
        param_names = ["self"]
        param_matchers = [ParameterAnyMatcher()]
    elif isinstance(decl, UnaryMessageExpr):
        format_msg = (
            "When the method:does: 'declaration' argument is a unary message, "
            "it must be a simple unary message of the form [target-name message-name] "
            "or else a unary message of the form [(target-name: matcher) message-name]"
        )
        message = decl.message.value
        param_name, param_matcher = param_name_and_matcher(decl.target, format_msg)
        param_names = [param_name]
        param_matchers = [param_matcher]
    elif isinstance(decl, NAryMessageExpr):
        format_msg = (
            "When the method:does: 'declaration' argument is an n-ary message, "
            "it must be a simple n-ary message of the form [target-name message: param-name ...] "
            "or else an n-ary message of the form [(target-name: matcher) message: (param-name: matcher) ...] "
            "(the target-name is optional, as is each parameter matcher declaration)"
        )
        message = "".join(message.value + ":" for message in decl.messages)
        param_names = []
        param_matchers = []

        if decl.target:
            param_name, param_matcher = param_name_and_matcher(decl.target, format_msg)
            param_names.append(param_name)
            param_matchers.append(param_matcher)
        else:
            param_names.append("self")
            param_matchers.append(ParameterAnyMatcher())

        for arg in decl.args:
            param_name, param_matcher = param_name_and_matcher(arg, format_msg)
            param_names.append(param_name)
            param_matchers.append(param_matcher)
    else:
        # TODO: allow unary / binary ops too
        raise ValueError(
            f"method:does: 'declaration' argument should be a name or message; got {decl}"
        )

    # Fill in the body and context, then define the method.
    if not isinstance(body, QuoteExpr):
        raise ValueError("method:does: 'body' argument should be a quote")
    if body.parameters:
        raise ValueError("method:does: 'body' argument should not specify any parameters")

    body_comp_ctxt = CompilationContext(slots={}, base=compiler.ctxt)
    # TODO: this is a bit hacky, only works for direct recursion.
    body_comp_ctxt.slots[message] = UndeterminedMethod(message)
    for name in param_names:
        body_comp_ctxt.slots[name] = UndeterminedValue(name)

    method = Method(
        param_matchers=param_matchers,
        body=QuoteMethodBody(
            context=compiler.ctxt,  # TODO: this seems a bit funky, not a real context...
            param_names=param_names,
            compiled_body=CompiledBody(body.body, bytecode=None, comp_ctxt=body_comp_ctxt),
        ),
    )

    # Add it to a multimethod, or create a new multimethod if the slot isn't yet defined.
    assert isinstance(compiler.ctxt.base, Context)
    create_or_add_method(compiler.ctxt.base, message, method)


builtin_compile_time("method:does:", handle__method_does_)


def handle__defer_method_(ctxt: Context, receiver: Value, method: SymbolValue) -> Value:
    slot = method.symbol
    if slot in ctxt.slots:
        raise ValueError(f"Slot '{slot}' is already defined.")
    ctxt.slots[slot] = MultiMethod(name=slot, methods=[], compilations_to_invalidate=[])
    return NullValue()


builtin_method("defer-method:", (None, SymbolType), handle__defer_method_)


def handle__type(ctxt: Context, receiver: Value) -> Value:
    return type_of(receiver)


builtin_method("type", (None,), handle__type)


def handle__is_instance_(ctxt: Context, receiver: Value, type: TypeValue) -> Value:
    return BoolValue(is_subtype(type_of(receiver), type))


builtin_method("is-instance:", (None, TypeType), handle__is_instance_)


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
builtin_method("ReturnContinuation?", (None,), handle_is_type(ReturnContinuationType))
builtin_method("Type?", (None,), handle_is_type(TypeType))
builtin_method("DataclassType?", (None,), handle_is_type(DataclassTypeType))


def define_dataclass(
    message: str, ctxt: Context, decl: SymbolValue, extends: VectorValue, slots: VectorValue
) -> None:
    class_name = decl.symbol

    for base in extends.components:
        if not isinstance(base, TypeValue):
            raise ValueError(f"{message} 'extends' argument should be a vector of types")
    bases: list[TypeValue] = list(extends.components)
    for base in bases:
        if base.sealed:
            raise ValueError(f"Cannot extend from sealed class {base}")

    # TODO: This feels a bit hacky. Better way? Maybe separate args.
    base_dataclass = None
    for base in bases:
        if isinstance(base, DataclassTypeValue):
            if base_dataclass:
                raise ValueError(
                    f"Cannot extend from multiple dataclasses: {base_dataclass}, {base}"
                )
            else:
                base_dataclass = base

    # TODO: change format? maybe also should parse an AST node...
    # also consider adding "parsing methods", which have a property indicating that evaluation
    # of this form should always just pass in AST nodes (+ surrounding lexical context)
    for slot in slots.components:
        if not isinstance(slot, SymbolValue):
            raise ValueError(f"{message} 'slots' argument should be a vector of symbols")
    slots = (base_dataclass.slots if base_dataclass else []) + [
        slot.symbol for slot in slots.components
    ]

    if class_name in ctxt.slots:
        raise ValueError(f"'{class_name}' is already defined")
    _class = DataclassTypeValue(name=class_name, bases=bases, slots=slots, sealed=False)
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
            param_matchers=[ParameterValueMatcher(_class)] + [ParameterAnyMatcher()] * len(slots),
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


def handle__data_extends_has_(
    ctxt: Context, receiver: Value, decl: SymbolValue, extends: VectorValue, slots: VectorValue
) -> Value:
    # TODO: set ctxt to the receiver if the receiver is some sort of reified context value?
    define_dataclass("data:extends:has:", ctxt, decl, extends, slots)
    return NullValue()


def handle__data_has_(
    ctxt: Context, receiver: Value, decl: SymbolValue, slots: VectorValue
) -> Value:
    # TODO: set ctxt to the receiver if the receiver is some sort of reified context value?
    extends = VectorValue(components=[])
    define_dataclass("data:has:", ctxt, decl, extends, slots)
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


# TODO: should probably make these compile-time, since they add slot definitions
builtin_method(
    "data:extends:has:", (None, SymbolType, VectorType, VectorType), handle__data_extends_has_
)
builtin_method("data:has:", (None, SymbolType, VectorType), handle__data_has_)


def handle__mixin_(ctxt: Context, receiver: Value, name: SymbolValue) -> Value:
    mixin_name = name.symbol
    if mixin_name in ctxt.slots:
        raise ValueError(f"mixin: slot {mixin_name} already exists")
    # TODO: allow inheritance?
    # TODO: allow specifying required methods? (i.e. checker for when we are later mixing-in)
    ctxt.slots[mixin_name] = MixinTypeValue(name=mixin_name, bases=[], sealed=False)
    return NullValue()


# TODO: should probably make this compile-time, since they add slot definitions
builtin_method("mixin:", (None, SymbolType), handle__mixin_)


def handle__mix_in_to_(ctxt: Context, receiver: Value, mixin: TypeValue, type: TypeValue) -> Value:
    if not isinstance(mixin, MixinTypeValue):
        raise ValueError(f"mix-in:to: type {mixin} is not a mixin")
    type.try_set_bases(type.bases + [mixin])
    return NullValue()


builtin_method("mix-in:to:", (None, TypeType, TypeType), handle__mix_in_to_)


def handle__let_eq_(
    compiler: Compiler, span: SourceSpan, receiver: Optional[Expr], decl: Expr, value: Expr
) -> None:
    if receiver:
        raise ValueError("let:=: does not need a receiver")

    if isinstance(decl, NameExpr):
        local_name = decl.name.value
    else:
        raise ValueError(f"let:=: 'declaration' argument should be a name; got {decl}")

    if local_name in compiler.ctxt.slots:
        raise ValueError(f"Slot '{local_name}' is already defined.")
    # Compile the value-expression before adding to the context, so that the value-expression cannot
    # use the local.
    compiler.compile_expr(value)
    compiler.ctxt.slots[local_name] = UndeterminedValue(local_name)
    compiler.add_bytecode("create-slot", (local_name,), span=span)


builtin_compile_time("let:=:", handle__let_eq_)


def handle__set(ctxt: Context, slot: Value, value: Value) -> Value:
    # TODO: set ctxt to the receiver if the receiver is some sort of reified context value?
    if isinstance(slot, QuoteValue):
        if isinstance(slot.compiled_body.body, NameExpr):
            slot = slot.compiled_body.body.name.value
        else:
            raise ValueError(f"=: 'slot' argument should be a quoted name; got {slot}")
    else:
        raise ValueError("=: receiver should be a quoted name")

    while ctxt and slot not in ctxt.slots:
        ctxt = ctxt.base
    if not ctxt:
        raise ValueError(f"'{slot}' is not yet defined.")
    ctxt.slots[slot] = value
    return value


# TODO: maybe should be compile time so LHS isn't a quote.
builtin_method("=:", (QuoteType, None), handle__set)


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
            op + ":",
            param_matchers=[left_matcher, right_matcher],
            handler=(lambda ctxt, left, right: handler(left, right)),
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


def handle__pr(ctxt: Context, receiver: Value) -> Value:
    print(receiver)
    return receiver


builtin_method("pr", (None,), handle__pr)


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


def handle__pop_(ctxt: Context, receiver: Value) -> Value:
    if isinstance(receiver, VectorValue):
        return receiver.components.pop()
    else:
        raise ValueError(f"pop: requires a vector; got {receiver}")


builtin_method("pop", (VectorType,), handle__pop_)


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


def handle__query_user_for_restart_(
    ctxt: Context, receiver: Value, restarts: VectorValue, condition: Value
) -> Value:
    print(colored("A condition occurred and was not handled!", "red"))
    print(f"The condition is {colored(str(condition), 'red')}")
    print(f"Restarts available:")
    print(f"  [#{colored('0', 'blue')}] {colored('panic / abort', 'green')}")
    for i, restart in enumerate(restarts.components):
        print(f"  [#{colored(str(i + 1), 'blue')}] {colored(str(restart), 'green')}")
    while True:
        try:
            in_str = input("Select a restart (or empty for default restart): ")
        except EOFError:
            return NullValue()
        if in_str == "":
            return NullValue()
        try:
            index = int(in_str)
        except ValueError:
            print("not an integer")
            continue
        if index < 0 or index > len(restarts.components):
            print(f"out of range: valid restarts are 0 - {len(restarts.components)}")
            continue
        if index == 0:
            return NullValue()
        return NumberValue(index - 1)


builtin_method(
    "query-user-for-restart:condition:", (None, VectorType, None), handle__query_user_for_restart_
)

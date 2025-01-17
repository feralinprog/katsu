from dataclasses import dataclass, field
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

from termcolor import colored

from error import EvaluationError, RunError, SignalError
from lexer import TokenType
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
        return ":" + self.symbol


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
    compiled_body: "CompiledBody"
    context: "Context"
    # Source span, to allow for useful error messages.
    span: Optional[SourceSpan]

    def __str__(self):
        # TODO: pprint, make less verbose
        return (
            ("\\" + " ".join(self.parameters) + " " if self.parameters else "")
            + "[ "
            + str(self.compiled_body.body)
            + " ]"
        )


@dataclass
class ContinuationValue(Value):
    # Must be a deep copy (up to Values / Contexts) of a runtime state.
    state: "RuntimeState"

    def __str__(self):
        return "<continuation>"


@dataclass
class ReturnContinuationValue(Value):
    # Uses reference equality to determine if the return_to frame is still
    # in the current dynamic scope (i.e. on the call stack).
    # TODO: make this work with continuations (since those are implemented
    # as wholesale copying call frames...)
    return_to: "CallFrame"

    def __str__(self):
        return "<return-continuation>"


@dataclass
class TypeValue(Value):
    name: str
    bases: list["TypeValue"]
    # Can user defined types inherit from this?
    sealed: bool
    # C3 linearization.
    linearization: list["TypeValue"] = field(init=False, compare=False)
    subtypes: list["TypeValue"] = field(init=False, compare=False)

    def __post_init__(self):
        self.linearization = c3_linearization(self)
        self.subtypes = []
        # (Skip self, which is always first in the linearization.)
        for base in self.linearization[1:]:
            if self not in base.subtypes:
                base.subtypes.append(self)

    def __str__(self):
        return f"<class {self.name}>"

    # Switches to new bases and attempts to calculate new linearization.
    # If linearization fails, restores previous state.
    def try_set_bases(self, new_bases: list["TypeValue"]) -> None:
        old_bases = self.bases
        try:
            self.bases = new_bases
            self.linearization = c3_linearization(self)
        except:
            self.bases = old_bases
            raise
        # Linearization of subtypes should not be able to fail now.
        for subtype in self.subtypes:
            subtype.linearization = c3_linearization(subtype)


def c3_linearization(type: TypeValue) -> list[TypeValue]:
    # Calculate linearization, or None if not possible.
    def c3_merge(linearizations: list[list[TypeValue]]) -> Optional[list[TypeValue]]:
        # Should start with all nonempty linearizations.
        assert all(linearizations), linearizations
        merged = []
        while any(linearizations):
            head = None
            for lin in linearizations:
                candidate = lin[0]
                if all(candidate not in lin[1:] for lin in linearizations):
                    head = candidate
                    break
            if head:
                merged.append(head)
                for lin in linearizations:
                    if lin and lin[0] == head:
                        lin.remove(head)
                linearizations = [lin for lin in linearizations if lin]
            else:
                return None
        return merged

    for base in type.bases:
        if type in base.linearization:
            raise ValueError(f"Inheritance cycle starting from {type}")

    if type.bases:
        base_linearization = c3_merge(
            [list(base.linearization) for base in type.bases] + [list(type.bases)]
        )
        if base_linearization is None:
            raise ValueError(f"Could not determine linearization of {type}")
    else:
        base_linearization = []

    return [type] + base_linearization


@dataclass
class MixinTypeValue(TypeValue):
    def __str__(self):
        return f"<mixin {self.name}>"


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


NumberType = TypeValue("Number", bases=[], sealed=True)
StringType = TypeValue("String", bases=[], sealed=True)
BoolType = TypeValue("Bool", bases=[], sealed=True)
NullType = TypeValue("Null", bases=[], sealed=True)
SymbolType = TypeValue("Symbol", bases=[], sealed=True)
VectorType = TypeValue("Vector", bases=[], sealed=True)
TupleType = TypeValue("Tuple", bases=[], sealed=True)
QuoteType = TypeValue("Quote", bases=[], sealed=True)
ContinuationType = TypeValue("Continuation", bases=[], sealed=True)
ReturnContinuationType = TypeValue("ReturnContinuation", bases=[], sealed=True)
TypeType = TypeValue("Type", bases=[], sealed=True)
# TODO: delete?
DataclassTypeType = TypeValue("DataclassType", bases=[TypeType], sealed=True)


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
    elif isinstance(value, ReturnContinuationValue):
        return ReturnContinuationType
    elif isinstance(value, TypeValue):
        return TypeType
    elif isinstance(value, DataclassTypeValue):
        return DataclassTypeType
    elif isinstance(value, DataclassValue):
        return value.type


def is_subtype(a: TypeValue, b: TypeValue) -> bool:
    assert a.linearization, a
    assert b.linearization, b
    # Neat, eh?
    return (
        len(a.linearization) >= len(b.linearization)
        and a.linearization[len(a.linearization) - len(b.linearization)] == b.linearization[0]
    )


#################################################
# Runtime State Model
#################################################


@dataclass
class IntrinsicHandler:
    # Doesn't have to immediately produce a result; the handler is provided
    # the current runtime state, whether this should be a tail call or not,
    # a receiver, and arguments, and may update the runtime state arbitrarily.
    # This handler must also update the state's top of stack frame position as
    # necessary; the interpreter gives up this responsibility. On entry, the
    # data stack has the receiver and arguments already removed. If the handler
    # raises an exception, it must ensure that the data stack is left in a state
    # where pushing one more value to the stack allows that value to be treated
    # as the result of the handler invocation. (For instance, not modifying the
    # data stack at all meets this criterion.)
    handler: Callable[["RuntimeState", bool, Value, list[Value]], None]


@dataclass
class NativeHandler:
    # A callable of the form
    #   (ctxt: Context, receiver: Value, *args: list[Value]) -> Value
    # Should avoid using any `eval` functions; those should be implemented using
    # an IntrinsicHandler instead, so as to reify the call stack.
    handler: Callable[["Context", Value, list[Value]], Value]


@dataclass
class CompileTimeHandler:
    # Not called during evaluation, but rather while compiling a block.
    # This is provided the current block compiler (which includes the compilation
    # context), the source span of the originating message expression, a receiver
    # expression (or None if there was no receiver specified), and a list of
    # argument expressions.
    handler: Callable[["Compiler", SourceSpan, Optional[Expr], list[Expr]], None]


@dataclass
class Context:
    # TODO: add back IntrinsicHandler / NativeHandler? Maybe multimethods should just
    # be implemented in-language instead of being so intrinsic...
    slots: dict[str, Union[Value, "MultiMethod"]]
    base: Optional["Context"]


@dataclass
class BytecodeOp:
    # TODO: switch to enum
    op: str
    # TODO: make subclasses per op for better type safety
    args: Tuple
    # For stack traces / debugging
    span: SourceSpan


# List of bytecode operations:
# get-slot <str>
# create-slot <str>
# push-default-receiver
# push-value <Value>
# push-closure <QuoteValue>
# invoke <message: str> <nargs: int>        (nargs includes the receiver)
# tail-invoke <message: str> <nargs: int>   (nargs includes the receiver)
# components>vector <length: int>
# components>tuple <length: int>
# drop


@dataclass
class BytecodeSequence:
    code: list[BytecodeOp]


@dataclass
class CallFrame:
    # For debug / logging.
    name: str
    sequence: BytecodeSequence
    # Next index to execute (or == len(sequence.code) if the next operation
    # is to return.
    spot: int
    data_stack: list[Union[Value, Expr, Context]]
    context: Context
    # Receiver to use if none is explicitly provided in an invocation.
    default_receiver: Value
    # Optional value to call when unwinding this call frame.
    cleanup: Optional[Value]
    # Is this frame the invocation of a cleanup action?
    is_cleanup: bool
    # If is_cleanup, this holds the value being returned to the first non-is_cleanup
    # frame in the call stack.
    cleanup_retain: Optional[Value]
    # If true, when we next attempt to execute any bytecode in this frame,
    # the frame will be immediately unwound instead.
    force_unwind: bool
    # Number of return continuations pointing to this call frame.
    num_nonlocal_returns: int

    def copy(self) -> "CallFrame":
        return CallFrame(
            name=self.name,
            sequence=self.sequence,
            spot=self.spot,
            data_stack=list(self.data_stack),
            context=self.context,
            default_receiver=self.default_receiver,
            cleanup=self.cleanup,
            is_cleanup=self.is_cleanup,
            cleanup_retain=self.cleanup_retain,
            force_unwind=self.force_unwind,
            # TODO: ... should this just be 0? Need to figure out better way to handle
            # return-continuation frame matching.
            num_nonlocal_returns=self.num_nonlocal_returns,
        )


@dataclass
class RuntimeState:
    call_stack: list[CallFrame]
    panic_value: Optional[Value]


@dataclass
class ParameterMatcher:
    def matches(self, arg: Value) -> bool:
        raise NotImplementedError("Subclasses must implement.")


@dataclass
class ParameterAnyMatcher(ParameterMatcher):
    def matches(self, arg: Value) -> bool:
        # Matches any value!
        return True

    def __str__(self):
        return "<match:any>"


@dataclass
class ParameterTypeMatcher(ParameterMatcher):
    param_type: TypeValue

    def matches(self, arg: Value) -> bool:
        return is_subtype(type_of(arg), self.param_type)

    def __str__(self):
        return f"<match:type<={self.param_type}>"


@dataclass
class ParameterValueMatcher(ParameterMatcher):
    param_value: Value

    def matches(self, arg: Value) -> bool:
        # TODO: structural (==) equality instead?
        return arg is self.param_value

    def __str__(self):
        return f"<match:value={self.param_value}>"


@dataclass
class MethodBody:
    pass


@dataclass
class QuoteMethodBody(MethodBody):
    context: Context
    # This includes the receiver name.
    param_names: list[str]
    compiled_body: "CompiledBody"

    def __str__(self):
        # TODO: pprint, make less verbose
        return (
            ("\\" + " ".join(self.param_names) + " " if self.param_names else "")
            + "[ "
            + str(self.compiled_body.body)
            + " ]"
        )


@dataclass
class IntrinsicMethodBody(MethodBody):
    handler: IntrinsicHandler
    # Optional function to call to inline-compile an invocation of this instrinsic method.
    # It takes the compiler being used to compile the expression which invoked the intrinsic,
    # the receiver and other arguments (only the receiver is optional), whether or not this
    # should be a tail call, and the source span for the invocation.
    compile_inline: Optional[Callable[["Compiler", list[Optional[Expr]], bool, SourceSpan], None]]


@dataclass
class NativeMethodBody(MethodBody):
    handler: NativeHandler


@dataclass
class Method:
    # This also includes a matcher for the receiver.
    param_matchers: list[ParameterMatcher]
    body: MethodBody
    inline: bool


# a <= b if a is at least as specific as b.
# (Think of the sets of matched values.)
def matcher_lte(a: ParameterMatcher, b: ParameterMatcher):
    # A value matcher is more specific than a type matcher, which is more specific than an 'any' matcher.
    # Value matchers are unordered.
    # Type matchers are partially ordered by their parameter types.
    # 'Any' matchers are all equal.
    if isinstance(b, ParameterAnyMatcher):
        return True
    elif isinstance(b, ParameterTypeMatcher):
        if isinstance(a, ParameterAnyMatcher):
            return False
        elif isinstance(a, ParameterTypeMatcher):
            return is_subtype(a.param_type, b.param_type)
        elif isinstance(a, ParameterValueMatcher):
            return True
        else:
            raise AssertionError(f"Forgot matcher type: {a}")
    elif isinstance(b, ParameterValueMatcher):
        if isinstance(a, ParameterAnyMatcher):
            return False
        elif isinstance(a, ParameterTypeMatcher):
            return False
        elif isinstance(a, ParameterValueMatcher):
            # TODO: structural (==) equality instead?
            return a.param_value is b.param_value
        else:
            raise AssertionError(f"Forgot matcher type: {a}")
    else:
        raise AssertionError(f"Forgot matcher type: {b}")


# a <= b if a is at least as specific as b.
# (Think of the sets of matched values-tuples.)
def method_lte(a: Method, b: Method):
    assert len(a.param_matchers) == len(b.param_matchers)
    return all(matcher_lte(ma, mb) for ma, mb in zip(a.param_matchers, b.param_matchers))


@dataclass
class MultiMethod:
    # Just for debug / error messages.
    name: str
    methods: list[Method]
    inline_dispatch: bool
    # Compiled bodies to invalidate when adding a method.
    # TODO: make this more fine-grained by param types?
    compilations_to_invalidate: list["CompiledBody"]

    def add_method(self, method: Method) -> None:
        assert isinstance(method, Method)
        assert isinstance(method.param_matchers, list) or isinstance(method.param_matchers, tuple)
        for matcher in method.param_matchers:
            assert isinstance(matcher, ParameterMatcher)

        # Make sure the method signature doesn't duplicate a previously added method.
        for existing in self.methods:
            if method.param_matchers == existing.param_matchers:
                # TODO: improve error message
                raise ValueError(f"Method signature already exists for '{self.name}'.")
        # TODO: come back to this if deciding to allow ambiguous multimethod resolution;
        # this would require more constraints on the sort ordering.
        # Sort so that if method A < method B, then A is before B in the list.
        # (A <= B if for each parameter index, the parameter matcher in A is at least as
        # specific as the parameter matcher in B, i.e. matcher-from-A <= matcher-from-B.
        # Then we define A < B if A <= B and A != B.)
        found_spot = False
        for i in range(len(self.methods)):
            if method_lte(method, self.methods[i]):
                found_spot = True
                break
        if found_spot:
            self.methods.insert(i, method)
        else:
            self.methods.append(method)

        # Method inlining requires inlining the multimethod dispatch as well.
        if method.inline:
            self.inline_dispatch = True

        for body in self.compilations_to_invalidate:
            body.invalidate()

    def select_method(self, args: list[Value]) -> Method:
        options = []
        for method in self.methods:
            assert len(args) == len(
                method.param_matchers
            ), f"Multimethod {self.name} has method {method} not matching argument count for {args}."
            if all(matcher.matches(arg) for arg, matcher in zip(args, method.param_matchers)):
                options.append(method)
        if len(options) == 1:
            return options[0]
        elif len(options) == 0:
            raise SignalError(
                condition_name="no-matching-method",
                message=f"No matching method found for multi-method {self.name} "
                f"with values: {', '.join(str(arg) for arg in args)}.",
            )
        else:
            # Determine if there is a single most-specific option. Otherwise the resolution
            # is ambiguous.
            # Note that self.methods is pre-sorted to allow these comparisons.
            candidate = min(options, key=self.methods.index)
            if all(method_lte(candidate, option) for option in options):
                return candidate
            raise SignalError(
                condition_name="ambiguous-method-resolution",
                message=f"There were multiple matching methods found for multi-method {self.name} "
                f"with values: {', '.join(str(arg) for arg in args)}.",
            )


#################################################
# "Compiler"
#################################################


@dataclass
class UndeterminedValue:
    # For debug only.
    name: str


@dataclass
class UndeterminedMethod:
    # For debug only.
    name: str


@dataclass
class CompilationContext:
    slots: dict[str, Union[Value, MultiMethod, UndeterminedValue, UndeterminedMethod]]
    base: Optional[Union[Context, "CompilationContext"]]

    def lookup(
        self, slot: str
    ) -> Optional[Union[Value, MultiMethod, UndeterminedValue, UndeterminedMethod]]:
        ctxt = self
        while ctxt:
            if slot in ctxt.slots:
                return ctxt.slots[slot]
            ctxt = ctxt.base
        return None


@dataclass
class CompiledBody:
    body: Expr
    # Only None if this compiled body has been invalidated.
    bytecode: Optional[BytecodeSequence]
    comp_ctxt: "CompilationContext"

    def __post_init__(self):
        self.maybe_recompile(_initial=True)

    def invalidate(self) -> None:
        self.bytecode = None
        if should_show_compiler_output:
            print("~~~~~~~~ INVALIDATING COMPILED BODY ~~~~~~~~~~~")
            print("COMPILED BODY:", self.body)

    def maybe_recompile(self, _initial: bool = False) -> BytecodeSequence:
        if not self.bytecode:
            # TODO: make a copy of comp_ctxt? but not the .base?
            compiler = Compiler.for_context(self.comp_ctxt)
            compiler.compile_expr(self.body)
            self.bytecode = compiler.sequence
            show_compiler_output(
                self.body, self.bytecode, compiler.multimethod_deps, is_recompilation=(not _initial)
            )
            for multimethod in compiler.multimethod_deps:
                if self not in multimethod.compilations_to_invalidate:
                    multimethod.compilations_to_invalidate.append(self)
        return self.bytecode


def should_inline_multimethod_dispatch(multimethod: MultiMethod) -> bool:
    # TODO: Inline also if heuristics suggest it should be inlined.
    return multimethod.inline_dispatch


def should_inline_quote_method(parent: MultiMethod, method: Method) -> bool:
    # TODO: Also inline if heuristics suggest it should be inlined.
    # TODO: support inlining after figuring out how to handle recursive methods.
    return False
    return method.inline


@dataclass
class JumpIndex:
    index: int


# Compiler for a specific method / quote body in a particular CompilationContext.
class Compiler:
    ctxt: CompilationContext
    sequence: BytecodeSequence
    # List of multimethods which, if changed, invalidate the compilation.
    multimethod_deps: list[MultiMethod]
    # Stack of quote-method-bodies we have inlined so far; when inlining a new quote body, a new
    # Compiler is produced with one more entry on this stack. Each method has associated the
    # bytecode index of the inlined-multimethod starting point.
    inlining_stack: list[Tuple[QuoteMethodBody, int]]

    # Internal use only.
    def __init__(self, ctxt, sequence, multimethod_deps, inlining_stack):
        self.ctxt = ctxt
        self.sequence = sequence
        self.multimethod_deps = multimethod_deps
        self.inlining_stack = inlining_stack

    @classmethod
    def for_context(cls, context: CompilationContext) -> "Compiler":
        # Make a shallow copy so that re-compilation doesn't end up reusing the same
        # already-added (say) local variables.
        # TODO: deep copy..?
        return cls(
            ctxt=CompilationContext(slots=dict(context.slots), base=context.base),
            sequence=BytecodeSequence(code=[]),
            multimethod_deps=[],
            inlining_stack=[],
        )

    def add_bytecode(self, op: str, args: Tuple, span: SourceSpan):
        self.sequence.code.append(BytecodeOp(op=op, args=args, span=span))

    def compile_expr(self, expr: Expr):
        try:
            self._compile_expr(expr)
        except Exception as e:
            print(f"Compilation error while compiling expression: {expr}")
            print(f"Error: {e}")
            raise e

    def _compile_expr(self, expr: Expr):
        assert expr is not None

        def add(op: str, *args, span: SourceSpan):
            self.add_bytecode(op, args, span)

        # TODO: make this less hacky -- should be part of macro / AST rewrite system.
        tail_call = False
        if isinstance(expr, NAryMessageExpr) and [message.value for message in expr.messages] == [
            "TAIL-CALL"
        ]:
            if expr.target is not None:
                raise ValueError("TAIL-CALL: requires no receiver")
            call = expr.args[0]
            while isinstance(call, ParenExpr):
                call = call.inner
            if not (
                isinstance(call, UnaryOpExpr)
                or isinstance(call, BinaryOpExpr)
                or isinstance(call, NameExpr)
                or isinstance(call, UnaryMessageExpr)
                or isinstance(call, NAryMessageExpr)
            ):
                raise ValueError(
                    "TAIL-CALL: must be applied to a unary/binary op or a unary/n-ary message"
                )
            tail_call = True
            expr = call

        # Only the first of the args is actually optional.
        def compile_invocation(
            message: str, args: list[Optional[Expr]], tail_call: bool, span: SourceSpan
        ) -> None:
            assert len(args) > 0
            assert all(arg is not None for arg in args[1:])

            maybe_tail = "tail-" if tail_call else ""

            slot = self.ctxt.lookup(message)
            if not slot:
                raise ValueError(f"Could not compile: unknown slot '{message}'.")

            if isinstance(slot, CompileTimeHandler):
                slot.handler(self, span, *args)
                return
            elif isinstance(slot, UndeterminedValue):
                receiver, args = args[0], args[1:]
                if receiver or args:
                    raise ValueError(
                        f"Could not compile: local slot is an undetermined-value, so does not need receiver or arguments: {message}."
                    )
                add("get-slot", message, span=span)
                return
            elif isinstance(slot, UndeterminedMethod) or isinstance(slot, MultiMethod):
                # You might think we want to compile evaluations of the method's arguments here.
                # However, if one of the multimethod's methods is an intrinsic (for instance if:then:else:),
                # then we need to allow the intrinsic to handle compilation (being given the argument expressions).
                # Wait until (possibly) later to evaluate arguments.
                # EDIT: ^^^ is not really correct. Inline intrinsics can't necessarily do much just with the call-site
                # expression arguments; e.g. if:then: is implemented in terms of if:then:else:, but then we need to
                # inline an invocation of `if: ... then: [...]`, and also apply some optimizations to move the quote
                # closer to the call site in the implementation of if:then:, and _then_ inline the quote...
                for arg in args:
                    if arg is None:
                        add("push-default-receiver", span=span)
                    else:
                        self._compile_expr(arg)

                if isinstance(slot, MultiMethod):
                    if slot not in self.multimethod_deps:
                        self.multimethod_deps.append(slot)
                    if should_inline_multimethod_dispatch(slot):
                        method_args = [
                            (list(method.param_matchers), JumpIndex(None))
                            for method in slot.methods
                        ]
                        jump_index_on_failure = JumpIndex(None)
                        add(
                            "multimethod-dispatch",
                            message,
                            len(args),
                            method_args,
                            jump_index_on_failure,
                            span=span,
                        )
                        return_jump_indices = []
                        for i, method in enumerate(slot.methods):
                            _, entry_jump = method_args[i]
                            entry_jump.index = len(self.sequence.code)
                            if isinstance(method.body, QuoteMethodBody):
                                if should_inline_quote_method(slot, method):
                                    # If already in the inlining stack, then we can handle this as a tail call
                                    # to where we already inlined this method body.
                                    # EDIT: need to get fancier here, since this only works if directly recursively
                                    # calling without any quotes getting in the way (i.e. not even if:then:else:).
                                    # We need to inline quotes at their call sites (including if:then:else:) before
                                    # a check like this would help much.
                                    if any(
                                        method.body == inlined_method
                                        for inlined_method, _ in self.inlining_stack
                                    ):
                                        inlined_start = next(
                                            index
                                            for inlined_method, index in self.inlining_stack
                                            if method.body == inlined_method
                                        )
                                        add("jump", JumpIndex(inlined_start), span=span)
                                    else:
                                        # TODO: need to handle name conflicts (e.g. parameter of inlined function
                                        # has same name as local slot). Well, TODO is to do this in a better way.
                                        # This is pretty hacky.
                                        add("push-context", span=span)
                                        for param in reversed(method.body.param_names):
                                            add("create-slot", param, span=span)
                                            add("drop", span=span)
                                        # Refer to the same bytecode sequence and multimethod deps, so
                                        # that when compiling the inlined body, we add to the outer
                                        # bytecode and outer deps.
                                        # TODO: handle tail-calls within the inlined function (i.e. replace with
                                        # non-tail-call variant, unless the call to this inlined function was a
                                        # tail call in the first place; it's ok to have pop-context afterwards).
                                        inline_compiler = Compiler(
                                            ctxt=CompilationContext(
                                                slots={
                                                    param: UndeterminedValue(param)
                                                    for param in method.body.param_names
                                                },
                                                base=method.body.context,
                                            ),
                                            sequence=self.sequence,
                                            multimethod_deps=self.multimethod_deps,
                                            inlining_stack=self.inlining_stack
                                            + [(method.body, entry_jump.index)],
                                        )
                                        inline_compiler._compile_expr(
                                            method.body.compiled_body.body
                                        )
                                        add("pop-context", span=span)
                                else:
                                    add(
                                        maybe_tail + "invoke-quote",
                                        message,
                                        method.body,
                                        len(args),
                                        span=span,
                                    )
                            elif isinstance(method.body, IntrinsicMethodBody):
                                if method.body.compile_inline is not None and False:
                                    # Ask the intrinsic to compile inline!
                                    method.body.compile_inline(self, tail_call, span)
                                else:
                                    # Just invoke the instrinsic.
                                    add(
                                        maybe_tail + "invoke-intrinsic",
                                        message,
                                        method.body.handler,
                                        len(args),
                                        span=span,
                                    )
                            elif isinstance(method.body, NativeMethodBody):
                                # TODO: inline compile natives?
                                add(
                                    maybe_tail + "invoke-native",
                                    message,
                                    method.body.handler,
                                    len(args),
                                    span=span,
                                )
                            else:
                                raise AssertionError(
                                    f"forgot a method body type: {type(method.body)}"
                                )
                            # Last method can fall through.
                            if i < len(slot.methods) - 1:
                                # The jump arg will be filled in with bytecode index to indicate where to jump after the method body.
                                return_jump_index = JumpIndex(None)
                                return_jump_indices.append(return_jump_index)
                                add("jump", return_jump_index, span=span)
                        return_index = len(self.sequence.code)
                        jump_index_on_failure.index = return_index
                        for return_jump in return_jump_indices:
                            return_jump.index = return_index
                        return
                # Default: just add an invocation.
                add(maybe_tail + "invoke", message, len(args), span=span)
            elif isinstance(slot, Value):
                receiver, args = args[0], args[1:]
                if receiver or args:
                    raise ValueError(
                        f"Could not compile: slot is not a method, so does not need receiver or arguments: {message}."
                    )
                # Don't use the value directly. Look it up at runtime later (it could very well be a local variable).
                # add("push-value", slot, span=span)
                add("get-slot", message, span=span)
            else:
                raise AssertionError(f"forgot a slot type? {type(slot)}: {slot}")

        if isinstance(expr, UnaryOpExpr):
            compile_invocation(expr.op.value, [expr.arg], tail_call=tail_call, span=expr.span)
        elif isinstance(expr, BinaryOpExpr):
            compile_invocation(
                expr.op.value + ":", [expr.left, expr.right], tail_call=tail_call, span=expr.span
            )
        elif isinstance(expr, NameExpr):
            compile_invocation(expr.name.value, [None], tail_call=tail_call, span=expr.span)
        elif isinstance(expr, LiteralExpr):
            literal = expr.literal
            if literal._type == TokenType.SYMBOL:
                add("push-value", SymbolValue(literal.value), span=literal.span)
            elif literal._type == TokenType.NUMBER:
                add("push-value", NumberValue(literal.value), span=literal.span)
            elif literal._type == TokenType.STRING:
                add("push-value", StringValue(literal.value), span=literal.span)
            else:
                raise AssertionError(f"Forgot a literal token type! {literal._type} ({literal})")
        elif isinstance(expr, UnaryMessageExpr):
            compile_invocation(
                expr.message.value, [expr.target], tail_call=tail_call, span=expr.span
            )
        elif isinstance(expr, NAryMessageExpr):
            assert len(expr.messages) == len(expr.args)
            message = "".join(message.value + ":" for message in expr.messages)
            compile_invocation(
                message, [expr.target] + expr.args, tail_call=tail_call, span=expr.span
            )
        elif isinstance(expr, ParenExpr):
            self._compile_expr(expr.inner)
        elif isinstance(expr, QuoteExpr):
            # TODO: this is where `it` default param needs to be added (or not).
            if not expr.parameters:
                param_names = ["it"]
            else:
                param_names = expr.parameters
            body_comp_ctxt = CompilationContext(
                slots={param: UndeterminedValue(param) for param in param_names}, base=self.ctxt
            )
            quote = QuoteValue(
                parameters=expr.parameters,
                compiled_body=CompiledBody(expr.body, bytecode=None, comp_ctxt=body_comp_ctxt),
                context=None,  # will be filled in later during evaluation
                span=expr.span,
            )
            add("push-closure", quote, span=expr.span)
        elif isinstance(expr, DataExpr):
            for component in expr.components:
                self._compile_expr(component)
            add("components>vector", len(expr.components), span=expr.span)
        elif isinstance(expr, SequenceExpr):
            assert expr.sequence != []
            for i, part in enumerate(expr.sequence):
                self._compile_expr(part)
                is_last = i == len(expr.sequence) - 1
                if not is_last:
                    add("drop", span=part.span)
        elif isinstance(expr, TupleExpr):
            for component in expr.components:
                self._compile_expr(component)
            add("components>tuple", len(expr.components), span=expr.span)
        else:
            raise AssertionError(f"Forgot an expression type! {type(expr)}")


should_show_compiler_output = True


def show_compiler_output(
    expr: Expr,
    sequence: BytecodeSequence,
    multimethod_deps: list[MultiMethod],
    is_recompilation: bool,
):
    if not should_show_compiler_output:
        return

    print("===== COMPILER OUTPUT =====")
    if is_recompilation:
        print("(RECOMPILATION)")
    print("INPUT EXPRESSION:", expr)

    def print_op(index, bytecode, depth):
        level = "    "
        indent = level * depth

        if depth >= 5:
            print(indent + "(WARNING: recursion depth exceeded")
            return

        def prefix():
            print(indent + f"{index}: {bytecode.op}", end="")

        def basic_print_op():
            prefix()
            print("".join(" " + str(arg) for arg in bytecode.args))

        if bytecode.op == "multimethod-dispatch":
            message, nargs, methods, jump_index_on_failure = bytecode.args
            prefix()
            print(f" {message} {nargs}")
            for matchers, jump in methods:

                def matcher_str(matcher):
                    if isinstance(matcher, ParameterAnyMatcher):
                        return "<any>"
                    elif isinstance(matcher, ParameterTypeMatcher):
                        return matcher.param_type.name
                    elif isinstance(matcher, ParameterValueMatcher):
                        return f"eq({matcher.param_value})"

                print(
                    indent
                    + level
                    + f"->{jump.index} on match: {', '.join(matcher_str(matcher) for matcher in matchers)}"
                )
            print(indent + level + f"->{jump_index_on_failure.index} on failure")
        # elif bytecode.op in ["invoke-quote", "tail-invoke-quote"]:
        #     prefix()
        #     message = bytecode.args[0]
        #     quote: QuoteMethodBody = bytecode.args[1]
        #     nargs: int = bytecode.args[2]
        #     print(f" {message} {quote.compiled_body.body} {nargs}")
        #     if quote.compiled_body.bytecode:
        #         for i, c in enumerate(quote.compiled_body.bytecode.code):
        #             print_op(i, c, depth + 1)
        #     else:
        #         print(indent + level + "<invalidated, needs recompilation>")
        # elif bytecode.op == "push-closure":
        #     basic_print_op()
        #     quote: QuoteValue = bytecode.args[0]
        #     if quote.compiled_body.bytecode:
        #         for i, c in enumerate(quote.compiled_body.bytecode.code):
        #             print_op(i, c, depth + 1)
        #     else:
        #         print(indent + level + "<invalidated, needs recompilation>")
        else:
            basic_print_op()

    print("BYTECODE:")
    for i, c in enumerate(sequence.code):
        print_op(i, c, depth=1)
    print("MULTIMETHOD DEPS:")
    for multimethod in multimethod_deps:
        print("  " + multimethod.name)
    print("=========== END ===========")


#################################################
# Runtime Interpreter / Evaluation
#################################################

should_log_states = True


def debug_log_state(state: RuntimeState) -> None:
    if not should_log_states:
        return

    print("============ RUNTIME STATE ===============")

    for depth, frame in enumerate(state.call_stack):
        if frame.spot == len(frame.sequence.code):
            location_msg = f"just after "
            bytecode = frame.sequence.code[-1]
        else:
            location_msg = f"at "
            bytecode = frame.sequence.code[frame.spot]
        location_msg += f"{repr(bytecode.span.file.source[bytecode.span.start.index:bytecode.span.end.index])} (at {bytecode.span})"
        print(f"call frame #{depth}: ({frame.name}) {location_msg}")

        flags = []
        if frame.is_cleanup:
            flags.append("is-cleanup")
        if frame.force_unwind:
            flags.append("force-unwind")
        if flags:
            print("  " + " ".join(flags))

        if frame.num_nonlocal_returns != 0:
            print(f"  return continuations returning here: {frame.num_nonlocal_returns}")

        print("  bytecode:")
        for spot, bytecode in enumerate(frame.sequence.code):
            prefix = "  -> " if spot == frame.spot else "     "
            if bytecode.op == "multimethod-dispatch":
                message, nargs, methods, jump_index_on_failure = bytecode.args

                def matcher_str(matcher):
                    if isinstance(matcher, ParameterAnyMatcher):
                        return "<any>"
                    elif isinstance(matcher, ParameterTypeMatcher):
                        return matcher.param_type.name
                    elif isinstance(matcher, ParameterValueMatcher):
                        return f"eq({matcher.param_value})"

                args = [
                    message,
                    nargs,
                    *[
                        f"({','.join(matcher_str(matcher) for matcher in matchers)})->{jump.index}"
                        for matchers, jump in methods
                    ],
                    f"err->{jump_index_on_failure.index}",
                ]
            else:
                args = bytecode.args
            print(
                f"{prefix} #{spot} {bytecode.op}{''.join(' ' + (repr(arg.value) if isinstance(arg, StringValue) else str(arg)) for arg in args)}"
            )
        if frame.spot == len(frame.sequence.code):
            print("  -> [about to return]")

        print(f"  default-receiver: {frame.default_receiver}")

        print("  context:" + ("" if frame.context.base else " <global>"))
        if frame.context.base:
            context = frame.context
            print("    slots:")
            while context.base:
                for slot, value in context.slots.items():
                    if isinstance(value, Context):
                        value_str = "<a context> (!!!!!!)"
                    else:
                        value_str = str(value)
                    print(f"      {slot} -> {value_str}")
                context = context.base
            print("    <global>")

        if frame.cleanup:
            print(f"  cleanup-action: {frame.cleanup}")

        if frame.cleanup_retain:
            if isinstance(frame.cleanup_retain, Context):
                value_str = "<a context> (!!!!!!)"
            else:
                value_str = str(frame.cleanup_retain)
            print(f"  cleanup-action retain value: {value_str}")

        print("  data stack:")
        for value in frame.data_stack:
            if isinstance(value, Context):
                value_str = "<a context>"
            elif isinstance(value, Expr):
                value_str = f"<expr {value}>"
            else:
                value_str = str(value)
            print(f"  >> {value_str}")

    if state.panic_value:
        print("panic value:", state.panic_value)

    print("================ END =====================")


def eval_one_op(state: RuntimeState) -> None:
    assert state
    assert state.call_stack
    frame = state.call_stack[-1]
    assert 0 <= frame.spot <= len(frame.sequence.code)
    assert not (frame.is_cleanup and frame.force_unwind)

    debug_log_state(state)

    if frame.spot == len(frame.sequence.code) or frame.force_unwind:
        # Return from invocation. The current frame's top-of-data-stack holds the return value,
        # so push this to the next lower call frame's data stack. (Well, that's mostly true.
        # If the current frame is_cleanup, then the return value is held in cleanup_retain, so
        # pass this down to the next frame instead. Also, if the next frame _also_ is_cleanup,
        # then we don't want to set its cleanup_retain at all; this is just a regular return
        # to an in-progress cleanup action which already has a return value retained.)

        if frame.is_cleanup:
            assert frame.cleanup_retain
            return_value = frame.cleanup_retain
        else:
            # The frame may have multiple values in the data stack, for instance if it is
            # performing a non-local return from within a subexpression.
            return_value = frame.data_stack.pop()

        cleanup_action = frame.cleanup

        state.call_stack.pop()

        if cleanup_action:
            call_impl(
                "call",
                state,
                receiver=cleanup_action,
                args=[],
                cleanup=None,
                is_cleanup=True,
                cleanup_retain=return_value,
                tail_call=False,
            )
        else:
            next_frame = state.call_stack[-1]
            # Sanity check:
            if next_frame.is_cleanup:
                assert next_frame.cleanup_retain
            next_frame.data_stack.append(return_value)

        return

    bytecode = frame.sequence.code[frame.spot]
    op = bytecode.op

    def signal_error(
        condition_name: str,
        error_message: str,
        state: RuntimeState,
        source_exception: Optional[Exception],
        already_signaled: bool,
    ):
        # Don't infinitely recurse if condition-handling itself signals a condition.
        if already_signaled:
            # TODO: spin up a debugger or something.
            raise RunError(
                f"Evaluation error while attempting to signal ({condition_name}): {error_message}",
                runtime_state=state,
            ) from source_exception

        # Call into language-space to handle the condition.
        signal_message = "handle-signal:"
        ctxt = frame.context
        slot = None
        while ctxt is not None:
            if signal_message in ctxt.slots:
                slot = ctxt.slots[signal_message]
                break
            ctxt = ctxt.base

        if not slot:
            # TODO: spin up a debugger or something.
            raise RunError(
                f"Could not handle-signal: evaluation error ({condition_name}): {error_message}",
                runtime_state=state,
            ) from source_exception

        signal_arg = TupleValue(
            components=[SymbolValue(condition_name), StringValue(error_message)]
        )
        frame.data_stack.append(frame.context)
        frame.data_stack.append(signal_arg)
        _do_invoke(
            state, frame, signal_message, slot, nargs=2, tail_call=False, already_signaled=True
        )

    def _do_invoke_method_body(
        state, frame, message, method_body, args, tail_call, already_signaled
    ):
        # TODO: refactor to separate methods per conditional block.
        if isinstance(method_body, QuoteMethodBody):
            # TODO: is there an earlier chance to recompile? Maybe this is fine since this only recompiles
            # if already compiled and then later invalidated...
            method_body.compiled_body.maybe_recompile()
            body_ctxt = Context(slots={}, base=method_body.context)
            assert len(args) == len(
                method_body.param_names
            ), f"{len(args)} != len({method_body.param_names})"
            for param_name, arg in zip(method_body.param_names, args):
                body_ctxt.slots[param_name] = arg

            # TODO: use reified context as default receiver?
            invoke_compiled(
                state,
                message,
                method_body.compiled_body.bytecode,
                body_ctxt,
                default_receiver=NullValue(),
                cleanup=None,
                is_cleanup=False,
                cleanup_retain=None,
                tail_call=tail_call,
            )
        elif isinstance(method_body, IntrinsicMethodBody):
            try:
                handler = method_body.handler
                # Allow the intrinsic handler to take arbitrary control of the runtime. It also takes
                # responsibility for updating the frame as necessary.
                handler.handler(state, tail_call, *args)
            except Exception as e:
                # TODO: allow handlers to raise more targeted exceptions that already include a condition name
                signal_error(
                    condition_name="internal-error",
                    error_message=f"error from intrinsic handler: {e}",
                    state=state,
                    source_exception=e,
                    already_signaled=already_signaled,
                )
                return
        elif isinstance(method_body, NativeMethodBody):
            if tail_call:
                print(
                    "WARNING: tail call requested, but could not be applied to native method body"
                )
            try:
                handler = method_body.handler
                # handler is something which can be called with a context, receiver, and arguments.
                # It should not call evaluation functions. (It can, but the stack is not reified and
                # uses host language stack instead).
                result = handler.handler(frame.context, *args)
            except Exception as e:
                # TODO: allow handlers to raise more targeted exceptions that already include a condition name
                signal_error(
                    condition_name="internal-error",
                    error_message=f"error from builtin handler: {e}",
                    state=state,
                    source_exception=e,
                    already_signaled=already_signaled,
                )
                return
            assert isinstance(
                result, Value
            ), f"Result from builtin handler '{message}' must be a Value; got '{result}'."
            frame.data_stack.append(result)
            frame.spot += 1
        else:
            raise AssertionError(f"Unexpected method body '{method_body}'")

    def _do_invoke(state, frame, message, slot, nargs, tail_call, already_signaled):
        if isinstance(slot, Value):
            frame.data_stack = frame.data_stack[:-nargs]
            frame.data_stack.append(slot)
            frame.spot += 1
        else:
            # Note: args includes the receiver.
            args = frame.data_stack[len(frame.data_stack) - nargs :]
            frame.data_stack = frame.data_stack[:-nargs]
            if isinstance(slot, MultiMethod):
                multimethod = slot

                try:
                    method = multimethod.select_method(args)
                except SignalError as e:
                    signal_error(
                        condition_name=e.condition_name,
                        error_message=str(e),
                        state=state,
                        source_exception=None,
                        already_signaled=already_signaled,
                    )
                    return

                _do_invoke_method_body(
                    state, frame, message, method.body, args, tail_call, already_signaled
                )
            elif isinstance(slot, CompileTimeHandler):
                raise AssertionError(
                    f"Shouldn't have gotten here: calling compile time handler '{slot}'"
                )
            else:
                raise AssertionError(f"Unexpected slot value '{slot}'")

    if op == "get-slot":
        (slot,) = bytecode.args
        assert isinstance(slot, str)
        # Find the slot value:
        ctxt = frame.context
        value = None
        while ctxt is not None:
            if slot in ctxt.slots:
                value = ctxt.slots[slot]
                break
            ctxt = ctxt.base
        assert value, "compilation issue?"
        frame.data_stack.append(value)
        frame.spot += 1
    elif op == "create-slot":
        (slot,) = bytecode.args
        assert isinstance(slot, str)
        assert slot not in frame.context.slots, "compilation issue?"
        assert frame.data_stack
        # TODO: often this will be followed by a 'drop'; peephole optimize?
        frame.context.slots[slot] = frame.data_stack[-1]
        frame.spot += 1
    elif op == "push-default-receiver":
        assert not bytecode.args
        frame.data_stack.append(frame.default_receiver)
        frame.spot += 1
    elif op == "push-value":
        (v,) = bytecode.args
        assert isinstance(v, Value)
        frame.data_stack.append(v)
        frame.spot += 1
    elif op == "push-closure":
        (quote,) = bytecode.args
        assert isinstance(quote, QuoteValue)
        assert quote.context is None
        closure = QuoteValue(
            parameters=quote.parameters,
            compiled_body=quote.compiled_body,
            context=frame.context,
            span=quote.span,
        )
        frame.data_stack.append(closure)
        frame.spot += 1
    elif op == "multimethod-dispatch":
        # `methods` is not really methods, just (param-matchers, jump-index) pairs.
        message, nargs, methods, jump_index_on_failure = bytecode.args
        assert isinstance(message, str)
        assert isinstance(nargs, int)
        assert nargs > 0
        assert len(frame.data_stack) >= nargs
        for matchers, jump_index in methods:
            assert len(matchers) == nargs
            for matcher in matchers:
                assert isinstance(matcher, ParameterMatcher)
            assert isinstance(jump_index, JumpIndex)
            assert 0 <= jump_index.index <= len(frame.sequence.code)
        assert isinstance(jump_index_on_failure, JumpIndex)
        assert 0 <= jump_index_on_failure.index <= len(frame.sequence.code)

        # Note: args includes the receiver.
        args = frame.data_stack[len(frame.data_stack) - nargs :]
        # Don't pop the args; the method body expects to use them.

        # Assumes / requires that the dispatch options are already sorted in an order where we can
        # just do a linear scan.
        options = []
        for method in methods:
            param_matchers, _ = method
            if all(matcher.matches(arg) for arg, matcher in zip(args, param_matchers)):
                options.append(method)
        if len(options) == 1:
            _, jump_index = options[0]
            frame.spot = jump_index.index
        elif len(options) == 0:
            # TODO: this is hacky; the `- 1` is to handle signal_error() ----> call_impl() eventually shifting the frame over.
            # Probably need to update shifting to properly support success/failure shifting per bytecode.
            frame.spot = jump_index_on_failure.index - 1
            signal_error(
                condition_name="no-matching-method",
                error_message=f"No matching method found for multi-method {message} "
                f"with values: {', '.join(str(arg) for arg in args)}.",
                state=state,
                source_exception=None,
                already_signaled=False,
            )
        else:
            # Determine if there is a single most-specific option. Otherwise the resolution is
            # ambiguous.
            # Note that the methods is pre-sorted to allow these comparisons.
            candidate = min(options, key=methods.index)
            candidate_matchers, jump_index = candidate
            if all(
                all(matcher_lte(ma, mb) for ma, mb in zip(candidate_matchers, option_matchers))
                for option_matchers, _ in options
            ):
                frame.spot = jump_index.index
                return
            # TODO: this is hacky; the `- 1` is to handle signal_error() ----> call_impl() eventually shifting the frame over.
            # Probably need to update shifting to properly support success/failure shifting per bytecode.
            frame.spot = jump_index_on_failure.index - 1
            signal_error(
                condition_name="ambiguous-method-resolution",
                error_message=f"There were multiple matching methods found for multi-method {message} "
                f"with values: {', '.join(str(arg) for arg in args)}.",
                state=state,
                source_exception=None,
                already_signaled=False,
            )
    elif op == "invoke" or op == "tail-invoke":
        message, nargs = bytecode.args
        assert isinstance(message, str)
        assert isinstance(nargs, int)
        assert nargs > 0
        assert len(frame.data_stack) >= nargs

        tail_call = op == "tail-invoke"

        # Find the slot value:
        ctxt = frame.context
        slot = None
        while ctxt is not None:
            if message in ctxt.slots:
                slot = ctxt.slots[message]
                break
            ctxt = ctxt.base
        if not slot:
            signal_error(
                condition_name="undefined-slot",
                error_message=f"Could not invoke '{message}'; slot is not defined.",
                state=state,
                source_exception=None,
                already_signaled=False,
            )
            return

        _do_invoke(state, frame, message, slot, nargs, tail_call, already_signaled=False)
    elif op == "invoke-quote" or op == "tail-invoke-quote":
        message, method_body, nargs = bytecode.args
        assert isinstance(message, str)
        assert isinstance(method_body, QuoteMethodBody)
        assert isinstance(nargs, int)
        assert nargs > 0
        assert len(frame.data_stack) >= nargs

        tail_call = op == "tail-invoke-quote"

        # Note: args includes the receiver.
        args = frame.data_stack[len(frame.data_stack) - nargs :]
        frame.data_stack = frame.data_stack[:-nargs]

        _do_invoke_method_body(
            state, frame, message, method_body, args, tail_call, already_signaled=False
        )
    elif op == "invoke-intrinsic" or op == "tail-invoke-intrinsic":
        message, intrinsic_handler, nargs = bytecode.args
        assert isinstance(message, str)
        assert isinstance(intrinsic_handler, IntrinsicHandler)
        assert isinstance(nargs, int)
        assert nargs > 0
        assert len(frame.data_stack) >= nargs

        tail_call = op == "tail-invoke-intrinsic"

        # Note: args includes the receiver.
        args = frame.data_stack[len(frame.data_stack) - nargs :]
        frame.data_stack = frame.data_stack[:-nargs]

        _do_invoke_method_body(
            state,
            frame,
            message,
            IntrinsicMethodBody(intrinsic_handler, compile_inline=None),
            args,
            tail_call,
            already_signaled=False,
        )
    elif op == "invoke-native" or op == "tail-invoke-native":
        message, native_handler, nargs = bytecode.args
        assert isinstance(message, str)
        assert isinstance(native_handler, NativeHandler)
        assert isinstance(nargs, int)
        assert nargs > 0
        assert len(frame.data_stack) >= nargs

        tail_call = op == "tail-invoke-native"

        # Note: args includes the receiver.
        args = frame.data_stack[len(frame.data_stack) - nargs :]
        frame.data_stack = frame.data_stack[:-nargs]

        _do_invoke_method_body(
            state,
            frame,
            message,
            NativeMethodBody(native_handler),
            args,
            tail_call,
            already_signaled=False,
        )
    elif op == "components>vector":
        (length,) = bytecode.args
        assert isinstance(length, int)
        assert 0 <= length <= len(frame.data_stack)
        components = frame.data_stack[len(frame.data_stack) - length :]
        frame.data_stack = frame.data_stack[: len(frame.data_stack) - length]
        frame.data_stack.append(VectorValue(components))
        frame.spot += 1
    elif op == "components>tuple":
        (length,) = bytecode.args
        assert isinstance(length, int)
        assert 0 <= length <= len(frame.data_stack)
        components = frame.data_stack[len(frame.data_stack) - length :]
        frame.data_stack = frame.data_stack[: len(frame.data_stack) - length]
        frame.data_stack.append(TupleValue(components))
        frame.spot += 1
    elif op == "drop":
        assert not bytecode.args
        frame.data_stack.pop()
        frame.spot += 1
    elif op == "jump":
        (jump_index,) = bytecode.args
        assert isinstance(jump_index, JumpIndex)
        assert 0 <= jump_index.index <= len(frame.sequence.code)
        frame.spot = jump_index.index
    elif op == "push-context":
        assert not bytecode.args
        frame.context = Context(slots={}, base=frame.context)
        frame.spot += 1
    elif op == "pop-context":
        assert not bytecode.args
        assert frame.context.base
        frame.context = frame.context.base
        frame.spot += 1
    else:
        raise AssertionError(f"Forgot a bytecode op! {op}")


def invoke_compiled(
    state: RuntimeState,
    name: str,
    code: BytecodeSequence,
    ctxt: Context,
    default_receiver: Value,
    cleanup: Optional[Value],
    is_cleanup: bool,
    cleanup_retain: Optional[Value],
    tail_call: bool,
) -> None:
    if not is_cleanup:
        assert not cleanup_retain
    assert not (cleanup and is_cleanup)

    # There may not be any call frame active, for instance if we are invoking the
    # cleanup action of some top-level block with tail-call optimization in effect.
    if state.call_stack:
        frame = state.call_stack[-1]

        if is_cleanup:
            # This could be a tail-call cleanup-action invocation.
            assert 0 <= frame.spot <= len(frame.sequence.code)
        else:
            assert 0 <= frame.spot < len(frame.sequence.code)

        # Tail-call optimization! Don't tail-call if the frame has a non-local return
        # continuation active, as tail-calling would invalidate that continuation.
        # Also don't tail-call if the current frame has a cleanup action or if the
        # current frame _is_ a cleanup action, though, to keep things simpler.
        # TODO: support tail-call if current frame and even next frame both have cleanup
        # actions required.
        if tail_call:
            if frame.spot != len(frame.sequence.code) - 1 or (
                frame.is_cleanup or frame.cleanup or frame.num_nonlocal_returns > 0
            ):
                print("WARNING: not performing tail-call even though requested")
                print(
                    f"details: spot={frame.spot}, codelen={len(frame.sequence.code)}, is_cleanup={frame.is_cleanup}, cleanup={frame.cleanup}, num_nonlocal_returns={frame.num_nonlocal_returns}"
                )
                tail_call = False
        if tail_call:
            # Unwind the frame. It hasn't yet produced a return value; that's what
            # we are about to calculate with a new frame.
            state.call_stack.pop()
        # Don't shift the frame if is_cleanup, since in this case we are setting
        # up a frame to handle a cleanup action, and this frame was _not_ created
        # due to explicitly invoking some callable object (i.e. as a normal part
        # of the frame's bytecode sequence), but rather automatically while unwinding
        # a frame.
        elif not is_cleanup:
            frame.spot += 1

    state.call_stack.append(
        CallFrame(
            name=name,
            sequence=code,
            spot=0,
            data_stack=[],
            context=ctxt,
            default_receiver=default_receiver,
            cleanup=cleanup,
            is_cleanup=is_cleanup,
            cleanup_retain=cleanup_retain,
            force_unwind=False,
            num_nonlocal_returns=0,
        )
    )


def shift_frame(frame: CallFrame) -> None:
    frame.spot += 1
    assert 0 <= frame.spot <= len(frame.sequence.code)


def shift_top_frame(state: RuntimeState) -> None:
    assert state.call_stack
    frame = state.call_stack[-1]
    shift_frame(frame)


def eval_toplevel(expr: Expr, context: Context) -> Value:
    compiler = Compiler.for_context(context=CompilationContext(slots={}, base=context))
    compiler.compile_expr(expr)
    bytecode = compiler.sequence
    show_compiler_output(expr, bytecode, compiler.multimethod_deps, is_recompilation=False)

    if not bytecode.code:
        # Must have been all compile-time evaluation.
        return NullValue()

    state = RuntimeState(
        # TODO: maybe default_receiver should be a reified global context instead?
        call_stack=[
            CallFrame(
                name="<top level>",
                sequence=bytecode,
                spot=0,
                data_stack=[],
                context=context,
                default_receiver=NullValue(),
                cleanup=None,
                is_cleanup=False,
                cleanup_retain=None,
                force_unwind=False,
                num_nonlocal_returns=0,
            )
        ],
        panic_value=None,
    )
    while True:
        if len(state.call_stack) == 1:
            frame = state.call_stack[0]
            if (frame.spot == len(frame.sequence.code) or frame.force_unwind) and not frame.cleanup:
                break
        eval_one_op(state)
    debug_log_state(state)
    if state.panic_value:
        raise EvaluationError(state.panic_value)
    frame = state.call_stack[0]
    assert len(frame.data_stack) == 1
    if frame.is_cleanup:
        assert frame.cleanup_retain
        return frame.cleanup_retain
    else:
        return frame.data_stack[0]


def show_error(header_prefix: str, span: SourceSpan):
    start, end = span.start, span.end
    print(f"{header_prefix} (at {span}):")

    context_lines = 2  # excluding error line itself
    color_output = True

    text_lines = span.file.source.split("\n")
    for line, text in enumerate(text_lines):
        if color_output:
            if not (start.line - context_lines <= line <= end.line + context_lines):
                continue
            if line == start.line:
                end_col = end.column if end.line == start.line else len(text)
                print(
                    colored("! ", "blue")
                    + text[: start.column]
                    + colored(text[start.column : end_col], "red")
                    + text[end_col:]
                )
            elif start.line < line < end.line:
                print(colored("! ", "blue") + colored(text, "red"))
            elif line == end.line:
                print(
                    colored("! ", "blue") + colored(text[: end.column], "red") + text[end.column :]
                )
            else:
                # Just context line.
                print(colored("| ", "green") + text)
        else:
            if start.line - context_lines <= line <= end.line + context_lines:
                print(f"| {text}")
            if line == start.line:
                end_col = end.column if end.line == start.line else len(text)
                print("! " + " " * start.column + "^" * (end_col - start.column))
            elif start.line < line < end.line:
                ws = len(text) - len(text.lstrip(" "))
                print("! " + " " * ws + "^" * (len(text) - ws))
            elif line == end.line:
                ws = len(text) - len(text.lstrip(" "))
                print("! " + " " * ws + "^" * (end.column - ws))


def pprint_stacktrace(state: RuntimeState):
    for i in range(len(state.call_stack)):
        depth = len(state.call_stack) - 1 - i
        frame = state.call_stack[depth]

        first = i == 0
        if first:
            msg = f"Evaluation error in {frame.name}"
        else:
            msg = f"Which was invoked from {frame.name}"

        top = depth == len(state.call_stack) - 1
        if top:
            # Cursor spot has not been ratcheted past the bytecode op producing an error.
            # Log the current spot.
            assert 0 <= frame.spot < len(frame.sequence.code)
            show_error(msg, frame.sequence.code[frame.spot].span)
        else:
            # Cursor indicates the bytecode op to _return_ to; the previous op was the
            # invocation leading to the next call frame.
            assert 0 < frame.spot <= len(frame.sequence.code)
            show_error(msg, frame.sequence.code[frame.spot - 1].span)
        print()


#################################################
# Intrinsic Handlers
#################################################
# These are generally message handlers which require control of the runtime state beyond being a
# function from value inputs to value output.
# The `builtin` module imports these and adds to the default global context.


def intrinsic__if_then_else_(
    state: RuntimeState, tail_call: bool, receiver: Value, cond: Value, tbody: Value, fbody: Value
) -> None:
    body = tbody if isinstance(cond, BoolValue) and cond.value else fbody
    if isinstance(body, QuoteValue):
        # TODO: use reified context as default receiver?
        invoke_compiled(
            state,
            "<a quote>",
            body.compiled_body.maybe_recompile(),
            body.context,
            default_receiver=NullValue(),
            cleanup=None,
            is_cleanup=False,
            cleanup_retain=None,
            tail_call=tail_call,
        )
    else:
        state.call_stack[-1].data_stack.append(body)
        shift_top_frame(state)


def compile_intrinsic__if_then_else_(
    compiler: Compiler, args: list[Optional[Expr]], tail_call: bool, span: SourceSpan
):
    # This needs to emit some more basic bytecodes which evaluate args,
    # check condition, then call the correct body. Other optimization passes will
    # be required to actually migrate quotes to the call site so we can fully inline.
    raise NotImplementedError()
    receiver, cond, tbody, fbody = args
    if receiver:
        # Receiver is ignored, but evaluation may have side effects.
        compiler.compile_expr(receiver)
        compiler.add_bytecode("drop", (), span)
    compiler.compile_expr(cond)
    fbody_entry = JumpIndex(None)
    compiler.add_bytecode("jump-if-not-true", (fbody_entry,), span)
    if isinstance(tbody, QuoteExpr):
        compiler.compile_quote_inline(tbody, tail_call)
    else:
        compiler.compile_expr(tbody)
    end = JumpIndex(None)
    compiler.add_bytecode("jump", (end,), span)
    fbody_entry.index = len(compiler.sequence.code)
    if isinstance(fbody, QuoteExpr):
        compiler.compile_quote_inline(fbody, tail_call)
    else:
        compiler.compile_expr(fbody)
    end.index = len(compiler.sequence.code)


def call_impl(
    message: str,
    state: RuntimeState,
    receiver: Value,
    args: list[Value],
    cleanup: Optional[Value],
    is_cleanup: bool,
    cleanup_retain: Optional[Value],
    tail_call: bool,
):
    if not is_cleanup:
        assert not cleanup_retain
    assert not (cleanup and is_cleanup)

    if isinstance(receiver, QuoteValue):
        # Special-case no parameters: use parameter name "it", and accept 0 or 1 arguments.
        if not receiver.parameters and len(args) <= 1:
            param_names = ["it"]
            default_receiver = args[0] if args else NullValue()
            args = [default_receiver]
        elif len(receiver.parameters) != len(args):
            raise ValueError(
                f"{message} receiver (a quote) has {len(receiver.parameters)} parameter(s), but is being provided {len(args)} argument(s)"
            )
        else:
            param_names = receiver.parameters
            # TODO: use reified context as default receiver?
            default_receiver = NullValue()
        new_slots = {name: value for name, value in zip(param_names, args)}
        invoke_compiled(
            state,
            message,
            receiver.compiled_body.maybe_recompile(),
            Context(slots=new_slots, base=receiver.context),
            default_receiver=default_receiver,
            cleanup=cleanup,
            is_cleanup=is_cleanup,
            cleanup_retain=cleanup_retain,
            tail_call=tail_call,
        )
    elif isinstance(receiver, ContinuationValue):
        if cleanup or is_cleanup:
            raise NotImplementedError()
        if len(args) != 1:
            raise ValueError(
                f"{message} receiver (a continuation) requires 1 parameter, but is being provided {len(args)} argument(s)"
            )
        continuation = receiver.state
        # TODO: fully delete (multi-shot) continuations? This does not interact nicely with return-continuations
        # at all. For instance, using any regular continuation will invalidate all return continuations, even
        # if not actually jumping sideways through call stacks.
        state.call_stack = [frame.copy() for frame in continuation.call_stack]
        state.call_stack[-1].data_stack.append(args[0])
    elif isinstance(receiver, ReturnContinuationValue):
        if cleanup or is_cleanup:
            raise NotImplementedError()
        if len(args) != 1:
            raise ValueError(
                f"{message} receiver (a return-continuation) requires 1 parameter, but is being provided {len(args)} argument(s)"
            )
        return_to_frame = receiver.return_to
        # Look for the frame in anything below the current top of stack.
        # (This only supports unwinding to a strictly lower point in the stack;
        # no sideways motion allowed in the current frame.)
        found_frame = False
        for frame in reversed(state.call_stack[:-1]):
            if frame is return_to_frame:
                found_frame = True
                break
        if not found_frame:
            raise ValueError(f"{message} receiver (a return-continuation) is no longer in scope")
        # This return-continuation is being finalized, so reduce the reference count on the frame.
        assert frame.num_nonlocal_returns >= 1
        frame.num_nonlocal_returns -= 1
        # Force frames from top-of-stack down to just above the `return_to_frame` to exit early
        # when we next get around to running any bytecode. Leave is_cleanup frames alone, though;
        # we still want to execute those!
        for frame in reversed(state.call_stack):
            if frame is return_to_frame:
                break
            if not frame.is_cleanup:
                frame.force_unwind = True
        state.call_stack[-1].data_stack.append(args[0])
        assert state.call_stack
        shift_top_frame(state)
    else:
        # Calling a non-callable value; the result is simply the value.
        if cleanup:
            frame = state.call_stack[-1]
            # Someone called `cleanup:` on a non-callable value. Kinda pointless, but run the
            # cleanup action regardless.
            call_impl(
                "call",
                state,
                receiver=cleanup,
                args=[],
                cleanup=None,
                is_cleanup=True,
                cleanup_retain=receiver,
                # Don't tail-call the cleanup action; presumably this is not what was requested
                # as a tail-call by the user when they indicated tail-call for the entire `cleanup:`
                # invocation.
                tail_call=False,
            )
            # This is a bit hacky. call_impl() -> invoke_compiled() doesn't realize that the
            # cleanup body has no call frame and that the _previous_ frame therefore needs
            # to be shifted instead. We just do that here instead.
            shift_frame(frame)
        else:
            state.call_stack[-1].data_stack.append(receiver)
            shift_top_frame(state)


def intrinsic__call(state: RuntimeState, tail_call: bool, receiver: Value) -> None:
    call_impl(
        "call",
        state,
        receiver,
        args=[],
        cleanup=None,
        is_cleanup=False,
        cleanup_retain=None,
        tail_call=tail_call,
    )


def compile_intrinsic__call(
    compiler: Compiler, args: list[Optional[Expr]], tail_call: bool, span: SourceSpan
):
    raise NotImplementedError()


def intrinsic__call_(state: RuntimeState, tail_call: bool, receiver: Value, value: Value) -> None:
    call_impl(
        "call:",
        state,
        receiver,
        args=[value],
        cleanup=None,
        is_cleanup=False,
        cleanup_retain=None,
        tail_call=tail_call,
    )


def compile_intrinsic__call_(
    compiler: Compiler, args: list[Optional[Expr]], tail_call: bool, span: SourceSpan
):
    raise NotImplementedError()


def intrinsic__call_star_(
    state: RuntimeState, tail_call: bool, receiver: Value, value: Value
) -> None:
    assert isinstance(value, TupleValue)
    call_impl(
        "call*:",
        state,
        receiver,
        args=value.components,
        cleanup=None,
        is_cleanup=False,
        cleanup_retain=None,
        tail_call=tail_call,
    )


def compile_intrinsic__call_star_(
    compiler: Compiler, args: list[Optional[Expr]], tail_call: bool, span: SourceSpan
):
    raise NotImplementedError()


def intrinsic__call_cc(state: RuntimeState, tail_call: bool, receiver: Value) -> None:
    current_continuation = ContinuationValue(
        state=RuntimeState(
            call_stack=[frame.copy() for frame in state.call_stack],
            panic_value=state.panic_value,
        )
    )
    call_impl(
        "call/cc",
        state,
        receiver,
        args=[current_continuation],
        cleanup=None,
        is_cleanup=False,
        cleanup_retain=None,
        tail_call=tail_call,
    )


def intrinsic__cleanup_(
    state: RuntimeState, tail_call: bool, receiver: Value, cleanup: Value
) -> None:
    call_impl(
        "cleanup:",
        state,
        receiver,
        args=[],
        cleanup=cleanup,
        is_cleanup=False,
        cleanup_retain=None,
        tail_call=tail_call,
    )


def compile_intrinsic__cleanup_(
    compiler: Compiler, args: list[Optional[Expr]], tail_call: bool, span: SourceSpan
):
    raise NotImplementedError()


def intrinsic__call_rc(state: RuntimeState, tail_call: bool, receiver: Value) -> None:
    return_continuation = ReturnContinuationValue(return_to=state.call_stack[-1])
    # Reference-count the return continuations.
    return_continuation.return_to.num_nonlocal_returns += 1
    call_impl(
        "call/rc",
        state,
        receiver,
        args=[return_continuation],
        cleanup=None,
        is_cleanup=False,
        cleanup_retain=None,
        tail_call=tail_call,
    )


def compile_intrinsic__call_rc(
    compiler: Compiler, args: list[Optional[Expr]], tail_call: bool, span: SourceSpan
):
    raise NotImplementedError()


def intrinsic__panic_(state: RuntimeState, tail_call: bool, receiver: Value, value: Value) -> None:
    pprint_stacktrace(state)
    # Force all frames to unwind except for frames actively cleaning up.
    for frame in reversed(state.call_stack):
        if not frame.is_cleanup:
            frame.force_unwind = True
    # HACK: this value is passed down is_cleanup frames but then must be ignored
    # by eval_toplevel().
    state.call_stack[-1].data_stack.append("<this value must not be used!>")
    state.panic_value = value
    shift_top_frame(state)


# Each handler is a tuple (param-matchers, intrinsic-handler, optional[compile-inline], inline?)
intrinsic_handlers = {
    "if:then:else:": (
        (
            ParameterAnyMatcher(),
            ParameterTypeMatcher(BoolType),
            ParameterAnyMatcher(),
            ParameterAnyMatcher(),
        ),
        IntrinsicHandler(intrinsic__if_then_else_),
        compile_intrinsic__if_then_else_,
        True,
    ),
    "call": (
        (ParameterAnyMatcher(),),
        IntrinsicHandler(intrinsic__call),
        compile_intrinsic__call,
        True,
    ),
    "call:": (
        (ParameterAnyMatcher(), ParameterAnyMatcher()),
        IntrinsicHandler(intrinsic__call_),
        compile_intrinsic__call_,
        True,
    ),
    "call*:": (
        (ParameterAnyMatcher(), ParameterTypeMatcher(TupleType)),
        IntrinsicHandler(intrinsic__call_star_),
        compile_intrinsic__call_star_,
        True,
    ),
    "call/cc": ((ParameterAnyMatcher(),), IntrinsicHandler(intrinsic__call_cc), None, False),
    "cleanup:": (
        (ParameterAnyMatcher(), ParameterTypeMatcher(QuoteType)),
        IntrinsicHandler(intrinsic__cleanup_),
        compile_intrinsic__cleanup_,
        True,
    ),
    # call/return-continuation
    "call/rc": (
        (ParameterAnyMatcher(),),
        IntrinsicHandler(intrinsic__call_rc),
        compile_intrinsic__call_rc,
        True,
    ),
    "panic!:": (
        (ParameterAnyMatcher(), ParameterAnyMatcher()),
        IntrinsicHandler(intrinsic__panic_),
        # Panics should not be common; inline not really required.
        None,
        False,
    ),
}

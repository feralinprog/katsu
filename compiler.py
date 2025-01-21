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
from typing import Optional, Tuple, Union

from termcolor import colored

from interpreter import (
    BytecodeOp,
    BytecodeSequence,
    CompiledBody,
    CompileTimeHandler,
    Context,
    IntrinsicHandler,
    IntrinsicMethodBody,
    JumpIndex,
    Method,
    MultiMethod,
    NativeHandler,
    NativeMethodBody,
    NullValue,
    NumberValue,
    ParameterAnyMatcher,
    ParameterMatcher,
    ParameterTypeMatcher,
    ParameterValueMatcher,
    QuoteMethodBody,
    QuoteValue,
    StringValue,
    SymbolValue,
    Value,
)
from lexer import TokenType
from span import SourceSpan


# Singleton.
@dataclass
class DefaultReceiver:
    pass


default_receiver = DefaultReceiver()


@dataclass
class CompilationContext:
    # Indicates where each slot value may be found.
    slots: list[Tuple[Union[str, DefaultReceiver], "Register"]]
    base: Union[Context, "CompilationContext"]

    @staticmethod
    def from_context(ctxt: Context) -> "CompilationContext":
        assert isinstance(ctxt, Context)
        return CompilationContext(slots=[], base=ctxt)

    @staticmethod
    def stacked_compilation_context(
        slots: list, base: Union[Context, "CompilationContext"]
    ) -> "CompilationContext":
        assert isinstance(base, (Context, CompilationContext))
        ## Just add all the slots already on the "stack". This wastes some memory, but
        ## makes it easy to determine slot indices.
        # return CompilationContext(slots=list(ctxt.slots), base=ctxt)
        return CompilationContext(slots=list(slots), base=base)

    # If the name is found within compilation-context slots, returns the register holding the slot value.
    # Otherwise, if the name is found within regular-context slots, returns that value.
    # Otherwise, returns None.
    # Example:
    #   For the following compilation context:
    #       <top> CompilationContext
    #           default_receiver: [2]
    #           "local-var":      [3]
    #       <base> CompilationContext
    #           default_receiver: [0]
    #           "outer-var":      [1]
    #       <base> Context
    #           "global-var": <some value>
    #           ...
    #   we have the following lookup results:
    #       default_receiver -> [2]
    #       "local-var" -> [3]
    #       "outer-var" -> [1]
    #       "global-var" -> <some value>
    #       "nonexistent-var" -> None
    def lookup(
        self, name: Union[str, DefaultReceiver]
    ) -> Optional[Union["Register", Value, MultiMethod, CompileTimeHandler]]:
        ctxt = self
        while isinstance(ctxt, CompilationContext):
            for slot, value in ctxt.slots:
                if name == slot:
                    return value
            ctxt = ctxt.base
        # Just look up in the regular Context.
        assert isinstance(ctxt, Context)
        assert (
            name is not default_receiver
        ), "shouldn't have gotten here... someone forgot to add a default_receiver slot!"
        while ctxt:
            if name in ctxt.slots:
                return ctxt.slots[name]
            ctxt = ctxt.base
        return None


@dataclass(frozen=True)
class Register:
    index: int


@dataclass(frozen=True)
class SlotRegister(Register):
    def __str__(self):
        return f"[{self.index}]"


@dataclass(frozen=True)
class VirtualRegister(Register):
    def __str__(self):
        # return f"@{self.index}({id(self) % 1000})"
        return f"@{self.index}"

    # def repoint_to(self, other: "VirtualRegister") -> None:
    #    assert isinstance(other, VirtualRegister)
    #    self.index = other.index


# Intermediate Representation op
@dataclass(kw_only=True)
class IROp:
    span: SourceSpan
    # Some instructions don't output a result.
    dst: Optional[Register] = None
    # Example: method blocks for a multimethod dispatch, or an inlined if/then/else.
    # The `args` may hold references to these blocks; this is just a standard form
    # for other analyses to be able to use.
    sub_blocks: list["IRBlock"] = field(default_factory=list)


# Not really an operation on its own, just hosts a sub-block.
@dataclass(kw_only=True)
class InlineBlockOp(IROp):
    span = None
    dst = None

    def __post_init__(self):
        assert not self.dst
        assert len(self.sub_blocks) == 1


# Not really an operation, just a jump target.
@dataclass(kw_only=True)
class Label(IROp):
    id: int

    def __post_init__(self):
        assert not self.dst


@dataclass(kw_only=True)
class JumpOp(IROp):
    target: Label

    def __post_init__(self):
        assert not self.dst


@dataclass(kw_only=True)
class UnreachableOp(IROp):
    # May have a destination register, as a stand-in.
    # Dead code elimination should end up deleting all instances of such a register.
    pass


@dataclass(kw_only=True)
class ReturnOp(IROp):
    value: Register

    def __post_init__(self):
        assert not self.dst


@dataclass(kw_only=True)
class CopyOp(IROp):
    src: Register


# CopyOp, except the value copied to destination is selected based on which block
# was just executed.
@dataclass(kw_only=True)
class PhiOp(IROp):
    srcs: list[Register]
    # If true, this phi op must not be optimized away, even if there is only a single source.
    # This is effectively an "open" phi, where additional sources may be added later (due to
    # tail-recursion).
    force_keep: bool = False


@dataclass(kw_only=True)
class LiteralOp(IROp):
    value: Value


@dataclass(kw_only=True)
class BaseInvokeOp(IROp):
    call_args: list[Register]
    tail_call: bool
    tail_position: bool

    def __post_init__(self):
        # Require a destination register, even if tail-call. This simplifies matters, and
        # extra registers will be removed later via dead code elimination.
        assert self.dst is not None


@dataclass(kw_only=True)
class InvokeRegisterOp(BaseInvokeOp):
    callable: Register


@dataclass(kw_only=True)
class InvokeMultimethodOp(BaseInvokeOp):
    multimethod: MultiMethod


@dataclass(kw_only=True)
class InvokeMethodOp(BaseInvokeOp):
    method: Method
    method_start_label: Label


@dataclass(kw_only=True)
class InvokeQuoteOp(BaseInvokeOp):
    quote: QuoteMethodBody


@dataclass(kw_only=True)
class InvokeIntrinsicOp(BaseInvokeOp):
    intrinsic: IntrinsicHandler


@dataclass(kw_only=True)
class InvokeNativeOp(BaseInvokeOp):
    native: NativeHandler


@dataclass(kw_only=True)
class ClosureOp(IROp):
    quote: QuoteValue


@dataclass(kw_only=True)
class SlotLookupOp(IROp):
    slot_name: str


@dataclass(kw_only=True)
class VectorOp(IROp):
    components: list[Register]


@dataclass(kw_only=True)
class TupleOp(IROp):
    components: list[Register]


@dataclass(kw_only=True)
class MultimethodDispatchOp(IROp):
    slot_name: str
    dispatch_args: list[Register]
    dispatch: list[Tuple[Method, "IRBlock"]]
    dispatch_failed: "IRBlock"


@dataclass(kw_only=True)
class SignalOp(IROp):
    condition_name: str
    signal_args: list[Register]


@dataclass(kw_only=True)
class IfElseOp(IROp):
    condition: Register
    true_block: "IRBlock"
    false_block: "IRBlock"


@dataclass
class InliningInfo:
    body: QuoteMethodBody
    allows_tail_recursion: bool
    # Everything below here is required if allows_tail_recursion, and must be None otherwise.
    # ---------------------------------------------------------------------------------------
    # Phi op per <default-receiver> and method parameter/argument.
    # The body must have been compiled to use these outputs as the argument values.
    argument_phi_ops: Optional[list[PhiOp]]
    entry_point: Optional[Label]


@dataclass
class IRBlock:
    ops: list[IROp]
    # Context to use when compiling / optimizing anything in the body of this block.
    ctxt: CompilationContext
    # Stack of (information about) quote-method-bodies we have inlined so far in the static scope of this block.
    inlining_stack: list[InliningInfo]


def should_inline_multimethod_dispatch(multimethod: MultiMethod) -> bool:
    # TODO: Inline also if heuristics suggest it should be inlined.
    return multimethod.inline_dispatch


def should_inline_quote_method(method: Method) -> bool:
    # TODO: Also inline if heuristics suggest it should be inlined.
    return method.inline


# Compilation process:
# * start with expression and a compilation context
# * compile to tree-form high level IR
# * do some optimizations, including inlining / intrinsic handling
# * flatten the tree into lower level IR
# * convert to bytecodes (or do native compilation... but that's a later project)


# Compiler for a specific method / quote body in a particular CompilationContext.
class Compiler:
    ir: IRBlock
    # List of multimethods which, if changed, invalidate the compilation.
    multimethod_deps: list[MultiMethod]
    # How many quote-method-bodies or just quote-closures deep we are into inlining.
    # TODO: is this deletable? need to figure out the tail-call inlining...
    inlining_depth: int
    # Keep track of virtual register count for allocation purposes.
    num_virtual_regs: int
    # Keep track of label count for allocation purposes.
    num_labels: int

    # Internal use only.
    def __init__(
        self,
        ir,
        multimethod_deps,
        inlining_depth,
        num_virtual_regs,
        num_labels,
    ):
        self.ir = ir
        self.multimethod_deps = multimethod_deps
        self.inlining_depth = inlining_depth
        self.num_virtual_regs = num_virtual_regs
        self.num_labels = num_labels

    @classmethod
    def for_context(cls, context: CompilationContext) -> "Compiler":
        # Make a shallow copy so that re-compilation doesn't end up reusing the same
        # already-added (say) local variables.
        # TODO: deep copy..?
        return cls(
            ir=IRBlock(
                ops=[],
                ctxt=CompilationContext.stacked_compilation_context(
                    slots=list(context.slots), base=context.base
                ),
                inlining_stack=[],
            ),
            multimethod_deps=[],
            inlining_depth=0,
            num_virtual_regs=0,
            num_labels=0,
        )

    # Add an IR operation, and return the destination register (for convenience).
    def add_ir_op(self, block: IRBlock, op: IROp) -> Optional[Register]:
        block.ops.append(op)
        return op.dst

    # Allocate the next available virtual register.
    def allocate_virtual_reg(self) -> VirtualRegister:
        reg = VirtualRegister(self.num_virtual_regs)
        self.num_virtual_regs += 1
        return reg

    # Allocate the next available label.
    def allocate_label(self) -> Label:
        label = Label(id=self.num_labels, span=None)
        self.num_labels += 1
        return label

    @staticmethod
    def compile_quote_method_body(method: QuoteMethodBody) -> "Compiler":
        compiler = Compiler(
            ir=IRBlock(
                ops=[],
                ctxt=CompilationContext.stacked_compilation_context(
                    slots=[], base=method.compiled_body.comp_ctxt
                ),
                inlining_stack=[],
            ),
            multimethod_deps=[],
            inlining_depth=0,
            num_virtual_regs=0,
            num_labels=0,
        )

        # TODO: offset slot registers (initial inputs to method) if there are already slot registers assigned?
        # e.g. if defining a method within another method.
        # method definitions outside of top level are probably totally broken right now...
        if method.tail_recursive:
            span = method.compiled_body.body.span

            entry_point = compiler.allocate_label()
            compiler.ir.ops.append(entry_point)

            # Includes default-receiver.
            argument_phi_ops = []
            for i, name in enumerate([default_receiver] + method.param_names):
                op = PhiOp(
                    dst=compiler.allocate_virtual_reg(),
                    srcs=[SlotRegister(i)],
                    force_keep=True,
                    span=span,
                )
                argument_phi_ops.append(op)
                compiler.ir.ops.append(op)
                compiler.ir.ctxt.slots.append((name, op.dst))

            compiler.ir.inlining_stack.append(
                InliningInfo(
                    body=method,
                    allows_tail_recursion=True,
                    argument_phi_ops=argument_phi_ops,
                    entry_point=entry_point,
                )
            )
        else:
            compiler.ir.ctxt.slots.append((default_receiver, SlotRegister(0)))
            for i, name in enumerate(method.param_names):
                compiler.ir.ctxt.slots.append((name, SlotRegister(i + 1)))

        compiler._compile_expr(method.compiled_body.body)

        return compiler

    @staticmethod
    def compile_toplevel_expr(ctxt: Context, expr: Expr) -> "Compiler":
        compiler = Compiler(
            ir=IRBlock(
                ops=[],
                ctxt=CompilationContext.stacked_compilation_context(slots=[], base=ctxt),
                inlining_stack=[],
            ),
            multimethod_deps=[],
            inlining_depth=0,
            num_virtual_regs=0,
            num_labels=0,
        )

        compiler.ir.ctxt.slots.append(
            (
                default_receiver,
                compiler.add_ir_op(
                    compiler.ir,
                    LiteralOp(
                        dst=compiler.allocate_virtual_reg(), value=NullValue(), span=expr.span
                    ),
                ),
            )
        )

        compiler._compile_expr(expr)

        return compiler

    def _compile_expr(self, expr: Expr) -> "Compiler":
        try:
            print(colored("~~~~~~~~~ COMPILATION PROCESS ~~~~~~~~~~~~", "cyan"))

            print("input expression:", expr)
            print(colored("input compilation context (bottom -> top of stack):", "green"))
            stack_from_top = []
            ctxt = self.ir.ctxt
            while isinstance(ctxt, CompilationContext):
                stack_from_top.append(ctxt)
                ctxt = ctxt.base

            for ctxt in reversed(stack_from_top):
                print("  context:")
                for slot, value in ctxt.slots:
                    print(f"    {slot} -> {value}")

            print(colored("initial compilation to tree IR:", "red"))
            result_reg = self.compile_expr(self.ir, expr, tail_position=True)
            if result_reg:
                self.add_ir_op(self.ir, ReturnOp(value=result_reg, span=expr.span))
            print_ir_block(self.ir, depth=1)

            any_change = True
            iteration = 0

            def optimize(summary: str, fn):
                nonlocal any_change
                print(colored(f"{summary} (iteration={iteration}):", "red"))
                delta = fn()
                if delta:
                    any_change = True
                    print_ir_block(self.ir, depth=1)
                else:
                    print("    (no delta)")
                "a breakpoint right after printing latest state"

            while any_change:
                any_change = False
                iteration += 1

                # Inline multimethod dispatches to open up the method invocations.
                optimize("inlining multimethod dispatches", self.tree_inline_multimethod_dispatches)
                # Inline method invocations to open up the method bodies and hopefully allow
                # closure inlining.
                optimize("inlining methods", self.tree_inline_methods)
                # Try to move closures to their call sites.
                optimize("propagating copies", self.tree_propagate_copies)
                optimize("inlining closures", self.tree_inline_closures)

            # Tail recursion could have produced unnecessary phi nodes; now we can clean those up.
            # TODO: optimize("simplifying phi nodes post-inlining", self.simplify_phi_nodes)
            # !! From this point on, no more inlining methods. !!

            optimize("propagating copies", self.tree_propagate_copies)
            # TODO: self.convert_to_basic_blocks()

            any_change = True
            while any_change:
                any_change = False
                iteration += 1
                # TODO: optimize("inferring types", self.bb_infer_types)
                # This includes:
                # * applying type knowledge to simplify multimethod dispatches
                # * deleting UnreachableOp instances (and the registers they "write" to)
                # TODO: optimize("eliminating dead code", self.bb_eliminate_dead_code)
                # TODO: optimize("propagating copies", self.bb_propagate_copies)

            # print("compiling to low level bytecode:")
            # self.compile_to_low_level_bytecode()
            self.low_level_bytecode = "<TODO, don't have low level bytecode yet>"
            print(colored("~~~~~~~~~ COMPILATION PROCESS END ~~~~~~~~~~~~", "grey"))
        except Exception as e:
            print(f"Compilation error while compiling body: {expr}")
            print(f"Error: {e}")
            raise e

    # Add IROps which evaluate the given expression.
    def compile_expr(self, block: IRBlock, expr: Expr, tail_position: bool) -> Optional[Register]:
        assert expr is not None

        # TODO: make this less hacky -- should be part of macro / AST rewrite system.
        tail_call = False
        if isinstance(expr, NAryMessageExpr) and [message.value for message in expr.messages] == [
            "TAIL-CALL"
        ]:
            tail_call = True
            if not tail_position:
                if self.inlining_depth > 0:
                    # This is ok -- the tail-call got inlined somewhere where it is no longer in
                    # tail position. Inlining is stronger than tail-calling, anyway.
                    tail_call = False
                else:
                    raise ValueError("TAIL-CALL: must be invoked in tail position")

            if expr.target is not None:
                raise ValueError("TAIL-CALL: requires no receiver")
            call = expr.args[0]
            while isinstance(call, ParenExpr):
                call = call.inner
            if not isinstance(
                call, (UnaryOpExpr, BinaryOpExpr, NameExpr, UnaryMessageExpr, NAryMessageExpr)
            ):
                raise ValueError(
                    "TAIL-CALL: must be applied to a unary/binary op or a unary/n-ary message"
                )
            expr = call

        # Only the first of the args is actually optional.
        def compile_invocation(
            message: str, args: list[Optional[Expr]], tail_call: bool, span: SourceSpan
        ) -> Optional[Register]:
            assert len(args) > 0
            assert all(arg is not None for arg in args[1:])

            slot = block.ctxt.lookup(message)
            if not slot:
                raise ValueError(f"Could not compile: unknown slot '{message}'.")
            assert isinstance(slot, (Register, Value, MultiMethod, CompileTimeHandler)), slot

            if isinstance(slot, Register):
                receiver, args = args[0], args[1:]
                if receiver or args:
                    raise ValueError(
                        f"Could not compile: local slot does not need receiver or arguments: {message}."
                    )
                return slot
            if isinstance(slot, CompileTimeHandler):
                return slot.handler(self, block, span, tail_call, *args)
            elif isinstance(slot, Value):
                receiver, args = args[0], args[1:]
                if receiver or args:
                    raise ValueError(
                        f"Could not compile: slot is not a method, so does not need receiver or arguments: {message}."
                    )
                # TODO: could do a similar thing to multimethods, where we add a lease to the slot and if it changes
                # it forces recompilation. Might cause too many invalidations though.
                return self.add_ir_op(
                    block,
                    SlotLookupOp(dst=self.allocate_virtual_reg(), slot_name=message, span=span),
                )
            elif isinstance(slot, MultiMethod):
                arg_regs = []
                for arg in args:
                    if arg is None:
                        default_receiver_reg = block.ctxt.lookup(default_receiver)
                        assert isinstance(default_receiver_reg, Register)
                        arg_reg = default_receiver_reg
                    else:
                        arg_reg = self.compile_expr(block, arg, tail_position=False)
                    arg_regs.append(arg_reg)

                # Even if a tail call, we must add a destination register to be used in case of dispatch failure.
                invocation = InvokeMultimethodOp(
                    dst=self.allocate_virtual_reg(),
                    multimethod=slot,
                    call_args=arg_regs,
                    tail_call=tail_call,
                    tail_position=tail_position,
                    span=span,
                )
                return self.add_ir_op(block, invocation)
            else:
                raise AssertionError(f"forgot a slot type? {type(slot)}: {slot}")

        if isinstance(expr, UnaryOpExpr):
            return compile_invocation(
                expr.op.value, [expr.arg], tail_call=tail_call, span=expr.span
            )
        elif isinstance(expr, BinaryOpExpr):
            return compile_invocation(
                expr.op.value + ":", [expr.left, expr.right], tail_call=tail_call, span=expr.span
            )
        elif isinstance(expr, NameExpr):
            return compile_invocation(expr.name.value, [None], tail_call=tail_call, span=expr.span)
        elif isinstance(expr, LiteralExpr):
            literal = expr.literal
            if literal._type == TokenType.SYMBOL:
                value = SymbolValue(literal.value)
            elif literal._type == TokenType.NUMBER:
                value = NumberValue(literal.value)
            elif literal._type == TokenType.STRING:
                value = StringValue(literal.value)
            else:
                raise AssertionError(f"Forgot a literal token type! {literal._type} ({literal})")
            return self.add_ir_op(
                block, LiteralOp(dst=self.allocate_virtual_reg(), value=value, span=expr.span)
            )
        elif isinstance(expr, UnaryMessageExpr):
            return compile_invocation(
                expr.message.value, [expr.target], tail_call=tail_call, span=expr.span
            )
        elif isinstance(expr, NAryMessageExpr):
            assert len(expr.messages) == len(expr.args)
            message = "".join(message.value + ":" for message in expr.messages)
            return compile_invocation(
                message, [expr.target] + expr.args, tail_call=tail_call, span=expr.span
            )
        elif isinstance(expr, ParenExpr):
            return self.compile_expr(block, expr.inner, tail_position)
        elif isinstance(expr, QuoteExpr):
            # TODO: this is where `it` default param needs to be added (or not).
            if not expr.parameters:
                param_names = ["it"]
            else:
                param_names = expr.parameters
            # TODO: what's right here?
            body_comp_ctxt = CompilationContext.stacked_compilation_context(
                slots=[], base=block.ctxt
            )
            # body_comp_ctxt = CompilationContext.stacked_compilation_context(
            #    slots=[(default_receiver, SlotRegister(0))]
            #    + [(param, SlotRegister(i + 1)) for i, param in enumerate(param_names)],
            #    base=block.ctxt,
            # )
            quote = QuoteValue(
                parameters=expr.parameters,
                compiled_body=CompiledBody(
                    expr.body, bytecode=None, comp_ctxt=body_comp_ctxt, method=None
                ),
                context=None,  # will be filled in later during evaluation
                span=expr.span,
            )
            return self.add_ir_op(
                block, ClosureOp(dst=self.allocate_virtual_reg(), quote=quote, span=expr.span)
            )
        elif isinstance(expr, DataExpr):
            component_regs = []
            for component in expr.components:
                reg = self.compile_expr(block, component, tail_position=False)
                # Each component should produce a result into a register... no tail calls possible here.
                assert reg
                component_regs.append(reg)
            return self.add_ir_op(
                block,
                VectorOp(
                    dst=self.allocate_virtual_reg(), components=component_regs, span=expr.span
                ),
            )
        elif isinstance(expr, SequenceExpr):
            assert expr.sequence != []
            last_output = None
            for i, part in enumerate(expr.sequence):
                part_is_tail_position = tail_position and (i == len(expr.sequence) - 1)
                last_output = self.compile_expr(block, part, part_is_tail_position)
            return last_output
        elif isinstance(expr, TupleExpr):
            component_regs = []
            for component in expr.components:
                reg = self.compile_expr(block, component, tail_position=False)
                # Each component should produce a result into a register... no tail calls possible here.
                assert reg
                component_regs.append(reg)
            return self.add_ir_op(
                block, TupleOp(dst=self.allocate_virtual_reg(), components=component_regs)
            )
        else:
            raise AssertionError(f"Forgot an expression type! {type(expr)}")

    def tree_inline_multimethod_dispatches(self) -> bool:
        def process_block(block: IRBlock) -> bool:
            old_ops = list(block.ops)
            block.ops = []

            any_change = False

            for op in old_ops:
                if not isinstance(op, InvokeMultimethodOp):
                    block.ops.append(op)
                    for sub_block in op.sub_blocks:
                        if process_block(sub_block):
                            any_change = True
                    continue

                multimethod = op.multimethod
                if not should_inline_multimethod_dispatch(multimethod):
                    block.ops.append(op)
                    continue

                any_change = True

                if multimethod not in self.multimethod_deps:
                    self.multimethod_deps.append(multimethod)

                dispatch = []
                method_results = []
                sub_blocks = []
                for method in multimethod.methods:
                    method_block = IRBlock(
                        ops=[], ctxt=block.ctxt, inlining_stack=block.inlining_stack
                    )
                    dispatch.append((method, method_block))
                    sub_blocks.append(method_block)

                    dst = self.allocate_virtual_reg()
                    method_results.append(dst)
                    start_label = self.allocate_label()
                    method_block.ops.append(start_label)
                    method_block.ops.append(
                        InvokeMethodOp(
                            dst=dst,
                            method=method,
                            call_args=op.call_args,
                            tail_call=op.tail_call,
                            tail_position=op.tail_position,
                            method_start_label=start_label,
                            span=op.span,
                        )
                    )

                # Add one more sub-block for the 'dispatch failed' case.
                dispatch_failed = IRBlock(
                    ops=[], ctxt=block.ctxt, inlining_stack=block.inlining_stack
                )
                signal_result = self.allocate_virtual_reg()
                dispatch_failed.ops.append(
                    SignalOp(
                        dst=signal_result,
                        condition_name="no-matching-method",
                        signal_args=op.call_args,
                        span=op.span,
                    )
                )
                sub_blocks.append(dispatch_failed)

                block.ops.append(
                    MultimethodDispatchOp(
                        dst=None,
                        slot_name=multimethod.name,
                        dispatch_args=op.call_args,
                        dispatch=dispatch,
                        dispatch_failed=dispatch_failed,
                        sub_blocks=sub_blocks,
                        span=op.span,
                    )
                )

                if op.dst and method_results:
                    # Re-use the old invoke op's destination register so that downstream consumers
                    # still get a value available in this register.
                    block.ops.append(
                        PhiOp(dst=op.dst, srcs=method_results + [signal_result], span=op.span)
                    )

            return any_change

        return process_block(self.ir)

    def tree_inline_methods(self):
        def process_block(block: IRBlock) -> bool:
            old_ops = list(block.ops)
            block.ops = []

            any_change = False

            for op in old_ops:
                if not isinstance(op, InvokeMethodOp):
                    block.ops.append(op)
                    for sub_block in op.sub_blocks:
                        if process_block(sub_block):
                            any_change = True
                    continue

                any_change = True

                method = op.method
                if isinstance(method.body, QuoteMethodBody):
                    if any(method.body == inlined.body for inlined in block.inlining_stack):
                        inlined = next(
                            info for info in block.inlining_stack if method.body == info.body
                        )
                    else:
                        inlined = None
                    # Stop inlining if we have already inlined the same method, but it doesn't support tail recursion.
                    # If we did try to inline, we would either have to jump back to the entry point (which doesn't exist,
                    # since we didn't compile the original inlining assuming there would be tail recursino), or actually
                    # inline again, which would lead to infinite compilation recursion.
                    if (
                        not inlined or inlined.allows_tail_recursion
                    ) and should_inline_quote_method(method):
                        # If already in the inlining stack, then we can handle this as a jump to where we
                        # already inlined this method body.
                        if inlined:
                            assert len(inlined.argument_phi_ops) == 1 + len(method.body.param_names)
                            for phi_op in inlined.argument_phi_ops:
                                assert isinstance(phi_op, PhiOp)
                                assert phi_op.force_keep
                            default_receiver_phi_op, arg_phi_ops = (
                                inlined.argument_phi_ops[0],
                                inlined.argument_phi_ops[1:],
                            )

                            # Copy arguments into the right slots.
                            # The default receiver is just <null>.
                            default_receiver_reg = self.add_ir_op(
                                block,
                                LiteralOp(
                                    dst=self.allocate_virtual_reg(),
                                    value=NullValue(),
                                    span=op.span,
                                ),
                            )
                            default_receiver_phi_op.srcs.append(default_receiver_reg)

                            assert len(op.call_args) == len(method.body.param_names)
                            for i, param_name in enumerate(method.body.param_names):
                                arg_phi_ops[i].srcs.append(op.call_args[i])

                            self.add_ir_op(block, JumpOp(target=inlined.entry_point, span=op.span))
                            self.add_ir_op(block, UnreachableOp(dst=op.dst, span=op.span))
                        else:
                            # Set up arguments.
                            # The default receiver is just <null>.
                            default_receiver_reg = self.add_ir_op(
                                block,
                                LiteralOp(
                                    dst=self.allocate_virtual_reg(),
                                    value=NullValue(),
                                    span=op.span,
                                ),
                            )
                            if method.body.tail_recursive:
                                default_receiver_phi_op = PhiOp(
                                    dst=self.allocate_virtual_reg(),
                                    srcs=[default_receiver_reg],
                                    force_keep=True,
                                    span=op.span,
                                )

                            assert len(op.call_args) == len(method.body.param_names)
                            if method.body.tail_recursive:
                                argument_phi_ops = []
                                for i, arg_reg in enumerate(op.call_args):
                                    argument_phi_ops.append(
                                        PhiOp(
                                            dst=self.allocate_virtual_reg(),
                                            srcs=[arg_reg],
                                            force_keep=True,
                                            span=op.span,
                                        )
                                    )

                            if method.body.tail_recursive:
                                block.ops.append(default_receiver_phi_op)
                                block.ops.extend(argument_phi_ops)

                            # "Push" the inline context.
                            if method.body.tail_recursive:
                                inline_ctxt = CompilationContext.stacked_compilation_context(
                                    slots=[(default_receiver, default_receiver_phi_op.dst)]
                                    + [
                                        (param, argument_phi_ops[i].dst)
                                        for i, param in enumerate(method.body.param_names)
                                    ],
                                    base=block.ctxt,
                                )
                            else:
                                inline_ctxt = CompilationContext.stacked_compilation_context(
                                    slots=[(default_receiver, default_receiver_reg)]
                                    + [
                                        (param, op.call_args[i])
                                        for i, param in enumerate(method.body.param_names)
                                    ],
                                    base=block.ctxt,
                                )

                            # Indicate that we are currently inlining the method, so if we find recursive evaluation
                            # we know we can just jump back to the starting point of the multimethod.
                            inline_block = IRBlock(
                                ops=list(argument_phi_ops) if method.body.tail_recursive else [],
                                ctxt=inline_ctxt,
                                inlining_stack=block.inlining_stack
                                + [
                                    (
                                        InliningInfo(
                                            body=method.body,
                                            allows_tail_recursion=True,
                                            argument_phi_ops=[default_receiver_phi_op]
                                            + argument_phi_ops,
                                            entry_point=op.method_start_label,
                                        )
                                        if method.body.tail_recursive
                                        else InliningInfo(
                                            body=method.body,
                                            allows_tail_recursion=False,
                                            argument_phi_ops=None,
                                            entry_point=None,
                                        )
                                    )
                                ],
                            )

                            # Finally we are ready to inline the body.
                            self.inlining_depth += 1
                            result = self.compile_expr(
                                inline_block,
                                method.body.compiled_body.body,
                                tail_position=op.tail_position,
                            )
                            self.inlining_depth -= 1

                            # Add the inline block!
                            self.add_ir_op(
                                block, InlineBlockOp(sub_blocks=[inline_block], span=None)
                            )

                            if result:
                                # Copy the result back to where the old invocation operation wrote the result.
                                self.add_ir_op(block, CopyOp(dst=op.dst, src=result, span=op.span))
                    else:
                        # Just invoke the quote body. We can reuse most of the op fields; this is just replacing
                        # an InvokeMethodOp with a more specific InvokeQuoteOp.
                        self.add_ir_op(
                            block,
                            InvokeQuoteOp(
                                dst=op.dst,
                                quote=method.body,
                                call_args=op.call_args,
                                tail_call=op.tail_call,
                                tail_position=op.tail_position,
                                span=op.span,
                            ),
                        )
                elif isinstance(method.body, IntrinsicMethodBody):
                    if method.body.compile_inline is not None:
                        # Ask the intrinsic to compile inline!
                        result = method.body.compile_inline(
                            self, block, op.call_args, op.tail_call, op.tail_position, op.span
                        )
                        assert result is None or isinstance(result, Register)
                        if op.dst and result:
                            # Make sure to write to the old-invocation output register.
                            self.add_ir_op(block, CopyOp(dst=op.dst, src=result, span=op.span))
                        # TODO: what if op.dst but not result? delete the old output register somehow?
                        # should just be handled by dead code elimination later on, I s'pose...
                    else:
                        # Just invoke the intrinsic. We can reuse most of the op fields; this is just replacing
                        # an InvokeMethodOp with a more specific InvokeIntrinsicOp.
                        self.add_ir_op(
                            block,
                            InvokeIntrinsicOp(
                                dst=op.dst,
                                intrinsic=method.body.handler,
                                call_args=op.call_args,
                                tail_call=op.tail_call,
                                tail_position=op.tail_position,
                                span=op.span,
                            ),
                        )
                elif isinstance(method.body, NativeMethodBody):
                    # TODO: inline compile natives?
                    # Just invoke the native. We can reuse most of the op fields; this is just replacing
                    # an InvokeMethodOp with a more specific InvokeNativeOp.
                    self.add_ir_op(
                        block,
                        InvokeNativeOp(
                            dst=op.dst,
                            native=method.body.handler,
                            call_args=op.call_args,
                            tail_call=op.tail_call,
                            tail_position=op.tail_position,
                            span=op.span,
                        ),
                    )
                else:
                    raise AssertionError(f"forgot a method body type: {type(method.body)}")

            return any_change

        return process_block(self.ir)

    def tree_propagate_copies(self) -> bool:
        # Propagate CopyOp assignments forward, even into sub-blocks, until we run into any
        # walls: any JumpOp.
        # We can also merge remappings modified variously by different branches (sub-blocks)
        # of an op.
        # TODO: do we actually need to clear out remappings on JumpOp? maybe not...

        # Remapping is from destination to source in various assignments.
        # Returns a new remapping, and a flag saying if any ops were modified.
        def process_block(block: IRBlock, remapping: dict[Register, Register]) -> Tuple[dict, bool]:
            remapping = dict(remapping)

            any_change = False

            for op in block.ops:
                remapping_per_subblock = []
                for sub_block in op.sub_blocks:
                    subblock_remapping, changed = process_block(sub_block, remapping)
                    if changed:
                        any_change = True
                    remapping_per_subblock.append(subblock_remapping)

                # If there were any branches in the first place, then merge them and use that as
                # our new remapping.
                if remapping_per_subblock:
                    for merger in remapping_per_subblock:
                        for k, v in merger.items():
                            if k not in remapping:
                                remapping[k] = v
                            if remapping[k] != v:
                                del remapping[k]

                def remap(reg: Register) -> Register:
                    nonlocal any_change
                    remapped = remapping.get(reg, reg)
                    if remapped != reg:
                        any_change = True
                    return remapped

                if isinstance(op, InlineBlockOp):
                    # We already got this as part of the sub-block copy propagation.
                    pass
                elif isinstance(op, Label):
                    pass
                elif isinstance(op, JumpOp):
                    # TODO: we don't have to forget all the remappings when hitting a JumpOp, just
                    # mappings that were established after the JumpOp target label. (I think...)
                    remapping = {}
                elif isinstance(op, UnreachableOp):
                    pass
                elif isinstance(op, ReturnOp):
                    op.value = remap(op.value)
                elif isinstance(op, CopyOp):
                    op.src = remap(op.src)
                    if op.dst not in remapping:
                        remapping[op.dst] = op.src
                elif isinstance(op, PhiOp):
                    op.srcs = [remap(src) for src in op.srcs]
                elif isinstance(op, LiteralOp):
                    pass
                elif isinstance(op, InvokeRegisterOp):
                    op.callable = remap(op.callable)
                    op.call_args = [remap(arg) for arg in op.call_args]
                elif isinstance(op, InvokeMultimethodOp):
                    op.call_args = [remap(arg) for arg in op.call_args]
                elif isinstance(op, InvokeMethodOp):
                    op.call_args = [remap(arg) for arg in op.call_args]
                elif isinstance(op, InvokeQuoteOp):
                    op.call_args = [remap(arg) for arg in op.call_args]
                elif isinstance(op, InvokeIntrinsicOp):
                    op.call_args = [remap(arg) for arg in op.call_args]
                elif isinstance(op, InvokeNativeOp):
                    op.call_args = [remap(arg) for arg in op.call_args]
                elif isinstance(op, ClosureOp):
                    pass
                elif isinstance(op, SlotLookupOp):
                    pass
                elif isinstance(op, VectorOp):
                    op.components = [remap(comp) for comp in op.components]
                elif isinstance(op, TupleOp):
                    op.components = [remap(comp) for comp in op.components]
                elif isinstance(op, MultimethodDispatchOp):
                    op.dispatch_args = [remap(arg) for arg in op.dispatch_args]
                elif isinstance(op, SignalOp):
                    op.signal_args = [remap(arg) for arg in op.signal_args]
                elif isinstance(op, IfElseOp):
                    op.condition = remap(op.condition)
                else:
                    raise AssertionError(f"forgot an IROp: {type(op)}")

            return remapping, any_change

        _, any_change = process_block(self.ir, remapping={})
        return any_change

    def tree_inline_closures(self) -> bool:
        # Propagates ClosureOp assignments directly forward to InvokeRegisterOp calls,
        # and inlines these closures at their call sites.

        def process_block(block: IRBlock, closures: dict[Register, QuoteValue]) -> bool:
            closures = dict(closures)
            old_ops = block.ops
            block.ops = []

            any_change = False

            for op in old_ops:
                for sub_block in op.sub_blocks:
                    if process_block(sub_block, closures):
                        any_change = True

                if isinstance(op, InlineBlockOp):
                    # Nothing else to do; we already got this as part of the sub-block processing.
                    block.ops.append(op)
                elif isinstance(op, Label):
                    block.ops.append(op)
                elif isinstance(op, JumpOp):
                    # TODO: do we actually need to clear out mappings here? Maybe not...
                    closures = {}
                    block.ops.append(op)
                elif isinstance(op, UnreachableOp):
                    pass
                elif isinstance(op, ReturnOp):
                    block.ops.append(op)
                elif isinstance(op, CopyOp):
                    block.ops.append(op)
                elif isinstance(op, PhiOp):
                    block.ops.append(op)
                elif isinstance(op, LiteralOp):
                    block.ops.append(op)
                elif isinstance(op, InvokeRegisterOp):
                    if op.callable in closures:
                        # Yay, we can inline!
                        # TODO: see about deduplicating with inline_methods().

                        # The "calling convention" here matches that of call_impl() in the interpreter:
                        # #parameters | #arguments  |  parameters   | default-receiver  |  passed values to parameters
                        # ------------+-------------+----------------------------------------------------------------------------------------
                        #      0      |     0       |   add "it"    |      null         |  null
                        #      0      |     1       |   add "it"    |     args[0]       |  args[0]
                        #      1      |     1       |      -        |     args[0]       |  args[0]
                        #        <otherwise>        |      -        |      null         |  args[0], args[1], ...

                        quote = closures[op.callable]

                        if not (len(quote.parameters) == 0 and len(op.call_args) <= 1) and len(
                            quote.parameters
                        ) != len(op.call_args):
                            raise ValueError(
                                f"quote has {len(quote.parameters)} parameter(s), but is being provided {len(op.call_args)} argument(s)"
                            )

                        # Set up arguments.
                        if len(op.call_args) == 1:
                            default_receiver_reg = op.call_args[0]
                        else:
                            default_receiver_reg = self.add_ir_op(
                                block,
                                LiteralOp(
                                    dst=self.allocate_virtual_reg(),
                                    value=NullValue(),
                                    span=op.span,
                                ),
                            )

                        if not quote.parameters and not op.call_args:
                            arg_reg = self.add_ir_op(
                                block,
                                LiteralOp(
                                    dst=self.allocate_virtual_reg(),
                                    value=NullValue(),
                                    span=op.span,
                                ),
                            )
                            arg_regs = [arg_reg]
                        else:
                            arg_regs = op.call_args

                        # "Push" the inline context.
                        inline_ctxt = CompilationContext.stacked_compilation_context(
                            slots=[(default_receiver, default_receiver_reg)]
                            + [
                                (param, arg_regs[i])
                                for i, param in enumerate(quote.parameters or ["it"])
                            ],
                            # TODO: block.ctxt or quote.ctxt?
                            base=quote.compiled_body.comp_ctxt,
                        )

                        def find_slot(name):
                            slot_result = inline_ctxt.lookup(name)
                            assert isinstance(slot_result, Tuple)
                            slot_reg, _ = slot_result
                            assert isinstance(slot_reg, SlotRegister)
                            return slot_reg

                        # No new quote-method-bodies inlined, just a closure quote. Not tracked on the inlining stack,
                        # since these are first class values (unlike multimethods / multimethod invocation).
                        inline_block = IRBlock(
                            ops=[], ctxt=inline_ctxt, inlining_stack=block.inlining_stack
                        )

                        # Finally we are ready to inline the body.
                        self.inlining_depth += 1
                        result = self.compile_expr(
                            inline_block,
                            quote.compiled_body.body,
                            tail_position=op.tail_position,
                        )
                        self.inlining_depth -= 1

                        # Add the inline block!
                        self.add_ir_op(block, InlineBlockOp(sub_blocks=[inline_block], span=None))

                        if op.dst and result:
                            # Copy the result back to where the old invocation operation wrote the result.
                            self.add_ir_op(block, CopyOp(dst=op.dst, src=result, span=op.span))

                        any_change = True
                    else:
                        block.ops.append(op)
                elif isinstance(op, InvokeMultimethodOp):
                    block.ops.append(op)
                elif isinstance(op, InvokeMethodOp):
                    block.ops.append(op)
                elif isinstance(op, InvokeQuoteOp):
                    block.ops.append(op)
                elif isinstance(op, InvokeIntrinsicOp):
                    block.ops.append(op)
                elif isinstance(op, InvokeNativeOp):
                    block.ops.append(op)
                elif isinstance(op, ClosureOp):
                    assert op.dst not in closures
                    closures[op.dst] = op.quote
                    block.ops.append(op)
                elif isinstance(op, SlotLookupOp):
                    block.ops.append(op)
                elif isinstance(op, VectorOp):
                    block.ops.append(op)
                elif isinstance(op, TupleOp):
                    block.ops.append(op)
                elif isinstance(op, MultimethodDispatchOp):
                    block.ops.append(op)
                elif isinstance(op, SignalOp):
                    block.ops.append(op)
                elif isinstance(op, IfElseOp):
                    block.ops.append(op)
                else:
                    raise AssertionError(f"forgot an IROp: {type(op)}")

            return any_change

        return process_block(self.ir, closures={})

    def compile_to_low_level_bytecode(self) -> None:
        raise NotImplementedError()


should_show_compiler_output = False


def print_ir_block(block: IRBlock, depth: int = 0):
    level = "    "

    def print_op(index: int, op: IROp, depth: int):
        indent = level * depth

        def prefix():
            # print(indent + f"{index}: ", end="")
            print(indent, end="")

        if isinstance(op, InlineBlockOp):
            prefix()
            print("<inline block>:")
            (inline_block,) = op.sub_blocks
            print(indent + level + colored("slots:", "grey"))
            for name, value in inline_block.ctxt.slots:
                print(indent + level + level + colored(f"{name} -> {value}", "grey"))
            print_ir_block(inline_block, depth=(depth + 1))
        elif isinstance(op, Label):
            prefix()
            print(colored(f"{op.id}:", "light_blue"))
        elif isinstance(op, JumpOp):
            prefix()
            print("jump " + colored(f"{op.target.id}", "light_magenta"))
        elif isinstance(op, UnreachableOp):
            prefix()
            if op.dst:
                print(f"{op.dst} = unreachable")
            else:
                print("unreachable")
        elif isinstance(op, ReturnOp):
            prefix()
            print(f"return {op.value}")
        elif isinstance(op, CopyOp):
            prefix()
            print(f"{op.dst} = {op.src}")
        elif isinstance(op, PhiOp):
            prefix()
            print(f"{op.dst} = phi({', '.join(str(src) for src in op.srcs)})", end="")
            if op.force_keep:
                print(" (force-keep)")
            else:
                print()
        elif isinstance(op, LiteralOp):
            prefix()
            print(
                f"{op.dst} = literal {repr(op.value.value) if isinstance(op.value, StringValue) else op.value}"
            )
        elif isinstance(op, BaseInvokeOp):
            prefix()

            if op.dst:
                print(f"{op.dst} = ", end="")
            if op.tail_call:
                print("tail-", end="")

            if isinstance(op, InvokeRegisterOp):
                print(f"invoke {op.callable}", end="")
            elif isinstance(op, InvokeMultimethodOp):
                print(
                    f"invoke-multimethod {op.multimethod.name} (inline_dispatch={op.multimethod.inline_dispatch})",
                    end="",
                )
            elif isinstance(op, InvokeMethodOp):
                print("invoke-method ", end="")
                if isinstance(op.method.body, QuoteMethodBody):
                    print(f"<quote:{op.method.body}>", end="")
                elif isinstance(op.method.body, IntrinsicMethodBody):
                    print(f"<intrinsic:{op.method.body.handler.handler}>", end="")
                elif isinstance(op.method.body, NativeMethodBody):
                    print(f"<native:{op.method.body.handler.handler}>", end="")
                else:
                    raise AssertionError(f"forgot a method body type: {op.method.body}")
                print(f" (label={op.method_start_label.id}, inline={op.method.inline})", end="")
            elif isinstance(op, InvokeQuoteOp):
                print(f"invoke-quote {op.quote}", end="")
            elif isinstance(op, InvokeIntrinsicOp):
                print(f"invoke-intrinsic {op.intrinsic.handler}", end="")
            elif isinstance(op, InvokeNativeOp):
                print(f"invoke-native {op.native.handler}", end="")
            else:
                raise AssertionError(f"forgot an op type: {op}")

            if op.call_args:
                print(f" with {', '.join(str(arg) for arg in op.call_args)}", end="")
            else:
                print(" with <no args>", end="")
            if op.tail_position:
                print(" (in tail position)")
            else:
                print()
        elif isinstance(op, ClosureOp):
            prefix()
            print(f"{op.dst} = closure {op.quote}")
        elif isinstance(op, SlotLookupOp):
            prefix()
            print(f"{op.dst} = slot-lookup {op.slot_name}")
        elif isinstance(op, VectorOp):
            prefix()
            print(f"{op.dst} = vector {', '.join(str(comp) for comp in op.components)}")
        elif isinstance(op, TupleOp):
            prefix()
            print(f"{op.dst} = tuple {', '.join(str(comp) for comp in op.components)}")
        elif isinstance(op, MultimethodDispatchOp):
            prefix()
            print(
                f"multimethod-dispatch {op.slot_name} on {', '.join(str(arg) for arg in op.dispatch_args)}"
            )
            for method, body_block in op.dispatch:

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
                    + f"if ({', '.join(matcher_str(matcher) for matcher in method.param_matchers)}):"
                )
                print_ir_block(body_block, depth=(depth + 2))

            print(indent + level + "else:")
            print_ir_block(op.dispatch_failed, depth=(depth + 2))
        elif isinstance(op, SignalOp):
            prefix()
            print(
                f"{op.dst} = signal :{op.condition_name} with {', '.join(str(arg) for arg in op.signal_args)}"
            )
        elif isinstance(op, IfElseOp):
            assert not op.dst
            prefix()
            print(f"if-else {op.condition}")
            print(indent + level + "if-truthy:")
            print_ir_block(op.true_block, depth=(depth + 2))
            print(indent + level + "if-falsy:")
            print_ir_block(op.false_block, depth=(depth + 2))
        else:
            raise AssertionError(f"forgot an IROp: {type(op)}")

    for i, op in enumerate(block.ops):
        print_op(i, op, depth)


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

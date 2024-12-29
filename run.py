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
    parser,
)

from termcolor import colored, cprint

from error import ParseError, RunError
from interpreter import NullValue, eval, global_context
from lexer import TokenStream, TokenType, get_all_tokens
from span import SourceFile, SourceSpan

source_path = "./source.src"
with open("source.src", "r") as f:
    source = f.read()

parser.should_log = False


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


# pretty-format into a string
# TODO: line wrapping, indenting
def pf(expr: Expr) -> str:
    # Note: doesn't produce trailing newline.
    if isinstance(expr, UnaryOpExpr):
        return "(" + expr.op.value + " " + pf(expr.arg) + ")"
    elif isinstance(expr, BinaryOpExpr):
        return "(" + pf(expr.left) + " " + expr.op.value + " " + pf(expr.right) + ")"
    elif isinstance(expr, NameExpr):
        return expr.name.value
    elif isinstance(expr, LiteralExpr):
        return "lit(" + repr(expr.literal.value) + ")"
    elif isinstance(expr, UnaryMessageExpr):
        return "(" + pf(expr.target) + " " + expr.message.value + ")"
    elif isinstance(expr, NAryMessageExpr):
        return (
            "("
            + (pf(expr.target) + " " if expr.target else "")
            + " ".join(
                message.value + ": " + pf(arg) for message, arg in zip(expr.messages, expr.args)
            )
            + ")"
        )
    elif isinstance(expr, ParenExpr):
        return pf(expr.inner)
    elif isinstance(expr, BlockExpr):
        return "{ " + pf(expr.inner) + " }"
    elif isinstance(expr, SequenceExpr):
        assert expr.sequence != []
        return "; ".join(pf(part) for part in expr.sequence)
    else:
        raise AssertionError(f"Forgot an expression type! {type(expr)}")


try:
    file = SourceFile(source_path=source_path, source=source)
    tokens = get_all_tokens(file)

    stream = TokenStream(tokens)
    while stream.peek()._type != TokenType.EOF:
        top_level_expr = parser.parse(stream, is_toplevel=True)
        print(">> " + colored(pf(top_level_expr), "green"))
        result = eval(top_level_expr, global_context)
        if not isinstance(result, NullValue):
            print(colored(str(result), "blue"))

        # Ratchet past any semicolons and newlines, since the parser explicitly stops
        # when it sees either of these at the top level.
        while stream.peek()._type in [TokenType.SEMICOLON, TokenType.NEWLINE]:
            stream.consume()
except ParseError as e:
    show_error("Parse error", e.span)
    print(e)
except RunError as e:
    while e:
        if isinstance(e, RunError):
            show_error("Evaluation error", e.span)
            print(e)
        else:
            raise e
        e = e.__cause__
        if e:
            print()
            print("(Due to:)")

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
    parser,
)

from termcolor import colored, cprint

from builtin import global_context
from error import ParseError, RunError
from interpreter import NullValue, eval_toplevel
from lexer import Token, TokenStream, TokenType, get_all_tokens
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


def print_syntax_highlighted_tokens(tokens: list[Token]) -> None:
    token_type_to_colorer = {
        TokenType.EOF: (lambda t: colored(t, "light_grey")),
        TokenType.ERROR: (lambda t: colored(t, "white", "on_red")),
        TokenType.SEMICOLON: (lambda t: colored(t, "light_grey")),
        TokenType.NEWLINE: (lambda t: colored(t, "light_grey")),
        TokenType.WHITESPACE: (lambda t: colored(t, "light_grey")),
        TokenType.COMMENT: (lambda t: colored(t, "magenta")),
        TokenType.LPAREN: (lambda t: colored(t, "light_grey")),
        TokenType.RPAREN: (lambda t: colored(t, "light_grey")),
        TokenType.LCURLY: (lambda t: colored(t, "yellow")),
        TokenType.RCURLY: (lambda t: colored(t, "yellow")),
        TokenType.LSQUARE: (lambda t: colored(t, "blue")),
        TokenType.RSQUARE: (lambda t: colored(t, "blue")),
        TokenType.COMMA: (lambda t: colored(t, "light_grey")),
        TokenType.NAME: (lambda t: colored(t, "green")),
        TokenType.MESSAGE: (lambda t: colored(t, "cyan")),
        TokenType.SYMBOL: (lambda t: colored(t, "light_magenta")),
        TokenType.QUOTE: (lambda t: colored(t, "cyan")),
        TokenType.OPERATOR: (lambda t: colored(t, "red")),
        TokenType.NUMBER: (lambda t: colored(t, "light_grey")),
        TokenType.STRING: (lambda t: colored(t, "light_blue")),
    }
    for token in tokens:
        colorer = token_type_to_colorer.get(token._type, lambda t: t)
        text = token.span.file.source[token.span.start.index : token.span.end.index]
        print(colorer(text), end="")
    print()


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
    elif isinstance(expr, QuoteExpr):
        return "[ " + pf(expr.inner) + " ]"
    elif isinstance(expr, DataExpr):
        return "{ " + "; ".join(pf(component) for component in expr.components) + " }"
    elif isinstance(expr, SequenceExpr):
        assert expr.sequence != []
        return "; ".join(pf(part) for part in expr.sequence)
    elif isinstance(expr, TupleExpr):
        return "tuple(" + ", ".join(pf(component) for component in expr.components) + ")"
    else:
        raise AssertionError(f"Forgot an expression type! {type(expr)}")


try:
    file = SourceFile(source_path=source_path, source=source)
    tokens = get_all_tokens(file)

    print("=" * 80)
    print_syntax_highlighted_tokens(tokens)
    print("=" * 80)

    stream = TokenStream(tokens)
    while stream.peek()._type != TokenType.EOF:
        top_level_expr = parser.parse(stream, is_toplevel=True)
        print(">> " + colored(pf(top_level_expr), "green"))
        result = eval_toplevel(top_level_expr, global_context)
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

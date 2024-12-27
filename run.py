from parser import parser

from error import ParseError, RunError
from interpreter import eval, global_context
from lexer import SourceSpan, TokenStream, TokenType, get_all_tokens

source_path = "./source.src"
with open("source.src", "r") as f:
    source = f.read()

parser.should_log = False


def show_error(header_prefix: str, span: SourceSpan):
    start, end = span.start, span.end
    print(f"{header_prefix} (at <{start.source_path}> {start.line + 1}:{start.column + 1}):")

    context_lines = 2  # excluding error line itself

    text_lines = start.source.split("\n")
    for line, text in enumerate(text_lines):
        if start.line - context_lines <= line <= end.line + context_lines:
            print(f"| {text}")
        if line == start.line:
            end_col = end.column if end.line == start.line else len(text)
            print("  " + " " * start.column + "^" * (end_col - start.column))
        elif start.line < line < end.line:
            print("  " + "^" * len(text))
        elif line == end.line:
            print("  " + "^" * end.column)


try:
    tokens = get_all_tokens(source, source_path)

    stream = TokenStream(tokens)
    while stream.peek()._type != TokenType.EOF:
        top_level_expr = parser.parse(stream)
        print("result:", eval(top_level_expr, global_context))
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

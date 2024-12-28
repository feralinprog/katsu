from parser import parser

from termcolor import colored, cprint

from error import ParseError, RunError
from interpreter import eval, global_context
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
                    colored("+ ", "blue")
                    + text[: start.column]
                    + colored(text[start.column : end_col], "red")
                    + text[end_col:]
                )
            elif start.line < line < end.line:
                print(colored("+ ", "blue") + colored(text, "red"))
            elif line == end.line:
                print(
                    colored("+ ", "blue") + colored(text[: end.column], "red") + text[end.column :]
                )
            else:
                # Just context line.
                print(colored("| ", "green") + text)
        else:
            if start.line - context_lines <= line <= end.line + context_lines:
                print(f"| {text}")
            if line == start.line:
                end_col = end.column if end.line == start.line else len(text)
                print("+ " + " " * start.column + "^" * (end_col - start.column))
            elif start.line < line < end.line:
                ws = len(text) - len(text.lstrip(" "))
                print("+ " + " " * ws + "^" * (len(text) - ws))
            elif line == end.line:
                ws = len(text) - len(text.lstrip(" "))
                print("+ " + " " * ws + "^" * (end.column - ws))


try:
    file = SourceFile(source_path=source_path, source=source)
    tokens = get_all_tokens(file)

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

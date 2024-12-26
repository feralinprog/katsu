from parser import parser

from error import ParseError
from lexer import TokenStream, TokenType, get_all_tokens

source_path = "./source.src"
with open("source.src", "r") as f:
    source = f.read()

parser.should_log = False

try:
    tokens = get_all_tokens(source, source_path)

    stream = TokenStream(tokens)
    while stream.peek()._type != TokenType.EOF:
        top_level_expr = parser.parse(stream)
        print(top_level_expr)
except ParseError as e:
    span = e.span
    start, end = span.start, span.end
    print(f"Parse error (at <{start.source_path}> {start.line + 1}:{start.column + 1}):")

    context_lines = 2  # excluding error line itself

    # TODO: improve, this is easy but probably quite slow.
    index, line, col = 0, 0, 0
    while index < len(start.source):
        if start.line - context_lines <= line <= end.line + context_lines:
            # Print the source line.
            print("| ", end="")
            for i in range(index, len(start.source)):
                c = start.source[i]
                if c == "\n":
                    break
                if c != "\r":
                    print(c, end="")
            print()
            # Go back and print error carets, if applicable.
            if start.line <= line <= end.line:
                print("+-", end="")
            while index < len(start.source):
                c = start.source[index]
                if c == "\n":
                    if start.line <= line <= end.line:
                        print()
                    line += 1
                    col = 0
                    index += 1
                    break
                else:
                    if start.line <= line <= end.line:
                        if start.index <= index < end.index or (index == start.index == end.index):
                            print("^", end="")
                        elif index < start.index:
                            print("-", end="")
                        else:
                            print(" ", end="")
                    col += 1
                index += 1
        elif start.line - context_lines > line:
            if c == "\n":
                line += 1
                col = 0
            else:
                col += 1
            index += 1
        else:
            break

    print(f"{e}")

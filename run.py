from parser import parser

from lexer import TokenStream, TokenType, get_all_tokens

with open("source.src", "r") as f:
    tokens = get_all_tokens(f.read(), "source.src")

stream = TokenStream(tokens)
while stream.peek()._type != TokenType.EOF:
    top_level_expr = parser.parse(stream)
    print(top_level_expr)

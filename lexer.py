import dataclasses
import re
from dataclasses import dataclass
from enum import Enum, auto
from typing import Optional

from error import ParseError
from span import SourceLocation, SourceSpan


class TokenType(Enum):
    EOF = auto()
    ERROR = auto()
    SEMICOLON = auto()
    WHITESPACE = auto()
    COMMENT = auto()
    NAME = auto()  # same as operator, except operators have different character set
    OPERATOR = auto()  # same as names, but different character set
    LPAREN = auto()  # (
    RPAREN = auto()  # )
    LCURLY = auto()  # {
    RCURLY = auto()  # }
    MESSAGE = auto()  # <name/operator>: or <name/operator>.
    SYMBOL = auto()  # :<name/operator>
    NUMBER = auto()
    STRING = auto()


@dataclass
class Token:
    span: SourceSpan
    _type: TokenType
    # value depends on the token type
    value: object

    def __str__(self):
        return f"{self._type}:{repr(self.value)}"


def next_token(loc: SourceLocation, source: str) -> Token:
    """
    Arguments:
        loc: current location in the _input
        source: the full input source code
    Returns: a token (which could be an error token or EOF token).
    """
    if loc.index == len(source):
        return Token(span=SourceSpan(loc, loc), _type=TokenType.EOF, value=None)

    regex_handlers = [
        # token _type | regex | (regex match -> value) function
        (TokenType.SEMICOLON, r";", lambda match: match.group(0)),
        (TokenType.WHITESPACE, r"[ \t\r\n]+", lambda match: match.group(0)),
        (TokenType.COMMENT, r"#.*\n?", lambda match: match.group(0)),
        (TokenType.LPAREN, r"\(", lambda match: match.group(0)),
        (TokenType.RPAREN, r"\)", lambda match: match.group(0)),
        (TokenType.LCURLY, r"\{", lambda match: match.group(0)),
        (TokenType.RCURLY, r"\}", lambda match: match.group(0)),
        (
            TokenType.MESSAGE,
            r"([^ \t\r\n\(\)\{\}\[\];:.]+)([:.])",
            lambda match: (match.group(1), match.group(2)),
        ),
        (TokenType.SYMBOL, r"(:([^ \t\r\n\(\)\{\}\[\];:.]+))+", lambda match: match.group(0)[1:]),
        (TokenType.NUMBER, r"[0-9]+", lambda match: int(match.group(0))),
        (
            TokenType.STRING,
            r"\"([^\"]|\\\")*\"",
            lambda match: match.group(0)[1:-1].replace('\\"', '"'),
        ),
        (TokenType.OPERATOR, r"[`~!@#$%^&*\-+=|/\\\(\)\{\}\[\],]+", lambda match: match.group(0)),
        (TokenType.NAME, r"[^ \t\r\n;:.\(\)\{\}\[\]]+", lambda match: match.group(0)),
    ]

    start = dataclasses.replace(loc)
    end = dataclasses.replace(loc)

    rest_of_source = source[start.index :]
    for token_type, regex, handler in regex_handlers:
        match = re.match(regex, rest_of_source)
        if match:
            token_len = len(match.group(0))

            while end.index < start.index + token_len:
                c = source[end.index]
                if c == "\n":
                    end.line += 1
                    end.column = 0
                else:
                    end.column += 1
                end.index += 1

            return Token(span=SourceSpan(start, end), _type=token_type, value=handler(match))

    # Didn't find any matches...
    raise ParseError("Couldn't determine next token.", span=SourceSpan(start, start))


def get_all_tokens(source: str, source_path: str) -> list[Token]:
    tokens = []
    loc = SourceLocation(source_path=source_path, source=source, index=0, line=0, column=0)
    while True:
        token = next_token(loc, source)
        tokens.append(token)
        if token._type == TokenType.EOF:
            break
        loc = token.span.end
    return tokens


class TokenStream:
    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0

    def _skip(self):
        while self.tokens[self.pos]._type in [TokenType.WHITESPACE, TokenType.COMMENT]:
            self.pos += 1

    def peek(self) -> Optional[Token]:
        self._skip()
        return self.tokens[self.pos]

    def next_has_type(self, token_type: TokenType) -> bool:
        token = self.peek()
        return token._type == token_type

    def consume(self, token_type: Optional[TokenType] = None) -> Optional[Token]:
        self._skip()
        token = self.tokens[self.pos]
        self.pos += 1
        if token_type is not None and token._type != token_type:
            raise ParseError(f"Expected {token_type}, got {token._type}.", token.span)
        # print(f"CONSUMED: {token}")
        return token

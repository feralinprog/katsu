import dataclasses
import re
from dataclasses import dataclass
from enum import Enum, auto
from typing import Optional

from error import ParseError
from span import SourceFile, SourceLocation, SourceSpan


class TokenType(Enum):
    EOF = auto()
    ERROR = auto()
    SEMICOLON = auto()
    NEWLINE = auto()
    WHITESPACE = auto()
    COMMENT = auto()
    LPAREN = auto()  # (
    RPAREN = auto()  # )
    LCURLY = auto()  # {
    RCURLY = auto()  # }
    LSQUARE = auto()  # [
    RSQUARE = auto()  # ]
    COMMA = auto()  # ,
    NAME = auto()  # same as operator, except operators have different character set
    MESSAGE = auto()  # <name/operator>: or <name/operator>.
    SYMBOL = auto()  # :<name/operator>
    QUOTE = auto()  # '<name>
    BACKSLASH = auto()  # \
    OPERATOR = auto()  # same as names, but limited character set
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


def next_token(loc: SourceLocation, file: SourceFile) -> Token:
    """
    Arguments:
        loc: current location in the file
        file: the source file to read from
    Returns: a token (which could be an error token or EOF token).
    """
    source = file.source
    if loc.index == len(source):
        return Token(span=SourceSpan(file, loc, loc), _type=TokenType.EOF, value=None)

    def word_to_token(match):
        word = match.group(0)

        if ":" in word:
            if word == ":":
                return (TokenType.ERROR, word)
            # Special case "::" as a message token with name ":".
            # This is purely a convenience.
            if word.startswith(":") and word != "::":
                return (TokenType.SYMBOL, word[1:])
            elif word.endswith(":"):
                return (TokenType.MESSAGE, word[:-1])
            else:
                return (TokenType.ERROR, word)
        if word.startswith("'") and word != "'":
            return (TokenType.QUOTE, word[1:])

        op_chars = "`~!@#$%^&*-+=\\|\"',<.>/?"
        if set(word) <= set(op_chars):
            return (TokenType.OPERATOR, word)

        num_chars = "0123456789"
        if set(word) <= set(num_chars):
            return (TokenType.NUMBER, int(word))

        # Some special cases. (TODO: expose to language?)
        if word in ["and", "or", "not"]:
            return (TokenType.OPERATOR, word)
        return (TokenType.NAME, word)

    regex_handlers = [
        # token _type | regex | (regex match -> value) function
        # or "<defer>" | regex | (regex match -> (token _type, value)) function
        (TokenType.SEMICOLON, r";", lambda match: match.group(0)),
        (TokenType.NEWLINE, r"\n", lambda match: match.group(0)),
        (TokenType.WHITESPACE, r"[ \t\r\n]+", lambda match: match.group(0)),
        (TokenType.COMMENT, r"#.*", lambda match: match.group(0)),
        (TokenType.LPAREN, r"\(", lambda match: match.group(0)),
        (TokenType.RPAREN, r"\)", lambda match: match.group(0)),
        (TokenType.LCURLY, r"\{", lambda match: match.group(0)),
        (TokenType.RCURLY, r"\}", lambda match: match.group(0)),
        (TokenType.LSQUARE, r"\[", lambda match: match.group(0)),
        (TokenType.RSQUARE, r"\]", lambda match: match.group(0)),
        (TokenType.COMMA, r",", lambda match: match.group(0)),
        (TokenType.BACKSLASH, r"\\", lambda match: match.group(0)),
        (
            TokenType.STRING,
            r"\"([^\"]|\\\")*\"",
            lambda match: match.group(0)[1:-1].replace('\\"', '"'),
        ),
        # word_to_token handles anything that looks like a message, symbol, number, operator, or name
        ("<defer>", r"([^ \t\r\n\(\)\{\}\[\];,])+", word_to_token),
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

            if token_type == "<defer>":
                token_type, value = handler(match)
            else:
                value = handler(match)
            return Token(span=SourceSpan(file, start, end), _type=token_type, value=value)

    # Didn't find any matches...
    raise ParseError("Couldn't determine next token.", span=SourceSpan(file, start, start))


def get_all_tokens(file: SourceFile) -> list[Token]:
    tokens = []
    loc = SourceLocation(index=0, line=0, column=0)
    while True:
        token = next_token(loc, file)
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

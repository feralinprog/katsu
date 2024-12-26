import dataclasses
import re
from dataclasses import dataclass
from enum import Enum, auto
from typing import Optional


@dataclass
class SourceLocation:
    # filename, or could be a representation of e.g. internal source or REPL UI
    source: str
    # index of character, just incrementing from 0 to end of file
    index: int
    # line / column
    line: int
    column: int


@dataclass
class SourceSpan:
    # inclusive
    start: SourceLocation
    # exclusive
    end: SourceLocation


def combine_spans(*spans: list[SourceSpan]) -> SourceSpan:
    """Determine the minimal span containing each span in the input list."""
    if len(set([span.start.source for span in spans] + [span.end.source for span in spans])) != 1:
        raise ValueError("There are no spans, or they come from different sources.")
    return SourceSpan(
        min([span.start for span in spans], key=lambda loc: loc.index),
        max([span.start for span in spans], key=lambda loc: loc.index),
    )


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

    def __repr__(self):
        return repr(self.value)


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
    raise ValueError(f"Couldn't parse input at {start}")


source = """
method. (sum: a and: b and: c) does: {
    mut. x;
    x = a + b;
    # equiv: :x = a + b (which is desugared to invoking =: with args :x, a+b)
    let. y =: x + c;
    # equiv: let: :y =: x + c
    c
};
print: ("sum is: " + (sum: 1 and: 2 and: 3))
"""

tokens = []
loc = SourceLocation(source="<source>", index=0, line=0, column=0)
while True:
    token = next_token(loc, source)
    tokens.append(token)
    if token._type == TokenType.EOF:
        break
    loc = token.span.end


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
            raise ValueError(f"Expected {token_type}, got {token._type} at {token.span}")
        # print(f"CONSUMED: {token}")
        return token


@dataclass
class Expr:
    span: SourceSpan


@dataclass
class UnaryOpExpr(Expr):
    op: Token
    arg: Expr

    def __repr__(self):
        return f"unary({repr(self.op)}, {repr(self.arg)})"


@dataclass
class BinaryOpExpr(Expr):
    op: Token
    left: Expr
    right: Expr

    def __repr__(self):
        return f"binary({repr(self.left)}, {repr(self.op)}, {repr(self.right)})"


@dataclass
class NameExpr(Expr):
    name: Token

    def __repr__(self):
        return f"name({repr(self.name)})"


@dataclass
class LiteralExpr(Expr):
    literal: Token

    def __repr__(self):
        return f"literal({repr(self.literal)})"


@dataclass
class UnaryMessageExpr(Expr):
    target: Expr
    message: Token

    def __repr__(self):
        return f"unary-message({repr(self.target)}, {repr(self.message)})"


@dataclass
class NAryMessageExpr(Expr):
    target: Optional[Expr]
    messages: list[Token]
    args: list[Expr]

    def __repr__(self):
        return f"n-ary-message({repr(self.target)}, {repr(self.messages)}, {repr(self.args)})"


@dataclass
class ParenExpr(Expr):
    inner: Expr

    def __repr__(self):
        return repr(self.inner)


@dataclass
class BlockExpr(Expr):
    inner: Expr

    def __repr__(self):
        return "{ " + repr(self.inner) + " }"


@dataclass
class SequenceExpr(Expr):
    sequence: list[Expr]

    def __repr__(self):
        return f"sequence({repr(self.sequence)})"


depth = 0


class PrattParser:
    # prefix parselets:
    #   .parse(token_stream, pratt_parser, current_token)
    # infix parselets:
    #   .parse(token_stream, pratt_parser, left_expr, current_token)
    #   .precedence(current_token)

    def __init__(self):
        self.prefix_parselets = {}
        self.infix_parselets = {}

    def parse(self, stream: TokenStream, precedence: Optional[int] = 0) -> Expr:
        """Parses an expression. Precondition: stream.peek() is not an EOF."""
        global depth
        depth += 1
        token = stream.consume()
        if token._type == TokenType.EOF:
            raise ValueError("Unexpected EOF")

        if token._type not in self.prefix_parselets:
            raise ValueError(f"No prefix parselet available for {token}")
        prefix = self.prefix_parselets[token._type]
        print(
            "| " * (depth - 1)
            + f"parsing prefix {token._type}, prec={precedence}, token={repr(token.value)}"
        )
        expr = prefix.parse(stream, self, token)

        def active_precedence(token):
            if token._type in self.infix_parselets:
                return self.infix_parselets[token._type].precedence(token)
            else:
                # raise ValueError(f"No infix parselets available to determine precedence for {token}")
                return 0

        while active_precedence(stream.peek()) > precedence:
            token = stream.consume()
            print(
                "| " * (depth - 1)
                + f"got infix token {token._type}, prec={precedence}, token={repr(token.value)}"
            )
            if token._type == TokenType.EOF:
                raise ValueError("Unexpected EOF")
            if token._type not in self.infix_parselets:
                raise ValueError(f"No infix parselet available for {token}")
            print("| " * (depth - 1) + f"parsing infix {token._type}, prec={precedence}")
            infix = self.infix_parselets[token._type]
            expr = infix.parse(stream, self, expr, token)

        print(
            "| " * (depth - 1)
            + f"finished parsing at prec={precedence} since next token {stream.peek()._type} ({repr(stream.peek().value)}) has active_prec={active_precedence(stream.peek())}"
        )
        depth -= 1
        return expr


class Precedence(Enum):
    SEMICOLON = 10
    ASSIGNMENT = 20

    N_ARY_MESSAGE = 50

    SUM_DIFFERENCE = 100
    DIVISION = 110
    PRODUCT = 120

    PREFIX = 500

    UNARY_MESSAGE = 1000


class OperatorPrefixParselet:
    def parse(self, stream: TokenStream, parser: PrattParser, token: Token) -> Expr:
        right = parser.parse(stream, Precedence.PREFIX.value)
        return UnaryOpExpr(span=combine_spans(token.span, right.span), op=token, arg=right)


class MessagePrefixParselet:
    def parse(self, stream: TokenStream, parser: PrattParser, token: Token) -> Expr:
        messages = [token]
        args = [parser.parse(stream, Precedence.N_ARY_MESSAGE.value + 1)]
        while stream.next_has_type(TokenType.MESSAGE):
            messages.append(stream.consume(TokenType.MESSAGE))
            args.append(parser.parse(stream, Precedence.N_ARY_MESSAGE.value + 1))
        return NAryMessageExpr(
            span=combine_spans(*[token.span for token in messages], *[arg.span for arg in args]),
            target=None,
            messages=messages,
            args=args,
        )


class LParenPrefixParselet:
    def parse(self, stream: TokenStream, parser: PrattParser, token: Token) -> Expr:
        inner = parser.parse(stream, precedence=0)
        rparen = stream.consume(TokenType.RPAREN)
        return ParenExpr(span=combine_spans(token.span, inner.span, rparen.span), inner=inner)


class LCurlyPrefixParselet:
    def parse(self, stream: TokenStream, parser: PrattParser, token: Token) -> Expr:
        inner = parser.parse(stream, precedence=0)
        rparen = stream.consume(TokenType.RCURLY)
        return BlockExpr(span=combine_spans(token.span, inner.span, rparen.span), inner=inner)


class NamePrefixParselet:
    def parse(self, stream: TokenStream, parser: PrattParser, token: Token) -> Expr:
        return NameExpr(token.span, token)


class LiteralPrefixParselet:
    def parse(self, stream: TokenStream, parser: PrattParser, token: Token) -> Expr:
        return LiteralExpr(token.span, token)


class NameInfixParselet:
    def parse(self, stream: TokenStream, parser: PrattParser, left: Expr, token: Token) -> Expr:
        return UnaryMessageExpr(
            span=combine_spans(left.span, token.span), target=left, message=token
        )

    def precedence(self, token: Token) -> int:
        return Precedence.UNARY_MESSAGE.value


class MessageInfixParselet:
    def parse(self, stream: TokenStream, parser: PrattParser, left: Expr, token: Token) -> Expr:
        messages = [token]
        args = [parser.parse(stream, Precedence.N_ARY_MESSAGE.value + 1)]
        while stream.next_has_type(TokenType.MESSAGE):
            messages.append(stream.consume(TokenType.MESSAGE))
            args.append(parser.parse(stream, Precedence.N_ARY_MESSAGE.value + 1))
        return NAryMessageExpr(
            span=combine_spans(
                left.span, *[token.span for token in messages], *[arg.span for arg in args]
            ),
            target=left,
            messages=messages,
            args=args,
        )

    def precedence(self, token: Token) -> int:
        return Precedence.N_ARY_MESSAGE.value


class SemicolonInfixParselet:
    def parse(self, stream: TokenStream, parser: PrattParser, left: Expr, token: Token) -> Expr:
        sequence = [left]
        semicolons = [token]

        def parse_next_expr_or_trailing_semicolon():
            # Hack: allow trailing semicolon. Check for a following token that cannot be a prefix.
            token = stream.peek()
            if token and token._type in [TokenType.RPAREN, TokenType.RCURLY, TokenType.EOF]:
                return
            sequence.append(parser.parse(stream, Precedence.SEMICOLON.value + 1))

        parse_next_expr_or_trailing_semicolon()
        while stream.next_has_type(TokenType.SEMICOLON):
            semicolons.append(stream.consume(TokenType.SEMICOLON))
            parse_next_expr_or_trailing_semicolon()
        return SequenceExpr(
            span=combine_spans(
                left.span,
                *[expr.span for expr in sequence],
                *[semicolon.span for semicolon in semicolons],
            ),
            sequence=sequence,
        )

    def precedence(self, token: Token) -> int:
        return Precedence.SEMICOLON.value


class Associativity(Enum):
    LEFT = auto()
    RIGHT = auto()


class OperatorInfixParselet:
    infix_precedence = {
        "=": Precedence.ASSIGNMENT,
        "+": Precedence.SUM_DIFFERENCE,
        "-": Precedence.SUM_DIFFERENCE,
        "*": Precedence.PRODUCT,
        "/": Precedence.DIVISION,
    }
    infix_associativity = {
        "=": Associativity.RIGHT,
        "+": Associativity.LEFT,
        "-": Associativity.LEFT,
        "*": Associativity.LEFT,
        "/": Associativity.LEFT,
    }

    def parse(self, stream: TokenStream, parser: PrattParser, left: Expr, token: Token) -> Expr:
        op_prec = self.infix_precedence[token.value].value
        op_assoc = self.infix_associativity[token.value]
        right = parser.parse(stream, op_prec if op_assoc == Associativity.LEFT else op_prec - 1)
        return BinaryOpExpr(combine_spans(left.span, token.span, right.span), token, left, right)

    def precedence(self, token: Token) -> int:
        return self.infix_precedence[token.value].value


parser = PrattParser()

parser.prefix_parselets[TokenType.OPERATOR] = OperatorPrefixParselet()
parser.prefix_parselets[TokenType.MESSAGE] = MessagePrefixParselet()
parser.prefix_parselets[TokenType.LPAREN] = LParenPrefixParselet()
parser.prefix_parselets[TokenType.LCURLY] = LCurlyPrefixParselet()
parser.prefix_parselets[TokenType.NAME] = NamePrefixParselet()
parser.prefix_parselets[TokenType.STRING] = LiteralPrefixParselet()
parser.prefix_parselets[TokenType.NUMBER] = LiteralPrefixParselet()

parser.infix_parselets[TokenType.NAME] = NameInfixParselet()
parser.infix_parselets[TokenType.MESSAGE] = MessageInfixParselet()
parser.infix_parselets[TokenType.SEMICOLON] = SemicolonInfixParselet()
parser.infix_parselets[TokenType.OPERATOR] = OperatorInfixParselet()

stream = TokenStream(tokens)
while stream.peek()._type != TokenType.EOF:
    top_level_expr = parser.parse(stream)
    print(top_level_expr)

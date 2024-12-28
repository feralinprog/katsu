from dataclasses import dataclass
from enum import Enum, auto
from typing import Optional

from error import ParseError
from lexer import Token, TokenStream, TokenType
from span import SourceSpan, combine_spans


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


class PrattParser:
    # prefix parselets:
    #   .parse(token_stream, pratt_parser, current_token)
    # infix parselets:
    #   .parse(token_stream, pratt_parser, left_expr, current_token)
    #   .precedence(current_token)

    def __init__(self):
        self.prefix_parselets = {}
        self.infix_parselets = {}

        self.should_log = False
        self.depth = 0

    def parse(
        self, stream: TokenStream, precedence: Optional[int] = 0, is_toplevel: bool = False
    ) -> Expr:
        """Parses an expression. Precondition: stream.peek() is not an EOF."""
        self.depth += 1
        token = stream.consume()
        while token._type == TokenType.NEWLINE:
            token = stream.consume()
        if token._type == TokenType.EOF:
            raise ParseError("Unexpected EOF.", token.span)

        if token._type not in self.prefix_parselets:
            raise ParseError(f"No prefix parselet available for {token}.", token.span)
        prefix = self.prefix_parselets[token._type]
        if self.should_log:
            print(
                "| " * (self.depth - 1)
                + f"parsing prefix {token._type}, prec={precedence}, token={repr(token.value)}"
            )
        expr = prefix.parse(stream, self, token)

        def active_precedence(token):
            if token._type in self.infix_parselets:
                return self.infix_parselets[token._type].precedence(token)
            else:
                # raise ValueError(f"No infix parselets available to determine precedence for {token}")
                return 0

        while active_precedence(stream.peek()) > precedence and (
            stream.peek()._type not in [TokenType.SEMICOLON, TokenType.NEWLINE]
            if is_toplevel
            else True
        ):
            token = stream.consume()
            if self.should_log:
                print(
                    "| " * (self.depth - 1)
                    + f"got infix token {token._type}, prec={precedence}, token={repr(token.value)}"
                )
            if token._type == TokenType.EOF:
                raise ParseError("Unexpected EOF.", token.span)
            if token._type not in self.infix_parselets:
                raise ParseError(f"No infix parselet available for {token}.", token.span)
            if self.should_log:
                print("| " * (self.depth - 1) + f"parsing infix {token._type}, prec={precedence}")
            infix = self.infix_parselets[token._type]
            expr = infix.parse(stream, self, expr, token)

        if self.should_log:
            print(
                "| " * (self.depth - 1)
                + f"finished parsing at prec={precedence} since next token {stream.peek()._type} ({repr(stream.peek().value)}) has active_prec={active_precedence(stream.peek())}"
            )
        self.depth -= 1
        return expr


class Precedence(Enum):
    SEQUENCING = 10
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


class SequencingInfixParselet:
    def parse(self, stream: TokenStream, parser: PrattParser, left: Expr, token: Token) -> Expr:
        sequence = [left]
        separators = [token]

        def parse_next_expr_or_trailing_semicolon():
            # Hack: allow trailing semicolon. Check for a following token that cannot be a prefix.
            token = stream.peek()
            if token and token._type in [TokenType.RPAREN, TokenType.RCURLY, TokenType.EOF]:
                return
            sequence.append(parser.parse(stream, Precedence.SEQUENCING.value + 1))

        parse_next_expr_or_trailing_semicolon()
        while stream.next_has_type(TokenType.SEMICOLON) or stream.next_has_type(TokenType.NEWLINE):
            separators.append(stream.consume())
            parse_next_expr_or_trailing_semicolon()
        return SequenceExpr(
            span=combine_spans(
                left.span, *[expr.span for expr in sequence], *[sep.span for sep in separators]
            ),
            sequence=sequence,
        )

    def precedence(self, token: Token) -> int:
        return Precedence.SEQUENCING.value


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
        if not token.value in self.infix_precedence:
            raise ParseError(f"Missing infix precedence for operator '{token.value}'.", token.span)
        if not token.value in self.infix_associativity:
            raise ParseError(
                f"Missing infix associativity for operator '{token.value}'.", token.span
            )
        op_prec = self.infix_precedence[token.value].value
        op_assoc = self.infix_associativity[token.value]
        right = parser.parse(stream, op_prec if op_assoc == Associativity.LEFT else op_prec - 1)
        return BinaryOpExpr(combine_spans(left.span, token.span, right.span), token, left, right)

    def precedence(self, token: Token) -> int:
        if not token.value in self.infix_precedence:
            raise ParseError(f"Missing infix precedence for operator '{token.value}'.", token.span)
        return self.infix_precedence[token.value].value


parser = PrattParser()

parser.prefix_parselets[TokenType.OPERATOR] = OperatorPrefixParselet()
parser.prefix_parselets[TokenType.MESSAGE] = MessagePrefixParselet()
parser.prefix_parselets[TokenType.LPAREN] = LParenPrefixParselet()
parser.prefix_parselets[TokenType.LCURLY] = LCurlyPrefixParselet()
parser.prefix_parselets[TokenType.NAME] = NamePrefixParselet()
parser.prefix_parselets[TokenType.STRING] = LiteralPrefixParselet()
parser.prefix_parselets[TokenType.NUMBER] = LiteralPrefixParselet()
parser.prefix_parselets[TokenType.SYMBOL] = LiteralPrefixParselet()

parser.infix_parselets[TokenType.NAME] = NameInfixParselet()
parser.infix_parselets[TokenType.MESSAGE] = MessageInfixParselet()
parser.infix_parselets[TokenType.SEMICOLON] = SequencingInfixParselet()
parser.infix_parselets[TokenType.NEWLINE] = SequencingInfixParselet()
parser.infix_parselets[TokenType.OPERATOR] = OperatorInfixParselet()

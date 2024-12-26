from span import SourceSpan


class ParseError(Exception):
    def __init__(self, message, span: SourceSpan):
        super().__init__(message)
        self.span = span

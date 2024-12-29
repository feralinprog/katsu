from span import SourceSpan


class ParseError(Exception):
    def __init__(self, message, span: SourceSpan):
        super().__init__(message)
        self.span = span


class RunError(Exception):
    def __init__(self, message, runtime_state: "RuntimeState"):
        super().__init__(message)
        self.runtime_state = runtime_state

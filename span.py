from dataclasses import dataclass


@dataclass(kw_only=True)
class SourceFile:
    # filename, or could be a representation of e.g. internal source or REPL UI
    source_path: str
    # Reference to full source (don't make a copy!)
    source: str

    def __str__(self):
        return f"<{self.source_path}>"

    def __repr__(self):
        return f"SourceFile(source_path={repr(self.source_path)}, source=...)"

    def __hash__(self) -> int:
        return hash((self.source_path, self.source))


@dataclass(kw_only=True)
class SourceLocation:
    # index of character, just incrementing from 0 to end of file
    index: int
    # line / column
    line: int
    column: int

    def __str__(self):
        return f"{self.line+1}:{self.column+1}"


@dataclass
class SourceSpan:
    file: SourceFile
    # inclusive
    start: SourceLocation
    # exclusive
    end: SourceLocation

    def __str__(self):
        return f"<{self.file.source_path}> ({self.start} to {self.end})"


def combine_spans(*spans: list[SourceSpan]) -> SourceSpan:
    """Determine the minimal span containing each span in the input list."""
    if not spans:
        raise ValueError("Need at least one input span.")
    files = set(span.file for span in spans)
    if len(files) != 1:
        raise ValueError(f"Input spans must all come from a single .file; got {files}")
    return SourceSpan(
        file=next(iter(files)),
        start=min([span.start for span in spans], key=lambda loc: loc.index),
        end=max([span.end for span in spans], key=lambda loc: loc.index),
    )

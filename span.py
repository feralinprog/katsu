from dataclasses import dataclass


@dataclass
class SourceLocation:
    # filename, or could be a representation of e.g. internal source or REPL UI
    source_path: str
    # Reference to full source (don't make a copy!)
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

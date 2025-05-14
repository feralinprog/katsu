#include "span.h"

#include <stdexcept>

namespace Katsu
{
    bool operator==(const SourceFile& a, const SourceFile& b)
    {
        return a.path == b.path && a.source == b.source;
    }
    bool operator!=(const SourceFile& a, const SourceFile& b)
    {
        return !(a == b);
    }

    SourceSpan SourceSpan::combine(const std::vector<SourceSpan>& spans)
    {
        if (spans.empty()) {
            throw std::invalid_argument("spans must be nonempty");
        }
        const SourceFile file = spans[0].file;
        for (const SourceSpan& span : spans) {
            if (span.file != file) {
                throw std::invalid_argument("all spans must have the same .file");
            }
        }

        SourceLocation min = spans[0].start;
        SourceLocation max = spans[0].end;
        for (const SourceSpan& span : spans) {
            if (span.start.index < min.index) {
                min = span.start;
            }
            if (span.end.index > max.index) {
                max = span.end;
            }
        }

        return SourceSpan{.file = file, .start = min, .end = max};
    }

    bool operator==(const SourceLocation& a, const SourceLocation& b)
    {
        return a.index == b.index && a.line == b.line && a.column == b.column;
    }
    bool operator!=(const SourceLocation& a, const SourceLocation& b)
    {
        return !(a == b);
    }
};

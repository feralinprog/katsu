#include "span.h"

#include "assertions.h"

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
        ASSERT_ARG(!spans.empty());
        const SourceFile file = spans[0].file;
#if DEBUG_ASSERTIONS
        for (const SourceSpan& span : spans) {
            ASSERT_ARG_MSG(span.file == file, "all spans must have the same .file");
        }
#endif

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

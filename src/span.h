#pragma once

#include <string>
#include <memory>
#include <vector>

namespace Katsu
{
    struct SourceFile
    {
        // Filepath, or otherwise some representation of e.g. internal source or REPL UI.
        std::shared_ptr<const std::string> path;
        // Full file contents.
        std::shared_ptr<const std::string> source;
    };

    bool operator==(const SourceFile& a, const SourceFile& b);
    bool operator!=(const SourceFile& a, const SourceFile& b);

    struct SourceLocation
    {
        // Index from 0 to end of file.
        int index;
        // Line/column indices, also 0-based.
        int line;
        int column;
    };

    // Represents a contiguous portion of a SourceFile as a location interval [start, end).
    struct SourceSpan
    {
        SourceFile file;
        // Inclusive start position.
        SourceLocation start;
        // Exclusive end position.
        SourceLocation end;

        // Determines the minimal span combining each span in the input list.
        // All the input spans must have the same `file`.
        static SourceSpan combine(const std::vector<SourceSpan>& spans);
    };
};

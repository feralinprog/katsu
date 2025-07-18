#pragma once

#include <stdexcept>

namespace Katsu
{
    // An exception which is bubbled up to Katsu as a signaled condition.
    class condition_error : public std::runtime_error
    {
    public:
        condition_error(const std::string& _condition, const std::string& message)
            : std::runtime_error(message)
            , condition(_condition)
        {}

        const std::string condition;
    };

    class terminate_error : public std::runtime_error
    {
    public:
        terminate_error(const std::string& message)
            : std::runtime_error(message)
        {}
    };
};

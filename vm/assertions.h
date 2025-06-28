#pragma once

#ifndef DEBUG_ASSERTIONS
#define DEBUG_ASSERTIONS (1)
#endif

#define STRINGIZE_EXPANDED(x) #x
#define STRINGIZE(x) STRINGIZE_EXPANDED(x)

// Raise an exception if the condition is not met. Append an extra message.
#define ALWAYS_ASSERT_EXC_MSG(condition, exc, extra_message)                               \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            throw exc("assertion failed at " __FILE__                                      \
                      ":" STRINGIZE(__LINE__) "\nASSERT(" #condition ")\n" extra_message); \
        }                                                                                  \
    } while (0)
// Raise an exception if the condition is not met.
#define ALWAYS_ASSERT_EXC(condition, exc)                                  \
    do {                                                                   \
        if (!(condition)) {                                                \
            throw exc("assertion failed at " __FILE__                      \
                      ":" STRINGIZE(__LINE__) "\nASSERT(" #condition ")"); \
        }                                                                  \
    } while (0)

// Raise a std::logic_error if the condition is not met. Append an extra message.
#define ALWAYS_ASSERT_MSG(condition, extra_message) \
    ALWAYS_ASSERT_EXC_MSG(condition, std::logic_error, extra_message)
// Raise a std::logic_error if the condition is not met.
#define ALWAYS_ASSERT(condition) ALWAYS_ASSERT_EXC(condition, std::logic_error)

// Raise a std::invalid_argument if the condition is not met. Append an extra message.
#define ALWAYS_ASSERT_ARG_MSG(condition, extra_message) \
    ALWAYS_ASSERT_EXC_MSG(condition, std::invalid_argument, extra_message)
// Raise a std::invalid_argument if the condition is not met.
#define ALWAYS_ASSERT_ARG(condition) ALWAYS_ASSERT_EXC(condition, std::invalid_argument)


#if DEBUG_ASSERTIONS

// Raise an exception if the condition is not met. Append an extra message.
#define ASSERT_EXC_MSG(condition, exc, extra_message) \
    ALWAYS_ASSERT_EXC_MSG(condition, exc, extra_message)
// Raise an exception if the condition is not met.
#define ASSERT_EXC(condition, exc) ALWAYS_ASSERT_EXC(condition, exc)

// Raise a std::logic_error if the condition is not met. Append an extra message.
#define ASSERT_MSG(condition, extra_message) ALWAYS_ASSERT_MSG(condition, extra_message)
// Raise a std::logic_error if the condition is not met.
#define ASSERT(condition) ALWAYS_ASSERT(condition)

// Raise a std::invalid_argument if the condition is not met. Append an extra message.
#define ASSERT_ARG_MSG(condition, extra_message) ALWAYS_ASSERT_ARG_MSG(condition, extra_message)
// Raise a std::invalid_argument if the condition is not met.
#define ASSERT_ARG(condition) ALWAYS_ASSERT_ARG(condition)

#else /* !DEBUG_ASSERTIONS */

#define DO_NOTHING() \
    do {             \
    } while (0)

// Do nothing!
#define ASSERT_EXC_MSG(condition, exc, extra_message) DO_NOTHING()
// Do nothing!
#define ASSERT_EXC(condition, exc) DO_NOTHING()

// Do nothing!
#define ASSERT_MSG(condition, extra_message) DO_NOTHING()
// Do nothing!
#define ASSERT(condition) DO_NOTHING()

// Do nothing!
#define ASSERT_ARG_MSG(condition, extra_message) DO_NOTHING()
// Do nothing!
#define ASSERT_ARG(condition) DO_NOTHING()

#endif /* DEBUG_ASSERTIONS */

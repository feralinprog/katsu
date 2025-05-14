#include <catch2/catch_test_macros.hpp>

#include "lexer.h"

using namespace Katsu;

TEST_CASE("lexer smoketest", "[lexer]")
{
    SourceFile source{.path = std::make_shared<std::string>("path"),
                      .source = std::make_shared<std::string>("123")};
    Lexer lexer(source);
    Token t;
    {
        REQUIRE_NOTHROW(t = lexer.next());
        CHECK(t.span.file == source);
        CHECK(t.span.start == SourceLocation{.index = 0, .line = 0, .column = 0});
        CHECK(t.span.end == SourceLocation{.index = 3, .line = 0, .column = 3});
        CHECK(t.type == TokenType::INTEGER);
        REQUIRE(std::get_if<long long>(&t.value));
        CHECK(std::get<long long>(t.value) == 123);
    }
    for (int i = 0; i < 2; i++) {
        REQUIRE_NOTHROW(t = lexer.next());
        CHECK(t.span.file == source);
        CHECK(t.span.start == SourceLocation{.index = 3, .line = 0, .column = 3});
        CHECK(t.span.end == SourceLocation{.index = 3, .line = 0, .column = 3});
        CHECK(t.type == TokenType::END);
        REQUIRE(std::get_if<std::monostate>(&t.value));
    }
}

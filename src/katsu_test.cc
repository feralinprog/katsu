#include <catch2/catch_test_macros.hpp>
#include "katsu.h"

TEST_CASE("thing does a thing", "")
{
    REQUIRE(thing(5) == 10);
}
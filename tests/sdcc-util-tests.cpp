#include <gtest/gtest.h>
#include <sdcc/util.h>

#include <regex>
#include <string>
#include <string_view>

TEST(SdccUtilTest, MatchSupportsSubstringViews) {
    const std::string storage = "prefix:FOO:123:suffix";
    const std::string_view line(storage.data() + 7, 7);

    const auto groups = sdcc::util::match(
        line, std::regex(R"(([A-Z]+):([0-9]+))"));

    ASSERT_TRUE(groups.has_value());
    ASSERT_EQ(groups->size(), 2u);
    EXPECT_EQ((*groups)[0], "FOO");
    EXPECT_EQ((*groups)[1], "123");
}

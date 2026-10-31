#include "shop/ShopSign.h"

#include <array>
#include <doctest/doctest.h>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace ss::shop;

std::array<std::string_view, 4>
lines(std::string_view owner, std::string_view amount, std::string_view price, std::string_view item) {
    return {owner, amount, price, item};
}

ShopSignConfig defaultConfig() {
    ShopSignConfig config;
    config.maxShopAmount               = 3456;
    config.adminShopName               = "Admin Shop";
    config.blockSellPriceHigherThanBuy = true;
    return config;
}

} // namespace

TEST_CASE("份量行：合法整数") {
    CHECK(parseAmountLine("1", 3456) == std::optional<int>{1});
    CHECK(parseAmountLine("64", 3456) == std::optional<int>{64});
    CHECK(parseAmountLine("3456", 3456) == std::optional<int>{3456});
}

TEST_CASE("份量行：非法输入全部拒绝") {
    CHECK_FALSE(parseAmountLine("0", 3456).has_value());
    CHECK_FALSE(parseAmountLine("-1", 3456).has_value());
    CHECK_FALSE(parseAmountLine("3457", 3456).has_value());
    CHECK_FALSE(parseAmountLine("", 3456).has_value());
    CHECK_FALSE(parseAmountLine("abc", 3456).has_value());
    CHECK_FALSE(parseAmountLine("6 4", 3456).has_value());
    CHECK_FALSE(parseAmountLine(" 64", 3456).has_value());
    CHECK_FALSE(parseAmountLine("64 ", 3456).has_value());
    CHECK_FALSE(parseAmountLine("64.0", 3456).has_value());
    CHECK_FALSE(parseAmountLine("Q 64 : C 64", 3456).has_value());
}

TEST_CASE("价格行：单边与双边") {
    auto buyOnly = parsePriceLine("B 10");
    REQUIRE(buyOnly.has_value());
    CHECK(buyOnly->buy == std::optional<std::int64_t>{10});
    CHECK_FALSE(buyOnly->sell.has_value());

    auto sellOnly = parsePriceLine("S 5");
    REQUIRE(sellOnly.has_value());
    CHECK_FALSE(sellOnly->buy.has_value());
    CHECK(sellOnly->sell == std::optional<std::int64_t>{5});

    auto both = parsePriceLine("B 10 : S 5");
    REQUIRE(both.has_value());
    CHECK(both->buy == std::optional<std::int64_t>{10});
    CHECK(both->sell == std::optional<std::int64_t>{5});
}

TEST_CASE("价格行：缺省方向按 B 处理") {
    auto bare = parsePriceLine("10");
    REQUIRE(bare.has_value());
    CHECK(bare->buy == std::optional<std::int64_t>{10});
    CHECK_FALSE(bare->sell.has_value());

    auto suffix = parsePriceLine("10 B");
    REQUIRE(suffix.has_value());
    CHECK(suffix->buy == std::optional<std::int64_t>{10});
    CHECK_FALSE(suffix->sell.has_value());

    auto sellSuffix = parsePriceLine("5 S");
    REQUIRE(sellSuffix.has_value());
    CHECK_FALSE(sellSuffix->buy.has_value());
    CHECK(sellSuffix->sell == std::optional<std::int64_t>{5});
}

TEST_CASE("价格行：free 与零价") {
    auto freeOnly = parsePriceLine("free");
    REQUIRE(freeOnly.has_value());
    CHECK(freeOnly->buy == std::optional<std::int64_t>{0});
    CHECK_FALSE(freeOnly->sell.has_value());

    auto freeBuy = parsePriceLine("B free");
    REQUIRE(freeBuy.has_value());
    CHECK(freeBuy->buy == std::optional<std::int64_t>{0});

    auto zero = parsePriceLine("B 0 : S 0");
    REQUIRE(zero.has_value());
    CHECK(zero->buy == std::optional<std::int64_t>{0});
    CHECK(zero->sell == std::optional<std::int64_t>{0});
}

TEST_CASE("价格行：K/M 后缀") {
    auto thousand = parsePriceLine("B 10K");
    REQUIRE(thousand.has_value());
    CHECK(thousand->buy == std::optional<std::int64_t>{10000});

    auto million = parsePriceLine("S 2M");
    REQUIRE(million.has_value());
    CHECK(million->sell == std::optional<std::int64_t>{2000000});

    auto lower = parsePriceLine("B 1k");
    REQUIRE(lower.has_value());
    CHECK(lower->buy == std::optional<std::int64_t>{1000});
}

TEST_CASE("价格行：小数向下取整") {
    auto decimal = parsePriceLine("B 1.5");
    REQUIRE(decimal.has_value());
    CHECK(decimal->buy == std::optional<std::int64_t>{1});

    auto trailing = parsePriceLine("B 2.999");
    REQUIRE(trailing.has_value());
    CHECK(trailing->buy == std::optional<std::int64_t>{2});

    auto scaled = parsePriceLine("S 1.2345K");
    REQUIRE(scaled.has_value());
    CHECK(scaled->sell == std::optional<std::int64_t>{1234});

    auto scaledUp = parsePriceLine("B 1.5K");
    REQUIRE(scaledUp.has_value());
    CHECK(scaledUp->buy == std::optional<std::int64_t>{1500});
}

TEST_CASE("价格行：负数与格式错误被拒绝") {
    CHECK_FALSE(parsePriceLine("B -5").has_value());
    CHECK_FALSE(parsePriceLine("").has_value());
    CHECK_FALSE(parsePriceLine("B").has_value());
    CHECK_FALSE(parsePriceLine("X 10").has_value());
    CHECK_FALSE(parsePriceLine("B10").has_value());
    CHECK_FALSE(parsePriceLine("10.5.5").has_value());
    CHECK_FALSE(parsePriceLine("9999999999999999999").has_value());
}

TEST_CASE("价格行：缺冒号与多余冒号") {
    // 两段之间没有冒号：方向重复了，必须拒绝
    CHECK_FALSE(parsePriceLine("B 10 S 5").has_value());
    CHECK_FALSE(parsePriceLine("B 10 :").has_value());
    CHECK_FALSE(parsePriceLine(": B 10").has_value());
    CHECK_FALSE(parsePriceLine("B 10 : S 5 : S 1").has_value());
    CHECK_FALSE(parsePriceLine("B 10 : 5").has_value());
    CHECK_FALSE(parsePriceLine("B 10 : B 5").has_value());
    CHECK_FALSE(parsePriceLine("S 10 : S 5").has_value());
}

TEST_CASE("份量行：颜色码被忽略") {
    CHECK(parseAmountLine("§a64", 3456) == std::optional<int>{64});
    CHECK_FALSE(parseAmountLine("§a", 3456).has_value());
}

TEST_CASE("价格行：单独的零") {
    auto zero = parsePriceLine("0");
    REQUIRE(zero.has_value());
    CHECK(zero->buy == std::optional<std::int64_t>{0});
    CHECK_FALSE(zero->sell.has_value());
}

TEST_CASE("价格行：int64 边界与溢出") {
    // 恰好 int64 上限：合法
    auto max = parsePriceLine("9223372036854775807");
    REQUIRE(max.has_value());
    CHECK(max->buy == std::optional<std::int64_t>{std::numeric_limits<std::int64_t>::max()});

    // 整数部分没越界，加上小数部分后越界：必须拒绝，不能变成负数
    CHECK_FALSE(parsePriceLine("B 9223372036854775.999K").has_value());
    CHECK_FALSE(parsePriceLine("B 9223372036854.999M").has_value());
    CHECK_FALSE(parsePriceLine("B 9223372036854775808").has_value());
}

TEST_CASE("管理员商店判定") {
    CHECK(isAdminShop("Admin Shop", "Admin Shop", true));
    CHECK_FALSE(isAdminShop("Admin Shop", "Admin Shop", false));
    CHECK_FALSE(isAdminShop("Steve", "Admin Shop", true));
}

TEST_CASE("四行解析：合法商店") {
    auto result = parseShopSign(lines("Steve", "64", "B 10", "?"), false, defaultConfig());
    REQUIRE(result.spec.has_value());
    CHECK(result.error == ShopSignError::None);
    CHECK(result.spec->amount == 64);
    CHECK(result.spec->buyPrice == std::optional<std::int64_t>{10});
    CHECK_FALSE(result.spec->sellPrice.has_value());
    CHECK_FALSE(result.spec->adminShop);
}

TEST_CASE("四行解析：不是商店的告示牌直接放行") {
    auto result = parseShopSign(lines("Steve", "64", "欢迎光临", "?"), false, defaultConfig());
    CHECK_FALSE(result.spec.has_value());
    CHECK(result.error == ShopSignError::NotAShopSign);
    CHECK_FALSE(result.looksLikeShopSign());
}

TEST_CASE("四行解析：含 B/S 但非法时仍然是「像商店」，牌子必须保留") {
    // 普通告示牌写 “SOLD OUT” 也会被当成候选，但解析失败；调用方只提示、不删牌
    auto result = parseShopSign(lines("Steve", "64", "SOLD OUT", "?"), false, defaultConfig());
    CHECK_FALSE(result.spec.has_value());
    CHECK(result.error == ShopSignError::BadPrice);
    CHECK(result.looksLikeShopSign());
}

TEST_CASE("四行解析：第 2 行带颜色码仍然能解析") {
    auto result = parseShopSign(lines("Steve", "§a64", "§6B 10", "?"), false, defaultConfig());
    REQUIRE(result.spec.has_value());
    CHECK(result.spec->amount == 64);
    CHECK(result.spec->buyPrice == std::optional<std::int64_t>{10});
}

TEST_CASE("四行解析：像商店但第一行为空") {
    auto result = parseShopSign(lines("", "64", "B 10", "?"), false, defaultConfig());
    CHECK_FALSE(result.spec.has_value());
    CHECK(result.error == ShopSignError::EmptyOwnerName);

    auto colored = parseShopSign(lines("§r", "64", "B 10", "?"), false, defaultConfig());
    CHECK(colored.error == ShopSignError::EmptyOwnerName);
}

TEST_CASE("四行解析：份量非法") {
    auto result = parseShopSign(lines("Steve", "0", "B 10", "?"), false, defaultConfig());
    CHECK_FALSE(result.spec.has_value());
    CHECK(result.error == ShopSignError::BadAmount);

    auto config          = defaultConfig();
    config.maxShopAmount = 10;
    auto over            = parseShopSign(lines("Steve", "11", "B 10", "?"), false, config);
    CHECK(over.error == ShopSignError::BadAmount);
}

TEST_CASE("四行解析：价格非法") {
    auto result = parseShopSign(lines("Steve", "64", "B abc", "?"), false, defaultConfig());
    CHECK_FALSE(result.spec.has_value());
    CHECK(result.error == ShopSignError::BadPrice);
}

TEST_CASE("四行解析：B 价低于 S 价按配置拒绝") {
    auto reject = parseShopSign(lines("Steve", "64", "B 5 : S 10", "?"), false, defaultConfig());
    CHECK_FALSE(reject.spec.has_value());
    CHECK(reject.error == ShopSignError::SellPriceAboveBuy);

    auto config                        = defaultConfig();
    config.blockSellPriceHigherThanBuy = false;
    auto accept                        = parseShopSign(lines("Steve", "64", "B 5 : S 10", "?"), false, config);
    REQUIRE(accept.spec.has_value());
    CHECK(accept.spec->buyPrice == std::optional<std::int64_t>{5});
    CHECK(accept.spec->sellPrice == std::optional<std::int64_t>{10});

    auto equal = parseShopSign(lines("Steve", "64", "B 5 : S 5", "?"), false, defaultConfig());
    CHECK(equal.spec.has_value());
}

TEST_CASE("四行解析：管理员商店") {
    auto op = parseShopSign(lines("Admin Shop", "64", "B 10", "?"), true, defaultConfig());
    REQUIRE(op.spec.has_value());
    CHECK(op.spec->adminShop);

    auto notOp = parseShopSign(lines("Admin Shop", "64", "B 10", "?"), false, defaultConfig());
    REQUIRE(notOp.spec.has_value());
    CHECK_FALSE(notOp.spec->adminShop);
}

TEST_CASE("规范化：四行拼回") {
    ShopSpec spec;
    spec.amount    = 64;
    spec.buyPrice  = 10;
    spec.sellPrice = 5;

    auto formatted = formatShopSign(spec, "Steve", "Stone");
    CHECK(formatted[0] == "Steve");
    CHECK(formatted[1] == "64");
    CHECK(formatted[2] == "B 10 : S 5");
    CHECK(formatted[3] == "Stone");
    CHECK(joinSignText(formatted) == "Steve\n64\nB 10 : S 5\nStone");
    // 告示牌编辑包用字面两字符 \n 分隔
    CHECK(joinSignText(formatted, "\\n") == "Steve\\n64\\nB 10 : S 5\\nStone");
}

TEST_CASE("规范化：缺省的方向省略") {
    ShopSpec buyOnly;
    buyOnly.amount   = 64;
    buyOnly.buyPrice = 10;
    CHECK(formatShopSign(buyOnly, "Steve", "Stone")[2] == "B 10");

    ShopSpec sellOnly;
    sellOnly.amount    = 64;
    sellOnly.sellPrice = 5;
    CHECK(formatShopSign(sellOnly, "Steve", "Stone")[2] == "S 5");

    ShopSpec freeShop;
    freeShop.amount   = 1;
    freeShop.buyPrice = 0;
    CHECK(formatShopSign(freeShop, "Steve", "Stone")[2] == "B 0");
}

TEST_CASE("规范化后的四行能被重新解析") {
    ShopSpec spec;
    spec.amount    = 64;
    spec.buyPrice  = 250;
    spec.sellPrice = 100;

    auto                            formatted = formatShopSign(spec, "Steve", "Stone");
    std::array<std::string_view, 4> reparsedLines{formatted[0], formatted[1], formatted[2], formatted[3]};

    auto result = parseShopSign(reparsedLines, false, defaultConfig());
    REQUIRE(result.spec.has_value());
    CHECK(result.spec->amount == 64);
    CHECK(result.spec->buyPrice == std::optional<std::int64_t>{250});
    CHECK(result.spec->sellPrice == std::optional<std::int64_t>{100});
}

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
    // 基岩版的告示牌文本就是真换行符分隔四行，没有第二种写法
    CHECK(joinSignText(formatted, "|") == "Steve|64|B 10 : S 5|Stone");
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

TEST_CASE("牌面文本：按真实换行符切分") {
    auto lines = splitSignText("Steve\n64\nB 10 : S 5\nStone");
    CHECK(lines[0] == "Steve");
    CHECK(lines[1] == "64");
    CHECK(lines[2] == "B 10 : S 5");
    CHECK(lines[3] == "Stone");
}

TEST_CASE("牌面文本：不足四行补空，多余行丢弃") {
    auto blank = splitSignText("");
    CHECK(blank[0].empty());
    CHECK(blank[1].empty());
    CHECK(blank[2].empty());
    CHECK(blank[3].empty());

    auto twoLines = splitSignText("Steve\n64");
    CHECK(twoLines[0] == "Steve");
    CHECK(twoLines[1] == "64");
    CHECK(twoLines[2].empty());
    CHECK(twoLines[3].empty());

    auto fiveLines = splitSignText("a\nb\nc\nd\ne");
    CHECK(fiveLines[3] == "d");
}

TEST_CASE("牌面文本：切分与拼接互逆") {
    std::array<std::string, 4> original{"Steve", "64", "B 10 : S 5", "Stone"};
    auto                       reparsed = splitSignText(joinSignText(original));
    CHECK(reparsed == original);
}

TEST_CASE("整段文本解析：等价于先切分再解析") {
    auto const fromText = parseShopSignText("Steve\n64\nB 10 : S 5\nStone", false, defaultConfig());
    REQUIRE(fromText.ok());
    CHECK(fromText.spec->amount == 64);
    CHECK(fromText.spec->buyPrice == std::optional<std::int64_t>{10});
    CHECK(fromText.spec->sellPrice == std::optional<std::int64_t>{5});

    auto const blank = parseShopSignText("", false, defaultConfig());
    CHECK_FALSE(blank.ok());
    CHECK_FALSE(blank.looksLikeShopSign());
}

TEST_CASE("价格行：末尾的 x 段表示停业（先剥离最后一段，再按原文法解）") {
    auto both = parsePriceLine("B 10 : S 5 :x");
    REQUIRE(both.has_value());
    CHECK(both->buy == std::optional<std::int64_t>{10});
    CHECK(both->sell == std::optional<std::int64_t>{5});
    CHECK(both->closed);

    // 单侧价格时 `:x` 就是第二段——把它当「第三段」实现就会在这里被 `segments.size() > 2` 误杀
    auto sellOnly = parsePriceLine("S 5 :x");
    REQUIRE(sellOnly.has_value());
    CHECK_FALSE(sellOnly->buy.has_value());
    CHECK(sellOnly->sell == std::optional<std::int64_t>{5});
    CHECK(sellOnly->closed);

    // 冒号两边不留空白也认（`B 10:x`，与 `B 10K` 同一种「贴着写」）
    auto glued = parsePriceLine("B 10:x");
    REQUIRE(glued.has_value());
    CHECK(glued->buy == std::optional<std::int64_t>{10});
    CHECK(glued->closed);

    // 大小写与空白都认：与 B/S、K/M 同一条口径
    auto upper = parsePriceLine("B 10 : S 5 : X");
    REQUIRE(upper.has_value());
    CHECK(upper->closed);

    auto spaced = parsePriceLine("B 10 : S 5 :  x  ");
    REQUIRE(spaced.has_value());
    CHECK(spaced->closed);

    // 省略方向的那一段也能带停业标记
    auto bare = parsePriceLine("10 :x");
    REQUIRE(bare.has_value());
    CHECK(bare->buy == std::optional<std::int64_t>{10});
    CHECK(bare->closed);
}

TEST_CASE("价格行：营业中的店 closed 为假") {
    auto open = parsePriceLine("B 10 : S 5");
    REQUIRE(open.has_value());
    CHECK_FALSE(open->closed);

    auto single = parsePriceLine("B 10");
    REQUIRE(single.has_value());
    CHECK_FALSE(single->closed);
}

TEST_CASE("价格行：只有 x 没有价按非法处理") {
    CHECK_FALSE(parsePriceLine("x").has_value());
    CHECK_FALSE(parsePriceLine("X").has_value());
    CHECK_FALSE(parsePriceLine(":x").has_value());
    CHECK_FALSE(parsePriceLine(": x").has_value());
    // x 只认最后一段：写在前面就是别的东西
    CHECK_FALSE(parsePriceLine("x : B 10").has_value());
    // 只允许一个标记
    CHECK_FALSE(parsePriceLine("B 10 :x :x").has_value());
}

TEST_CASE("价格行：带 :x 的价格行仍然要过原来的文法") {
    CHECK_FALSE(parsePriceLine("B 10 : S 5 : S 1 :x").has_value());
    CHECK_FALSE(parsePriceLine("S 10 : S 5 :x").has_value());
    CHECK_FALSE(parsePriceLine("B 10 : 5 :x").has_value());
    CHECK_FALSE(parsePriceLine("B :x").has_value());
    CHECK_FALSE(parsePriceLine("B -5 :x").has_value());
    CHECK_FALSE(parsePriceLine("B 10 : :x").has_value());
}

TEST_CASE("规范化：停业追加 :x，营业不写") {
    ShopSpec spec;
    spec.amount    = 64;
    spec.buyPrice  = 10;
    spec.sellPrice = 5;

    auto const open = formatShopSign(spec, "Steve", "钻石");
    CHECK(open[SignLine::PriceLine] == "B 10 : S 5");

    spec.closed = true;
    auto const closed = formatShopSign(spec, "Steve", "钻石");
    CHECK(closed[SignLine::PriceLine] == "B 10 : S 5 :x");

    // 单侧价格也只在末尾追加一个段，不会写成 `S 5 : :x`
    spec.buyPrice.reset();
    auto const sellOnly = formatShopSign(spec, "Steve", "钻石");
    CHECK(sellOnly[SignLine::PriceLine] == "S 5 :x");
}

TEST_CASE("规范化与解析：:x 来回一趟不丢") {
    ShopSpec spec;
    spec.amount    = 64;
    spec.buyPrice  = 10;
    spec.sellPrice = 5;
    spec.closed    = true;

    auto const formatted = formatShopSign(spec, "Steve", "钻石");
    auto const parsed    = parseShopSignText(joinSignText(formatted), false, defaultConfig());
    REQUIRE(parsed.ok());
    CHECK(parsed.spec->closed);
    CHECK(parsed.spec->buyPrice == spec.buyPrice);
    CHECK(parsed.spec->sellPrice == spec.sellPrice);
    CHECK(parsed.spec->amount == spec.amount);
    CHECK(formatShopSign(*parsed.spec, "Steve", "钻石")[SignLine::PriceLine] == "B 10 : S 5 :x");
}

TEST_CASE("四行解析：手写 :x 也是合法写法，规范化后留着") {
    auto const result = parseShopSign(lines("Steve", "64", "B 10 : S 5 :x", "?"), false, defaultConfig());
    REQUIRE(result.ok());
    CHECK(result.spec->closed);
    CHECK(formatShopSign(*result.spec, "Steve", "钻石")[SignLine::PriceLine] == "B 10 : S 5 :x");
}

TEST_CASE("四行解析：停业的牌仍然「像商店牌」") {
    // 停业不许靠清空价格行实现：第 3 行没有 B/S 就整体失去保护（spec D1）
    auto const closed = parseShopSign(lines("Steve", "64", "B 10 : S 5 :x", "?"), false, defaultConfig());
    CHECK(closed.looksLikeShopSign());
    CHECK(closed.ok());

    // 停业标记写错：像商店牌，仍要拦下改牌并给原因
    auto const broken = parseShopSign(lines("Steve", "64", "B 10 :x :x", "?"), false, defaultConfig());
    CHECK(broken.looksLikeShopSign());
    CHECK(broken.error == ShopSignError::BadPrice);
}

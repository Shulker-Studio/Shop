#include "shop/ManageForm.h"

#include <doctest/doctest.h>
#include <string>

namespace {

using namespace ss::shop;

/// 一个典型的普通商店定义：钻石 64 个一份，买入 10、卖出 5。
ShopSpec sampleSpec() {
    ShopSpec spec;
    spec.amount    = 64;
    spec.buyPrice  = 10;
    spec.sellPrice = 5;
    return spec;
}

ShopInfoData sampleShop() {
    ShopInfoData data;
    data.ownerName      = "Alex";
    data.itemName       = "钻石";
    data.amountPerUnit  = 64;
    data.buyPrice       = 10;
    data.sellPrice      = 5;
    data.stockUnits     = 3;
    data.balance        = 100;
    data.currencySymbol = "$";
    return data;
}

bool mentions(std::string const& text, std::string const& needle) { return text.find(needle) != std::string::npos; }

} // namespace

TEST_CASE("改买入价：只换买入价，其余字段原样带过去") {
    auto const result = withBuyPrice(sampleSpec(), "20", ShopSignConfig{});

    REQUIRE(result.ok());
    CHECK(result.spec->buyPrice == 20);
    CHECK(result.spec->sellPrice == 5);
    CHECK(result.spec->amount == 64);
}

TEST_CASE("改买入价：留空 = 这一侧不做生意") {
    auto const result = withBuyPrice(sampleSpec(), "", ShopSignConfig{});

    REQUIRE(result.ok());
    CHECK_FALSE(result.spec->buyPrice.has_value());
    CHECK(result.spec->sellPrice == 5);
}

TEST_CASE("改买入价：只写空白也算留空") {
    auto const result = withBuyPrice(sampleSpec(), "   ", ShopSignConfig{});

    REQUIRE(result.ok());
    CHECK_FALSE(result.spec->buyPrice.has_value());
}

TEST_CASE("改买入价：输入两端空白不影响取值") {
    auto const result = withBuyPrice(sampleSpec(), " 20 ", ShopSignConfig{});

    REQUIRE(result.ok());
    CHECK(result.spec->buyPrice == 20);
}

TEST_CASE("改买入价：0 是合法的价格（免费），不是「没填」") {
    auto spec      = sampleSpec();
    spec.sellPrice = 0; // 两边都是 0：免费收又免费送

    auto const result = withBuyPrice(spec, "0", ShopSignConfig{});

    REQUIRE(result.ok());
    REQUIRE(result.spec->buyPrice.has_value());
    CHECK(*result.spec->buyPrice == 0);
}

TEST_CASE("改买入价：牌面写法与带小数 / 单位 / 负号的输入一律不收") {
    // 方向由「哪个输入框」表达，所以这里不认 B / S 前缀；金额是整数，不认小数与 K/M。
    for (auto const* input : {"B 10", "S 5", "free", "1.5", "1K", "1.5K", "-1", "abc", "1 0", "0x10", "10,000"}) {
        auto const result = withBuyPrice(sampleSpec(), input, ShopSignConfig{});
        CHECK_MESSAGE(!result.ok(), "这个输入不该被当成价格：" << input);
        CHECK_FALSE(result.error.empty());
    }
}

TEST_CASE("改买入价：超出 int64 的整数算非法，不截断") {
    auto const result = withBuyPrice(sampleSpec(), "99999999999999999999", ShopSignConfig{});

    CHECK_FALSE(result.ok());
    CHECK(mentions(result.error, "非负整数"));
}

TEST_CASE("改买入价：两侧都清空要拒绝（否则这块牌不再是商店牌）") {
    ShopSpec spec;
    spec.amount   = 64;
    spec.buyPrice = 10; // 只有买入价的店：把它留空就什么都不剩了

    auto const result = withBuyPrice(spec, "", ShopSignConfig{});
    CHECK_FALSE(result.ok());
    CHECK(mentions(result.error, "至少要留一侧价格"));
    CHECK(mentions(result.error, "失去保护"));
}

TEST_CASE("改买入价：留空不会把本来就空的那一侧算进去") {
    ShopSpec spec;
    spec.amount    = 64;
    spec.sellPrice = 5; // 只收购的店，本来就没有买入价

    auto const result = withBuyPrice(spec, "", ShopSignConfig{});
    REQUIRE(result.ok());
    CHECK_FALSE(result.spec->buyPrice.has_value());
    CHECK(result.spec->sellPrice == 5);
}

TEST_CASE("改买入价：改完卖出价高于买入价仍要判（手写编辑同一条规则）") {
    auto const blocked = withBuyPrice(sampleSpec(), "3", ShopSignConfig{});
    CHECK_FALSE(blocked.ok());
    CHECK(mentions(blocked.error, "卖出价不能高于买入价"));

    // 相等是允许的（B 5 : S 5）。
    auto const equal = withBuyPrice(sampleSpec(), "5", ShopSignConfig{});
    REQUIRE(equal.ok());
    CHECK(equal.spec->buyPrice == 5);
}

TEST_CASE("改买入价：配置关掉「卖出价不得高于买入价」后照配置放行") {
    ShopSignConfig config;
    config.blockSellPriceHigherThanBuy = false;

    auto const result = withBuyPrice(sampleSpec(), "3", config);
    REQUIRE(result.ok());
    CHECK(result.spec->buyPrice == 3);
    CHECK(result.spec->sellPrice == 5);
}

TEST_CASE("改卖出价：只换卖出价，其余字段原样带过去") {
    auto const result = withSellPrice(sampleSpec(), "7", ShopSignConfig{});

    REQUIRE(result.ok());
    CHECK(result.spec->sellPrice == 7);
    CHECK(result.spec->buyPrice == 10);
    CHECK(result.spec->amount == 64);
}

TEST_CASE("改卖出价：留空 = 这一侧不收购（第 3 行只剩 B 10）") {
    auto const result = withSellPrice(sampleSpec(), "", ShopSignConfig{});

    REQUIRE(result.ok());
    CHECK_FALSE(result.spec->sellPrice.has_value());
    CHECK(result.spec->buyPrice == 10);

    auto const lines = formatShopSign(*result.spec, "Alex", "钻石");
    CHECK(lines[SignLine::PriceLine] == "B 10");
}

TEST_CASE("改卖出价：只写空白也算留空") {
    auto const result = withSellPrice(sampleSpec(), "  ", ShopSignConfig{});

    REQUIRE(result.ok());
    CHECK_FALSE(result.spec->sellPrice.has_value());
}

TEST_CASE("改卖出价：0 是合法的价格（免费收货），不是「没填」") {
    auto const result = withSellPrice(sampleSpec(), "0", ShopSignConfig{});

    REQUIRE(result.ok());
    REQUIRE(result.spec->sellPrice.has_value());
    CHECK(*result.spec->sellPrice == 0);
}

TEST_CASE("改卖出价：输入解析与买入价同一套，非负整数之外一律不收") {
    for (auto const* input : {"S 5", "B 10", "free", "1.5", "1K", "-1", "abc", "0x10", "10,000"}) {
        auto const result = withSellPrice(sampleSpec(), input, ShopSignConfig{});
        CHECK_MESSAGE(!result.ok(), "这个输入不该被当成卖出价：" << input);
        // 原因要说清是哪一侧写错了，不能说成「买入价」。
        CHECK(mentions(result.error, "卖出价"));
        CHECK_FALSE(mentions(result.error, "买入价只能"));
    }
}

TEST_CASE("改卖出价：卖出价高于买入价要拒，并把两个价念出来") {
    auto const blocked = withSellPrice(sampleSpec(), "12", ShopSignConfig{});
    CHECK_FALSE(blocked.ok());
    CHECK(mentions(blocked.error, "卖出价不能高于买入价"));
    CHECK(mentions(blocked.error, "12"));
    CHECK(mentions(blocked.error, "10"));

    // 相等是允许的（B 5 : S 5）。
    auto const equal = withSellPrice(sampleSpec(), "10", ShopSignConfig{});
    REQUIRE(equal.ok());
    CHECK(equal.spec->sellPrice == 10);
}

TEST_CASE("改卖出价：买入价为空时没有可比的价，随便写") {
    ShopSpec spec;
    spec.amount    = 64;
    spec.sellPrice = 5; // 只收购的店：没有买入价

    auto const result = withSellPrice(spec, "999", ShopSignConfig{});
    REQUIRE(result.ok());
    CHECK(result.spec->sellPrice == 999);
}

TEST_CASE("改卖出价：两侧都清空要拒绝，并指出想关店是拆牌或停业") {
    ShopSpec spec;
    spec.amount    = 64;
    spec.sellPrice = 5; // 只有卖出价的店：把它留空就什么都不剩了

    auto const result = withSellPrice(spec, "", ShopSignConfig{});
    CHECK_FALSE(result.ok());
    CHECK(mentions(result.error, "至少要留一侧价格"));
    CHECK(mentions(result.error, "失去保护"));
    CHECK(mentions(result.error, "拆牌"));
    CHECK(mentions(result.error, "停业"));
}

TEST_CASE("改每份件数：只换件数，价格与其余字段原样带过去") {
    auto const result = withAmount(sampleSpec(), "16", ShopSignConfig{});

    REQUIRE(result.ok());
    CHECK(result.spec->amount == 16);
    CHECK(result.spec->buyPrice == 10);
    CHECK(result.spec->sellPrice == 5);
}

TEST_CASE("改每份件数：两端空白不影响取值") {
    auto const result = withAmount(sampleSpec(), " 16 ", ShopSignConfig{});

    REQUIRE(result.ok());
    CHECK(result.spec->amount == 16);
}

TEST_CASE("改每份件数：1 与 maxShopAmount 都在范围内") {
    ShopSignConfig config;
    config.maxShopAmount = 64;

    auto const one = withAmount(sampleSpec(), "1", config);
    REQUIRE(one.ok());
    CHECK(one.spec->amount == 1);

    auto const max = withAmount(sampleSpec(), "64", config);
    REQUIRE(max.ok());
    CHECK(max.spec->amount == 64);

    // 默认配置的上限也不是一个「不能用」的边界。
    ShopSignConfig const defaults;
    auto const           defaultMax = withAmount(sampleSpec(), std::to_string(defaults.maxShopAmount), defaults);
    REQUIRE(defaultMax.ok());
    CHECK(defaultMax.spec->amount == defaults.maxShopAmount);
}

TEST_CASE("改每份件数：0 与 maxShopAmount+1 越界要拒并说出范围，不夹到边界") {
    ShopSignConfig config;
    config.maxShopAmount = 64;

    for (auto const* input : {"0", "65", "999"}) {
        auto const result = withAmount(sampleSpec(), input, config);
        CHECK_MESSAGE(!result.ok(), "这个件数不该被接受：" << input);
        CHECK_FALSE(result.spec.has_value());
        CHECK(mentions(result.error, "一份的件数要写 1 到 64"));
    }
}

TEST_CASE("改每份件数：留空 / 非整数 / 负数 / 小数都不收（件数没有「不做生意」这一档）") {
    for (auto const* input : {"", "   ", "1.5", "-1", "abc", "64 个", "1K", "99999999999999999999"}) {
        auto const result = withAmount(sampleSpec(), input, ShopSignConfig{});
        CHECK_MESSAGE(!result.ok(), "这个输入不该被当成件数：" << input);
        CHECK(mentions(result.error, "一份的件数要写 1 到"));
    }
}

TEST_CASE("改完之后 formatShopSign 的输出与手写编辑同格式") {
    ShopSignConfig const config;

    // 手写编辑那张牌、按同一套规范化拼出来的文本，就是管理表单应该写出来的样子。
    auto const handwritten = parseShopSignText("Alex\n64\nB 10 : S 7\n钻石", /*editorIsOp=*/false, config);
    REQUIRE(handwritten.ok());
    auto const expected = joinSignText(formatShopSign(*handwritten.spec, "Alex", "钻石"));

    auto const edited = withSellPrice(sampleSpec(), "7", config);
    REQUIRE(edited.ok());
    CHECK(joinSignText(formatShopSign(*edited.spec, "Alex", "钻石")) == expected);

    // 件数也走同一条规范化：手写 16 个一份与表单改成 16 必须一字不差。
    auto const handwrittenAmount = parseShopSignText("Alex\n16\nB 10 : S 5\n钻石", /*editorIsOp=*/false, config);
    REQUIRE(handwrittenAmount.ok());
    auto const expectedAmount = joinSignText(formatShopSign(*handwrittenAmount.spec, "Alex", "钻石"));

    auto const amountEdited = withAmount(sampleSpec(), "16", config);
    REQUIRE(amountEdited.ok());
    CHECK(joinSignText(formatShopSign(*amountEdited.spec, "Alex", "钻石")) == expectedAmount);
}

TEST_CASE("改完的牌面仍然是商店牌（卖出价留空不会让保护失效）") {
    ShopSignConfig const config;

    auto const edited = withSellPrice(sampleSpec(), "", config);
    REQUIRE(edited.ok());

    auto const text     = joinSignText(formatShopSign(*edited.spec, "Alex", "钻石"));
    auto const reparsed = parseShopSignText(text, /*editorIsOp=*/false, config);

    REQUIRE(reparsed.ok());
    CHECK(reparsed.spec->buyPrice == 10);
    CHECK_FALSE(reparsed.spec->sellPrice.has_value());
    CHECK(reparsed.spec->amount == 64);
}

TEST_CASE("管理表单正文：复用信息表单的内容，不重算库存") {
    auto const content = shopManageFormContent(sampleShop(), /*waxed=*/false);

    CHECK(mentions(content, "店主：Alex"));
    CHECK(mentions(content, "商品：钻石"));
    CHECK(mentions(content, "每份：64 个"));
    CHECK(mentions(content, "买入价：$ 10 / 份"));
    CHECK(mentions(content, "可成交份数：3 份"));
    CHECK(mentions(content, "余额：$ 100"));
    // 改价的写法提示要说清楚「留空」与「至少留一侧」。
    CHECK(mentions(content, "留空"));
}

TEST_CASE("管理表单正文：上蜡的牌补一句，免得店主以为改不了") {
    auto const waxed = shopManageFormContent(sampleShop(), /*waxed=*/true);
    CHECK(mentions(waxed, "上了蜡"));
    CHECK(mentions(waxed, "这里可以直接改"));

    auto const plain = shopManageFormContent(sampleShop(), /*waxed=*/false);
    CHECK_FALSE(mentions(plain, "上了蜡"));
}

TEST_CASE("管理表单按钮：三个入口都带上当前值") {
    auto const buttons = shopManageFormButtons(sampleShop());

    REQUIRE(buttons.size() == 3);
    CHECK(buttons[0].action == ShopFormAction::ManageBuyPrice);
    CHECK(mentions(buttons[0].label, "改买入价"));
    CHECK(mentions(buttons[0].label, "$ 10"));

    CHECK(buttons[1].action == ShopFormAction::ManageSellPrice);
    CHECK(mentions(buttons[1].label, "改卖出价"));
    CHECK(mentions(buttons[1].label, "$ 5"));

    CHECK(buttons[2].action == ShopFormAction::ManageAmount);
    CHECK(mentions(buttons[2].label, "改每份件数"));
    CHECK(mentions(buttons[2].label, "64 个"));
}

TEST_CASE("管理表单按钮：没有买入价时说「不出售」，不显示一个假价格") {
    auto data = sampleShop();
    data.buyPrice.reset();

    auto const buttons = shopManageFormButtons(data);
    REQUIRE(buttons.size() == 3);
    CHECK(mentions(buttons[0].label, "不出售"));
    CHECK_FALSE(mentions(buttons[0].label, "$"));
    // 另一侧照常显示：一侧不做生意不影响另一侧。
    CHECK(mentions(buttons[1].label, "$ 5"));
}

TEST_CASE("管理表单按钮：没有卖出价时说「不收购」") {
    auto data = sampleShop();
    data.sellPrice.reset();

    auto const buttons = shopManageFormButtons(data);
    REQUIRE(buttons.size() == 3);
    CHECK(mentions(buttons[1].label, "不收购"));
    CHECK_FALSE(mentions(buttons[1].label, "$"));
    CHECK(mentions(buttons[0].label, "$ 10"));
}

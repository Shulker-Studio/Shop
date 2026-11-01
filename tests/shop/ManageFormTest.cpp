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

TEST_CASE("管理表单按钮：本票只有「改买入价」，并带上当前值") {
    auto const buttons = shopManageFormButtons(sampleShop());

    REQUIRE(buttons.size() == 1);
    CHECK(buttons[0].action == ShopFormAction::ManageBuyPrice);
    CHECK(mentions(buttons[0].label, "改买入价"));
    CHECK(mentions(buttons[0].label, "$ 10"));
}

TEST_CASE("管理表单按钮：没有买入价时说「不出售」，不显示一个假价格") {
    auto data = sampleShop();
    data.buyPrice.reset();

    auto const buttons = shopManageFormButtons(data);
    REQUIRE(buttons.size() == 1);
    CHECK(mentions(buttons[0].label, "不出售"));
    CHECK_FALSE(mentions(buttons[0].label, "$"));
}

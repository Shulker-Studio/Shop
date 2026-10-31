#include "shop/BuyForm.h"

#include <doctest/doctest.h>
#include <string>
#include <vector>

namespace {

using namespace ss::shop;

/// 钻石，64 个一份，买入 10，余额 100，商店箱能撑 5 份。
ShopInfoData sampleShop() {
    ShopInfoData data;
    data.ownerName      = "Alex";
    data.itemName       = "钻石";
    data.amountPerUnit  = 64;
    data.buyPrice       = 10;
    data.stockUnits     = 5;
    data.balance        = 100;
    data.currencySymbol = "$";
    return data;
}

bool hasLine(std::string const& content, std::string const& line) { return content.find(line) != std::string::npos; }

} // namespace

TEST_CASE("份数档位：上限以内的 1/2/4，最后一项是自定义") {
    auto const model = makeBuyFormModel(4);

    REQUIRE(model.choices.size() == 4);
    CHECK(model.choices[0].units == 1);
    CHECK(model.choices[1].units == 2);
    CHECK(model.choices[2].units == 4);
    CHECK(model.choices[3].units == 0);

    auto const options = buyFormOptions(model);
    REQUIRE(options.size() == model.choices.size());
    CHECK(options[3] == "自定义份数");
}

TEST_CASE("份数档位：上限小于某档时那一档不出现") {
    auto const model = makeBuyFormModel(3);

    REQUIRE(model.choices.size() == 3);
    CHECK(model.choices[0].units == 1);
    CHECK(model.choices[1].units == 2);
    CHECK(model.choices[2].units == 0);
}

TEST_CASE("份数档位：上限为 0 时只剩自定义（这种店本来就不该弹表单）") {
    auto const model = makeBuyFormModel(0);

    REQUIRE(model.choices.size() == 1);
    CHECK(model.choices[0].units == 0);
    CHECK(model.maxUnits == 0);
}

TEST_CASE("表单正文：自定义档把乘数与上限写全，玩家能自己算") {
    auto const content = buyFormContent(sampleShop(), makeBuyFormModel(4));

    CHECK(hasLine(content, "自定义份数：总价 = $ 10 × 份数（1 到 4 份，恒为整数）"));
}

TEST_CASE("表单正文：每一档的总件数与总价都写出来") {
    auto const model   = makeBuyFormModel(4);
    auto const content = buyFormContent(sampleShop(), model);

    CHECK(hasLine(content, "每份：64 个"));
    CHECK(hasLine(content, "买入价：$ 10 / 份"));
    CHECK(hasLine(content, "这次最多买：4 份（受余额、背包与单次上限限制）"));
    CHECK(hasLine(content, "选 1 份 → 共 64 个 / $ 10"));
    CHECK(hasLine(content, "选 2 份 → 共 128 个 / $ 20"));
    CHECK(hasLine(content, "选 4 份 → 共 256 个 / $ 40"));
}

TEST_CASE("表单正文：没有余额时不编造数字") {
    auto data = sampleShop();
    data.balance.reset();

    auto const content = buyFormContent(data, makeBuyFormModel(2));
    CHECK_FALSE(hasLine(content, "余额："));
}

TEST_CASE("解析份数：选中预设档位直接用它") {
    auto const model = makeBuyFormModel(4);

    CHECK(resolveRequestedUnits(model, 0, "").units == std::optional<int>{1});
    CHECK(resolveRequestedUnits(model, 2, "").units == std::optional<int>{4});
    // 选了预设档位就忽略输入框里的内容，免得两处值打架
    CHECK(resolveRequestedUnits(model, 1, "999").units == std::optional<int>{2});
}

TEST_CASE("解析份数：自定义要走输入框，且落在 1..上限") {
    auto const model = makeBuyFormModel(10);

    CHECK(resolveRequestedUnits(model, 3, "7").units == std::optional<int>{7});
    CHECK(resolveRequestedUnits(model, 3, "  7 ").units == std::optional<int>{7});
    CHECK(resolveRequestedUnits(model, 3, "10").units == std::optional<int>{10});
}

TEST_CASE("解析份数：自定义的非法输入给出原因，不悄悄夹到上限") {
    auto const model = makeBuyFormModel(4);

    CHECK_FALSE(resolveRequestedUnits(model, 3, "").units.has_value());
    CHECK_FALSE(resolveRequestedUnits(model, 3, "   ").units.has_value());
    CHECK_FALSE(resolveRequestedUnits(model, 3, "abc").units.has_value());
    CHECK_FALSE(resolveRequestedUnits(model, 3, "2.5").units.has_value());
    CHECK_FALSE(resolveRequestedUnits(model, 3, "-3").units.has_value());
    CHECK_FALSE(resolveRequestedUnits(model, 3, "0").units.has_value());
    CHECK_FALSE(resolveRequestedUnits(model, 3, "5").units.has_value());
    CHECK_FALSE(resolveRequestedUnits(model, 3, "999999999999").units.has_value());

    CHECK_FALSE(resolveRequestedUnits(model, 3, "5").error.empty());
}

TEST_CASE("解析份数：下拉框下标越界时报错而不是崩") {
    auto const model = makeBuyFormModel(2);

    CHECK_FALSE(resolveRequestedUnits(model, -1, "").units.has_value());
    CHECK_FALSE(resolveRequestedUnits(model, 99, "").units.has_value());
}

TEST_CASE("部分成交的确认正文写的是重算后的真实份数、件数与金额") {
    auto const content = buyConfirmContent(sampleShop(), BuyPlan{2, 128, 20}, ChangeReason::Units);

    CHECK(hasLine(content, "这家店现在只够成交 2 份"));
    CHECK(hasLine(content, "成交份数：2 份"));
    CHECK(hasLine(content, "总件数：128 个"));
    CHECK(hasLine(content, "实际扣款：$ 20"));
    CHECK(hasLine(content, "当前余额：$ 100"));
    CHECK(hasLine(content, "确认后按上面的份数与金额成交。"));
}

TEST_CASE("只改价时确认正文说价格变了，不说「只够成交几份」") {
    auto const content = buyConfirmContent(sampleShop(), BuyPlan{2, 128, 40}, ChangeReason::Price);

    CHECK(hasLine(content, "这家店的买入价刚刚变了"));
    CHECK_FALSE(hasLine(content, "只够成交"));
    CHECK(hasLine(content, "实际扣款：$ 40"));
}

TEST_CASE("只改「一份的件数」时确认正文把新的件数说出来") {
    auto data             = sampleShop();
    data.amountPerUnit    = 1;
    auto const content    = buyConfirmContent(data, BuyPlan{2, 2, 20}, ChangeReason::Items);

    CHECK(hasLine(content, "这家店「一份的件数」变了，现在 1 个算一份"));
    CHECK(hasLine(content, "总件数：2 个"));
}

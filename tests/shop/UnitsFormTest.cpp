#include "shop/UnitsForm.h"

#include <doctest/doctest.h>
#include <string>
#include <vector>

namespace {

using namespace ss::shop;

/// 钻石，64 个一份，买入 10、卖出 5，商店箱能撑 5 份，玩家手里还有 3 份。
ShopInfoData sampleShop() {
    ShopInfoData data;
    data.ownerName      = "Alex";
    data.itemName       = "钻石";
    data.amountPerUnit  = 64;
    data.buyPrice       = 10;
    data.sellPrice      = 5;
    data.stockUnits     = 5;
    data.heldUnits      = 3;
    data.boxRoomUnits   = 7;
    data.ownerBalance   = 50;
    data.balance        = 100;
    data.currencySymbol = "$";
    return data;
}

bool hasLine(std::string const& content, std::string const& line) { return content.find(line) != std::string::npos; }

} // namespace

TEST_CASE("份数档位：上限以内的 1/2/4，最后一项是自定义") {
    auto const model = makeUnitsFormModel(4);

    REQUIRE(model.choices.size() == 4);
    CHECK(model.choices[0].units == 1);
    CHECK(model.choices[1].units == 2);
    CHECK(model.choices[2].units == 4);
    CHECK(model.choices[3].units == 0);

    auto const options = unitsFormOptions(model);
    REQUIRE(options.size() == model.choices.size());
    CHECK(options[3] == "自定义份数");
}

TEST_CASE("份数档位：上限小于某档时那一档不出现") {
    auto const model = makeUnitsFormModel(3);

    REQUIRE(model.choices.size() == 3);
    CHECK(model.choices[0].units == 1);
    CHECK(model.choices[1].units == 2);
    CHECK(model.choices[2].units == 0);
}

TEST_CASE("份数档位：上限为 0 时只剩自定义（这种店本来就不该弹表单）") {
    auto const model = makeUnitsFormModel(0);

    REQUIRE(model.choices.size() == 1);
    CHECK(model.choices[0].units == 0);
    CHECK(model.maxUnits == 0);
}

TEST_CASE("买表单正文：自定义档把乘数与上限写全，玩家能自己算") {
    auto const content = unitsFormContent(TradeDirection::Buy, sampleShop(), makeUnitsFormModel(4));

    CHECK(hasLine(content, "自定义份数：总价 = $ 10 × 份数（1 到 4 份，恒为整数）"));
}

TEST_CASE("买表单正文：每一档的总件数与总价都写出来") {
    auto const model   = makeUnitsFormModel(4);
    auto const content = unitsFormContent(TradeDirection::Buy, sampleShop(), model);

    CHECK(hasLine(content, "每份：64 个"));
    CHECK(hasLine(content, "买入价：$ 10 / 份"));
    CHECK(hasLine(content, "商店箱里的货还够：5 份"));
    CHECK(hasLine(content, "这次最多买：4 份（受余额、背包与单次上限限制）"));
    CHECK(hasLine(content, "选 1 份 → 共 64 个 / $ 10"));
    CHECK(hasLine(content, "选 2 份 → 共 128 个 / $ 20"));
    CHECK(hasLine(content, "选 4 份 → 共 256 个 / $ 40"));
}

TEST_CASE("卖表单正文：另外两个上限的数也摆出来，买入侧不显示它们") {
    auto const model    = makeUnitsFormModel(2);
    auto const sellForm = unitsFormContent(TradeDirection::Sell, sampleShop(), model);
    auto const buyForm  = unitsFormContent(TradeDirection::Buy, sampleShop(), model);

    CHECK(hasLine(sellForm, "商店箱还能装：7 份"));
    CHECK(hasLine(sellForm, "店主余额：$ 50"));
    CHECK(hasLine(sellForm, "你的余额：$ 100"));
    CHECK_FALSE(hasLine(buyForm, "商店箱还能装："));
    CHECK_FALSE(hasLine(buyForm, "店主余额："));
}

TEST_CASE("卖表单正文：店主余额读不到时不编造数字") {
    auto data = sampleShop();
    data.ownerBalance.reset();

    auto const content = unitsFormContent(TradeDirection::Sell, data, makeUnitsFormModel(2));
    CHECK_FALSE(hasLine(content, "店主余额："));
    CHECK(hasLine(content, "商店箱还能装：7 份"));
}

TEST_CASE("卖表单正文：限额来源是玩家手里的货与店主的钱，动词换成收入") {
    auto const model   = makeUnitsFormModel(2);
    auto const content = unitsFormContent(TradeDirection::Sell, sampleShop(), model);

    CHECK(hasLine(content, "卖出价：$ 5 / 份"));
    CHECK(hasLine(content, "你手里有：3 份"));
    CHECK_FALSE(hasLine(content, "箱子里的货还够"));
    CHECK(hasLine(content, "这次最多卖：2 份（受店主余额、商店箱空间与单次上限限制）"));
    CHECK(hasLine(content, "选 1 份 → 共 64 个 / $ 5"));
    CHECK(hasLine(content, "选 2 份 → 共 128 个 / $ 10"));
    CHECK(hasLine(content, "自定义份数：收入 = $ 5 × 份数（1 到 2 份，恒为整数）"));
}

TEST_CASE("卖表单正文：管理员店不把「店主余额」写进限额来源") {
    auto data      = sampleShop();
    data.adminShop = true;
    data.ownerBalance.reset(); // 管理员店没有可读的店主余额（DESIGN §7.4）

    auto const content = unitsFormContent(TradeDirection::Sell, data, makeUnitsFormModel(2));
    CHECK_FALSE(hasLine(content, "店主余额"));
    CHECK(hasLine(content, "这次最多卖：2 份（受你手里的货、商店箱空间与单次上限限制）"));
}

TEST_CASE("表单正文：没有余额时不编造数字") {
    auto data = sampleShop();
    data.balance.reset();

    auto const content = unitsFormContent(TradeDirection::Buy, data, makeUnitsFormModel(2));
    CHECK_FALSE(hasLine(content, "余额："));
}

TEST_CASE("解析份数：选中预设档位直接用它") {
    auto const model = makeUnitsFormModel(4);

    CHECK(resolveRequestedUnits(model, 0, "").units == std::optional<int>{1});
    CHECK(resolveRequestedUnits(model, 2, "").units == std::optional<int>{4});
    // 选了预设档位就忽略输入框里的内容，免得两处值打架
    CHECK(resolveRequestedUnits(model, 1, "999").units == std::optional<int>{2});
}

TEST_CASE("解析份数：自定义要走输入框，且落在 1..上限") {
    auto const model = makeUnitsFormModel(10);

    CHECK(resolveRequestedUnits(model, 3, "7").units == std::optional<int>{7});
    CHECK(resolveRequestedUnits(model, 3, "  7 ").units == std::optional<int>{7});
    CHECK(resolveRequestedUnits(model, 3, "10").units == std::optional<int>{10});
}

TEST_CASE("解析份数：自定义的非法输入给出原因，不悄悄夹到上限") {
    auto const model = makeUnitsFormModel(4);

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
    auto const model = makeUnitsFormModel(2);

    CHECK_FALSE(resolveRequestedUnits(model, -1, "").units.has_value());
    CHECK_FALSE(resolveRequestedUnits(model, 99, "").units.has_value());
}

TEST_CASE("下拉框回传的是选项文案：文案能换算回下标") {
    auto const model = makeUnitsFormModel(4);
    auto const options = unitsFormOptions(model);

    // 真机上 LeviLamina 的 Dropdown::parseResult 返回的就是这些文案
    CHECK(choiceIndexForOption(model, options[0]) == 0);
    CHECK(choiceIndexForOption(model, "1 份") == 0);
    CHECK(choiceIndexForOption(model, "2 份") == 1);
    CHECK(choiceIndexForOption(model, "4 份") == 2);
    CHECK(choiceIndexForOption(model, "自定义份数") == 3);

    // 换算回来之后走的是同一套解析：预设档直接成交，自定义档读输入框
    CHECK(resolveRequestedUnits(model, choiceIndexForOption(model, "4 份"), "").units == std::optional<int>{4});
    CHECK(
        resolveRequestedUnits(model, choiceIndexForOption(model, "自定义份数"), "3").units == std::optional<int>{3}
    );
}

TEST_CASE("下拉框文案：上限压低了也能换算，认不出的文案返回 -1") {
    auto const narrow = makeUnitsFormModel(2);

    CHECK(choiceIndexForOption(narrow, "2 份") == 1);
    // 这一档在上限 2 的表单里根本不存在
    CHECK(choiceIndexForOption(narrow, "4 份") == -1);
    CHECK(choiceIndexForOption(narrow, "") == -1);
    CHECK(choiceIndexForOption(narrow, "3") == -1);
    CHECK_FALSE(resolveRequestedUnits(narrow, choiceIndexForOption(narrow, "4 份"), "").units.has_value());
}

TEST_CASE("部分成交的确认正文写的是重算后的真实份数、件数与金额") {
    auto const content =
        unitsConfirmContent(TradeDirection::Buy, sampleShop(), TradePlan{2, 128, 20}, ChangeReason::Units);

    CHECK(hasLine(content, "这家店现在只够成交 2 份"));
    CHECK(hasLine(content, "成交份数：2 份"));
    CHECK(hasLine(content, "总件数：128 个"));
    CHECK(hasLine(content, "实际扣款：$ 20"));
    CHECK(hasLine(content, "你的余额：$ 100"));
    CHECK(hasLine(content, "确认后按上面的份数与金额成交。"));
}

TEST_CASE("只改价时确认正文说价格变了，不说「只够成交几份」") {
    auto const content =
        unitsConfirmContent(TradeDirection::Buy, sampleShop(), TradePlan{2, 128, 40}, ChangeReason::Price);

    CHECK(hasLine(content, "这家店的买入价刚刚变了"));
    CHECK_FALSE(hasLine(content, "只够成交"));
    CHECK(hasLine(content, "实际扣款：$ 40"));
}

TEST_CASE("只改「一份的件数」时确认正文把新的件数说出来") {
    auto data          = sampleShop();
    data.amountPerUnit = 1;
    auto const content = unitsConfirmContent(TradeDirection::Buy, data, TradePlan{2, 2, 20}, ChangeReason::Items);

    CHECK(hasLine(content, "这家店「一份的件数」变了，现在 1 个算一份"));
    CHECK(hasLine(content, "总件数：2 个"));
}

TEST_CASE("卖出的确认正文：金额是玩家收到的钱，改价说的是卖出价") {
    auto const content =
        unitsConfirmContent(TradeDirection::Sell, sampleShop(), TradePlan{2, 128, 10}, ChangeReason::Price);

    CHECK(hasLine(content, "这家店的卖出价刚刚变了"));
    CHECK(hasLine(content, "成交份数：2 份"));
    CHECK(hasLine(content, "总件数：128 个"));
    CHECK(hasLine(content, "实际收款：$ 10"));
    CHECK_FALSE(hasLine(content, "实际扣款"));
}

TEST_CASE("份数变少的确认正文：卖出的成因不是「这家店只够成交」") {
    auto const content =
        unitsConfirmContent(TradeDirection::Sell, sampleShop(), TradePlan{1, 64, 5}, ChangeReason::Units);

    CHECK(hasLine(content, "这一笔现在只成交得了 1 份"));
    CHECK_FALSE(hasLine(content, "这家店现在只够成交"));
    CHECK(hasLine(content, "实际收款：$ 5"));
}

TEST_CASE("份数表单正文：无限库存写「无限」，不写成数字") {
    auto admin       = sampleShop();
    admin.adminShop  = true;
    admin.stockUnits = StockView::makeUnlimited();

    auto const content = unitsFormContent(TradeDirection::Buy, admin, makeUnitsFormModel(4));
    CHECK(hasLine(content, "商店箱里的货还够：无限（管理员店，永远有货）"));
    // 箱子里明明只有一件样品，写「5 份」会让玩家以为买 5 份就卖光
    CHECK_FALSE(hasLine(content, "商店箱里的货还够：5 份"));
}

TEST_CASE("份数表单正文：卖出方向仍是玩家手里的数字，货没有被无限化") {
    auto admin       = sampleShop();
    admin.adminShop  = true;
    admin.stockUnits = StockView::makeUnlimited(); // 只管买入；卖出看玩家背包

    auto const content = unitsFormContent(TradeDirection::Sell, admin, makeUnitsFormModel(3));
    CHECK(hasLine(content, "你手里有：3 份"));
    CHECK_FALSE(hasLine(content, "你手里有：无限"));
}

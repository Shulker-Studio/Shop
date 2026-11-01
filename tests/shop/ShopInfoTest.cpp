#include "shop/ShopInfo.h"

#include <doctest/doctest.h>
#include <string>

namespace {

using namespace ss::shop;

/// 一个典型的普通商店：钻石，64 个一份，买入 10、卖出 5，可成交 3 份。
ShopInfoData sampleShop() {
    ShopInfoData data;
    data.ownerName     = "Alex";
    data.itemName      = "钻石";
    data.amountPerUnit = 64;
    data.buyPrice      = 10;
    data.sellPrice     = 5;
    data.stockUnits    = 3;
    // 货币符号只从 `economy.currencySymbol` 来，这里手填模拟调用方
    data.currencySymbol = "$";
    return data;
}

bool hasLine(std::vector<std::string> const& lines, std::string const& line) {
    for (auto const& candidate : lines) {
        if (candidate == line) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("表单标题：普通店是「<店主> 的商店」，管理员店是「管理员商店」") {
    auto data = sampleShop();
    CHECK(shopFormTitle(data) == "Alex 的商店");

    data.adminShop = true;
    CHECK(shopFormTitle(data) == "管理员商店");
}

TEST_CASE("表单标题：店主名缺失时不冒出空洞标题") {
    ShopInfoData data;
    CHECK(shopFormTitle(data) == "未知店主的商店");
}

TEST_CASE("表单正文：一行一项，覆盖店主/商品/份量/买卖价/可成交份数/余额") {
    auto const lines = shopInfoLines(sampleShop());

    CHECK(hasLine(lines, "店主：Alex"));
    CHECK(hasLine(lines, "商品：钻石"));
    CHECK(hasLine(lines, "每份：64 个"));
    CHECK(hasLine(lines, "买入价：$ 10 / 份"));
    CHECK(hasLine(lines, "卖出价：$ 5 / 份"));
    CHECK(hasLine(lines, "可成交份数：3 份"));
}

TEST_CASE("表单正文：管理员店的店主行不写玩家名") {
    auto data      = sampleShop();
    data.adminShop = true;
    CHECK(hasLine(shopInfoLines(data), "店主：管理员商店"));
}

TEST_CASE("表单正文：缺一边价格时写明不出售 / 不收购") {
    auto data = sampleShop();
    data.sellPrice.reset();
    auto const lines = shopInfoLines(data);

    CHECK(hasLine(lines, "买入价：$ 10 / 份"));
    CHECK(hasLine(lines, "卖出价：不收购"));
}

TEST_CASE("表单正文：余额读不到时写占位，不编造数字") {
    auto data = sampleShop();
    CHECK_FALSE(data.balance.has_value());

    // 空有两个原因（没有可用后端、读不到这名玩家的账户），正文不猜是哪一个：
    // 具体原因在不能交易时由「状态：」那一行说清楚（DESIGN §8.2）。
    auto const lines = shopInfoLines(data);
    CHECK(hasLine(lines, "余额：暂时读不到（经济后端没给出这个账户的余额）"));

    data.balance = 100;
    CHECK(hasLine(shopInfoLines(data), "余额：$ 100"));
}

TEST_CASE("表单正文：货币符号可配置，空符号不留下多余空格") {
    auto data           = sampleShop();
    data.balance        = 100;
    data.currencySymbol = "金币";
    CHECK(hasLine(shopInfoLines(data), "余额：金币 100"));

    data.currencySymbol = "";
    CHECK(hasLine(shopInfoLines(data), "余额：100"));
    CHECK(hasLine(shopInfoLines(data), "买入价：10 / 份"));
}

TEST_CASE("表单正文：售罄时明确写 0 份，而不是藏起来") {
    auto data       = sampleShop();
    data.stockUnits = 0;
    CHECK(hasLine(shopInfoLines(data), "可成交份数：0 份"));
}

TEST_CASE("表单正文：纯收购店不把可成交份数当卖点") {
    auto data = sampleShop();
    data.buyPrice.reset(); // 只写 S：玩家只能卖给它
    data.stockUnits = 0;

    auto const lines = shopInfoLines(data);
    CHECK_FALSE(hasLine(lines, "可成交份数：0 份"));
    CHECK(hasLine(lines, "可成交份数：—（本店只收购，以你手里的数量为准）"));
}

TEST_CASE("表单正文：商品名缺失时写占位") {
    auto data = sampleShop();
    data.itemName.clear();
    CHECK(hasLine(shopInfoLines(data), "商品：—"));
}

TEST_CASE("表单正文：多行拼成一个字符串，行间是换行符") {
    auto const content = shopFormContent(sampleShop());
    CHECK(content.find("店主：Alex") != std::string::npos);
    CHECK(content.find("店主：Alex\n商品：钻石") != std::string::npos);
    // 行尾不留换行，免得表单底部多一行空白
    CHECK(content.back() != '\n');
}

TEST_CASE("买家按钮：按 DESIGN §10.1 的文案给出购买与出售") {
    auto const buttons = shopFormButtons(sampleShop(), ShopFormRole::Visitor);

    REQUIRE(buttons.size() == 2);
    CHECK(buttons[0].action == ShopFormAction::Buy);
    CHECK(buttons[0].label == "购买（1 份 64 个 / $ 10）");
    CHECK(buttons[1].action == ShopFormAction::Sell);
    CHECK(buttons[1].label == "出售（1 份 64 个 / $ 5）");
}

TEST_CASE("买家按钮：只有一边价格时只给那一个按钮") {
    auto data = sampleShop();
    data.sellPrice.reset();
    auto const buyOnly = shopFormButtons(data, ShopFormRole::Visitor);
    REQUIRE(buyOnly.size() == 1);
    CHECK(buyOnly[0].action == ShopFormAction::Buy);

    data = sampleShop();
    data.buyPrice.reset();
    auto const sellOnly = shopFormButtons(data, ShopFormRole::Visitor);
    REQUIRE(sellOnly.size() == 1);
    CHECK(sellOnly[0].action == ShopFormAction::Sell);
}

TEST_CASE("店主按钮：只有一个管理入口，没有买卖按钮") {
    auto const buttons = shopFormButtons(sampleShop(), ShopFormRole::Manager);

    REQUIRE(buttons.size() == 1);
    CHECK(buttons[0].action == ShopFormAction::Manage);
    CHECK(buttons[0].label == "管理商店");
}

TEST_CASE("商店箱空了：正文写清原因，不给买卖按钮") {
    auto data = sampleShop();
    data.buyPrice.reset();
    data.sellPrice.reset();
    data.unavailableReason = emptyBoxReason();

    CHECK(emptyBoxReason().find("没有货") != std::string::npos);
    CHECK(hasLine(shopInfoLines(data), "状态：" + emptyBoxReason()));
    CHECK(shopFormButtons(data, ShopFormRole::Visitor).empty());
}

TEST_CASE("读不到店主余额的说法：讲的是余额，不是店主没钱") {
    auto const reason = ownerFundsUnknownReason();
    CHECK(reason.find("余额") != std::string::npos);
    CHECK(reason.find("资金不足") == std::string::npos);
    CHECK_FALSE(reason == ownerOfflineReason());
    CHECK_FALSE(reason == noEconomyReason());
}

TEST_CASE("后端当不了系统账户的说法：说的是经济后端能力，不是玩家的问题") {
    auto const reason = noSystemAccountReason();
    CHECK_FALSE(reason.empty());
    CHECK(reason.find("系统账户") != std::string::npos);
    CHECK(reason.find("管理员商店") != std::string::npos);
    // 与「没有后端」「店主不可达」是两回事，文案不能串行。
    CHECK_FALSE(reason == noEconomyReason());
    CHECK_FALSE(reason == ownerOfflineReason());
}

TEST_CASE("表单角色：普通店店主与 OP 拿管理入口，管理员店所有人都能买卖") {
    CHECK(shopFormRoleOf(ShopActor::Owner, /*adminShop=*/false) == ShopFormRole::Manager);
    CHECK(shopFormRoleOf(ShopActor::Operator, /*adminShop=*/false) == ShopFormRole::Manager);
    CHECK(shopFormRoleOf(ShopActor::Other, false) == ShopFormRole::Visitor);
    CHECK(shopFormRoleOf(ShopActor::NoRecordedOwner, false) == ShopFormRole::Visitor);

    // 管理员店：对手方是系统账户（§7.4），店主不是交易的一侧，OP 也能买卖（票 11）
    for (auto const actor :
         {ShopActor::Owner, ShopActor::Operator, ShopActor::Other, ShopActor::NoRecordedOwner}) {
        CHECK(shopFormRoleOf(actor, /*adminShop=*/true) == ShopFormRole::Visitor);
    }
}

TEST_CASE("管理员店 + 后端当不了系统账户：正文写原因，买卖按钮一个不给") {
    auto data              = sampleShop();
    data.adminShop         = true;
    data.unavailableReason = noSystemAccountReason();

    CHECK(hasLine(shopInfoLines(data), "状态：" + noSystemAccountReason()));
    CHECK(shopFormButtons(data, ShopFormRole::Visitor).empty());
}

TEST_CASE("表单正文：不能交易时写明原因，且不给买卖按钮") {
    auto data              = sampleShop();
    data.unavailableReason = "店主不在线，现在不能交易";

    CHECK(hasLine(shopInfoLines(data), "状态：店主不在线，现在不能交易"));
    CHECK(shopFormButtons(data, ShopFormRole::Visitor).empty());

    data.unavailableReason = "服务器没有可用的经济后端，暂时不能交易";
    CHECK(shopFormButtons(data, ShopFormRole::Visitor).empty());
}

TEST_CASE("表单按钮：能交易时买家拿到购买与出售两个按钮") {
    auto const buttons = shopFormButtons(sampleShop(), ShopFormRole::Visitor);

    REQUIRE(buttons.size() == 2);
    CHECK(buttons[0].action == ShopFormAction::Buy);
    CHECK(buttons[1].action == ShopFormAction::Sell);
}

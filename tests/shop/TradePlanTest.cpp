#include "shop/TradePlan.h"

#include <doctest/doctest.h>
#include <limits>

namespace {

using namespace ss::shop;

/// 一价 10、一份 64 件、余额充足、商店箱够货、背包放得下的正常买入。
///
/// `TradeRequest` 买卖共用，角色由调用方装：买入时 `goodsUnits` 是商店箱、
/// `roomUnits` 是背包、`payerFunds` 是买家余额（见 `TradePlan.h` 的对照表）。
TradeRequest soundBuy() {
    TradeRequest request;
    request.requested     = 1;
    request.amountPerUnit = 64;
    request.unitPrice     = 10;
    request.payerFunds    = 100;
    request.goodsUnits    = 5;
    request.roomUnits     = 5;
    request.allowPartial  = true;
    request.ownerOnline   = true;
    return request;
}

/// 同样的形状，卖出的角色反过来：货是玩家手里、收货方是商店箱、付款方是店主。
TradeRequest soundSell() {
    TradeRequest request;
    request.requested     = 1;
    request.amountPerUnit = 64;
    request.unitPrice     = 5;   // 卖出价
    request.payerFunds    = 100; // 店主余额
    request.goodsUnits    = 3;   // 玩家手里有几份
    request.roomUnits     = 10;  // 商店箱还能装几份
    request.allowPartial  = true;
    request.ownerOnline   = true;
    return request;
}

} // namespace

TEST_CASE("买一份：成交份数、件数、金额三者一致") {
    auto const decision = planTrade(soundBuy());

    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 1);
    CHECK(decision.plan->items == 64);
    CHECK(decision.plan->total == std::int64_t{10});
}

TEST_CASE("买多份：总价 = 单价 × 份数，恒为整数") {
    auto request       = soundBuy();
    request.requested  = 4;
    request.payerFunds = 40;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 4);
    CHECK(decision.plan->items == 256);
    CHECK(decision.plan->total == std::int64_t{40});
}

TEST_CASE("买：余额不足时整笔拒绝，不给出半份方案") {
    auto request       = soundBuy();
    request.payerFunds = 5;

    auto const decision = planTrade(request);
    CHECK_FALSE(decision.ok());
    CHECK(decision.rejection == TradeRejection::NotEnoughFunds);
}

TEST_CASE("买：箱子空了报缺货，不报缺钱") {
    auto request       = soundBuy();
    request.goodsUnits = 0;
    request.payerFunds = 0;

    auto const decision = planTrade(request);
    CHECK_FALSE(decision.ok());
    CHECK(decision.rejection == TradeRejection::NotEnoughGoods);
}

TEST_CASE("买：背包放不下报收货方装不下") {
    auto request      = soundBuy();
    request.roomUnits = 0;

    auto const decision = planTrade(request);
    CHECK_FALSE(decision.ok());
    CHECK(decision.rejection == TradeRejection::NoRoomToReceive);
}

TEST_CASE("买：部分成交时按实际份数成交，金额按份数重算") {
    auto request       = soundBuy();
    request.requested  = 4;
    request.payerFunds = 100;
    request.goodsUnits = 2;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 2);
    CHECK(decision.plan->items == 128);
    CHECK(decision.plan->total == std::int64_t{20});
}

TEST_CASE("买：余额只够 3 份时按 3 份成交") {
    auto request       = soundBuy();
    request.requested  = 4;
    request.payerFunds = 35;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 3);
    CHECK(decision.plan->total == std::int64_t{30});
}

TEST_CASE("买：背包只放得下 1 份时按 1 份成交") {
    auto request      = soundBuy();
    request.requested = 4;
    request.roomUnits = 1;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 1);
    CHECK(decision.plan->items == 64);
}

TEST_CASE("关掉部分成交：买的三条上限各报各的原因") {
    auto request         = soundBuy();
    request.requested    = 4;
    request.allowPartial = false;

    auto shortGoods       = request;
    shortGoods.goodsUnits = 2;
    CHECK(planTrade(shortGoods).rejection == TradeRejection::NotEnoughGoods);

    auto shortFunds       = request;
    shortFunds.payerFunds = 35;
    CHECK(planTrade(shortFunds).rejection == TradeRejection::NotEnoughFunds);

    auto shortRoom      = request;
    shortRoom.roomUnits = 2;
    CHECK(planTrade(shortRoom).rejection == TradeRejection::NoRoomToReceive);

    // 三条都够，挡住的是 maxShopAmount 这条单次上限
    auto overCap     = request;
    overCap.maxUnits = 2;
    CHECK(planTrade(overCap).rejection == TradeRejection::OverUnitLimit);
}

TEST_CASE("关掉部分成交：刚好够时不误伤") {
    auto request         = soundBuy();
    request.requested    = 2;
    request.payerFunds   = 20;
    request.goodsUnits   = 2;
    request.roomUnits    = 2;
    request.allowPartial = false;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 2);
    CHECK(decision.plan->total == std::int64_t{20});
}

TEST_CASE("店主离线：后端不支持离线记账就整笔拒绝（买卖都一样）") {
    auto buy        = soundBuy();
    buy.ownerOnline = false;
    CHECK(planTrade(buy).rejection == TradeRejection::OwnerOffline);

    auto sell        = soundSell();
    sell.ownerOnline = false;
    CHECK(planTrade(sell).rejection == TradeRejection::OwnerOffline);
}

TEST_CASE("店主离线：后端支持离线记账照常成交") {
    auto request                   = soundBuy();
    request.ownerOnline            = false;
    request.backendSupportsOffline = true;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 1);
}

TEST_CASE("免费商店：钱不是限制，金额为 0") {
    auto request       = soundBuy();
    request.unitPrice  = 0;
    request.payerFunds = 0;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->total == std::int64_t{0});
}

TEST_CASE("非法输入：请求份数、份价、每份件数都必须合法") {
    auto request      = soundBuy();
    request.requested = 0;
    CHECK(planTrade(request).rejection == TradeRejection::InvalidRequest);

    request           = soundBuy();
    request.unitPrice = -1;
    CHECK(planTrade(request).rejection == TradeRejection::InvalidRequest);

    auto noAmount          = soundBuy();
    noAmount.amountPerUnit = 0;
    CHECK(planTrade(noAmount).rejection == TradeRejection::InvalidRequest);

    noAmount.amountPerUnit = -64;
    CHECK(planTrade(noAmount).rejection == TradeRejection::InvalidRequest);
}

TEST_CASE("件数溢出 int 时拒绝，不给出回绕的件数") {
    auto request       = soundBuy();
    request.requested  = std::numeric_limits<int>::max();
    request.goodsUnits = std::numeric_limits<int>::max();
    request.roomUnits  = std::numeric_limits<int>::max();
    request.payerFunds = std::numeric_limits<std::int64_t>::max();

    auto const decision = planTrade(request);
    CHECK_FALSE(decision.ok());
    CHECK(decision.rejection == TradeRejection::InvalidRequest);
}

TEST_CASE("每条拒绝原因都有玩家看得懂的提示") {
    CHECK_FALSE(tradeRejectionMessage(TradeDirection::Buy, TradeRejection::NotEnoughGoods).empty());
    CHECK_FALSE(tradeRejectionMessage(TradeDirection::Buy, TradeRejection::NotEnoughFunds).empty());
    CHECK_FALSE(tradeRejectionMessage(TradeDirection::Buy, TradeRejection::NoRoomToReceive).empty());
    CHECK_FALSE(tradeRejectionMessage(TradeDirection::Buy, TradeRejection::OwnerOffline).empty());
    CHECK_FALSE(tradeRejectionMessage(TradeDirection::Buy, TradeRejection::InvalidRequest).empty());
    CHECK_FALSE(tradeRejectionMessage(TradeDirection::Buy, TradeRejection::ShopClosed).empty());
    CHECK(tradeRejectionMessage(TradeDirection::Buy, TradeRejection::None).empty());
}

TEST_CASE("份数天花板：maxShopAmount 也进上限（DESIGN §10.1 第 3 步）") {
    auto request      = soundBuy();
    request.requested = 10;
    request.maxUnits  = 2;

    // 允许部分成交：按天花板成交 2 份
    auto decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 2);
    CHECK(decision.plan->total == std::int64_t{20});

    // 不允许：整笔拒绝，别缩水成交
    request.allowPartial = false;
    CHECK_FALSE(planTrade(request).ok());
}

TEST_CASE("份数天花板：不额外限制时与三条上限的最小值一致") {
    CHECK(maxTradeUnits(100, 5, 4, 10, 1000) == 4);
    CHECK(maxTradeUnits(2, 5, 4, 10, 1000) == 2);
    CHECK(maxTradeUnits(100, 5, 4, 10, 35) == 3);
    CHECK(maxTradeUnits(100, 0, 4, 10, 1000) == 0);
    CHECK(maxTradeUnits(0, 5, 4, 10, 1000) == 0);
    CHECK(maxTradeUnits(100, 5, 4, 0, 0) == 4); // 免费店：钱不是限制
}

TEST_CASE("卖一份：份数、件数、收入三者一致") {
    auto const decision = planTrade(soundSell());

    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 1);
    CHECK(decision.plan->items == 64);
    CHECK(decision.plan->total == std::int64_t{5});
}

TEST_CASE("卖：手里没有这种物品就整笔拒绝") {
    auto request       = soundSell();
    request.goodsUnits = 0;
    CHECK(planTrade(request).rejection == TradeRejection::NotEnoughGoods);
}

TEST_CASE("卖：店主资金不足时按部分成交，金额按实际份数重算") {
    auto request       = soundSell();
    request.requested  = 4;
    request.payerFunds = 25; // 只付得起 2 份（一份 10 元的店）
    request.unitPrice  = 10;
    request.goodsUnits = 10;
    request.roomUnits  = 10;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 2);
    CHECK(decision.plan->items == 128);
    CHECK(decision.plan->total == std::int64_t{20});
}

TEST_CASE("卖：店主真的 0 元时报缺钱，与「读不到店主余额」无关") {
    auto request       = soundSell();
    request.unitPrice  = 10;
    request.payerFunds = 0;
    CHECK(planTrade(request).rejection == TradeRejection::NotEnoughFunds);
}

TEST_CASE("卖：商店箱装不下时按实际能装下的份数成交") {
    auto request       = soundSell();
    request.requested  = 4;
    request.unitPrice  = 10;
    request.payerFunds = 1000;
    request.goodsUnits = 10;
    request.roomUnits  = 1;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 1);
    CHECK(decision.plan->items == 64);
    CHECK(decision.plan->total == std::int64_t{10});
}

TEST_CASE("关掉部分成交时，卖出的三种不够要分开报") {
    auto request         = soundSell();
    request.requested    = 4;
    request.unitPrice    = 10;
    request.payerFunds   = 1000;
    request.goodsUnits   = 10;
    request.roomUnits    = 10;
    request.allowPartial = false;

    auto shortGoods       = request;
    shortGoods.goodsUnits = 2;
    CHECK(planTrade(shortGoods).rejection == TradeRejection::NotEnoughGoods);

    auto shortFunds       = request;
    shortFunds.payerFunds = 35;
    CHECK(planTrade(shortFunds).rejection == TradeRejection::NotEnoughFunds);

    auto shortRoom      = request;
    shortRoom.roomUnits = 2;
    CHECK(planTrade(shortRoom).rejection == TradeRejection::NoRoomToReceive);

    auto overCap     = request;
    overCap.maxUnits = 2;
    CHECK(planTrade(overCap).rejection == TradeRejection::OverUnitLimit);
}

TEST_CASE("同一个拒绝原因在两个方向上的说法不一样") {
    CHECK(tradeRejectionMessage(TradeDirection::Buy, TradeRejection::NotEnoughGoods) == "商店的货不够");
    CHECK(tradeRejectionMessage(TradeDirection::Sell, TradeRejection::NotEnoughGoods) == "你手里没有这么多");
    CHECK(tradeRejectionMessage(TradeDirection::Buy, TradeRejection::NotEnoughFunds) == "你的钱不够");
    CHECK(tradeRejectionMessage(TradeDirection::Sell, TradeRejection::NotEnoughFunds) == "店主的钱不够，付不起这一笔");
    CHECK(tradeRejectionMessage(TradeDirection::Buy, TradeRejection::NoRoomToReceive) == "背包放不下这些物品");
    CHECK(tradeRejectionMessage(TradeDirection::Sell, TradeRejection::NoRoomToReceive) == "商店箱装不下这些物品");
    CHECK(tradeRejectionMessage(TradeDirection::Buy, TradeRejection::OverUnitLimit).find("买") != std::string::npos);
    CHECK(tradeRejectionMessage(TradeDirection::Sell, TradeRejection::OverUnitLimit).find("卖") != std::string::npos);
    // 店主离线的说法两边一样：那不是份数问题
    CHECK(
        tradeRejectionMessage(TradeDirection::Buy, TradeRejection::OwnerOffline)
        == tradeRejectionMessage(TradeDirection::Sell, TradeRejection::OwnerOffline)
    );
    CHECK(std::string{directionVerb(TradeDirection::Buy)} == "买");
    CHECK(std::string{directionVerb(TradeDirection::Sell)} == "卖");
}

TEST_CASE("卖：件数溢出或输入非法时拒绝，不给出回绕的件数") {
    auto request       = soundSell();
    request.requested  = std::numeric_limits<int>::max();
    request.goodsUnits = std::numeric_limits<int>::max();
    request.roomUnits  = std::numeric_limits<int>::max();
    request.payerFunds = std::numeric_limits<std::int64_t>::max();

    CHECK(planTrade(request).rejection == TradeRejection::InvalidRequest);

    auto noAmount          = soundSell();
    noAmount.amountPerUnit = 0;
    CHECK(planTrade(noAmount).rejection == TradeRejection::InvalidRequest);
}

TEST_CASE("requireOwnerOnline=false：记分板不再因店主离线提前拒绝") {
    auto request                   = soundBuy();
    request.ownerOnline            = false;
    request.backendSupportsOffline = false;

    request.requireOwnerOnline = true;
    CHECK(planTrade(request).rejection == TradeRejection::OwnerOffline);

    // 关掉之后不再提前拦：这一笔会走到动钱那一步，由「读不到账户」失败
    request.requireOwnerOnline = false;
    CHECK(planTrade(request).ok());
}

TEST_CASE("requireOwnerOnline 对支持离线的后端没有意义") {
    for (bool requireOwnerOnline : {true, false}) {
        auto request                   = soundBuy();
        request.ownerOnline            = false;
        request.backendSupportsOffline = true;
        request.requireOwnerOnline     = requireOwnerOnline;

        auto const decision = planTrade(request);
        REQUIRE(decision.ok());
        CHECK(decision.plan->units == 1);
    }
}

TEST_CASE("税字段由方向与后端能力决定（mod 层不再自己写 direction == Sell）") {
    // 支持销毁（LegacyMoney）：两个方向都要收，税只压在卖出方向的店主身上
    auto const buy = tradeTaxPlan(TradeDirection::Buy, 0.1, /*backendSupportsMoneySink=*/true, /*adminShop=*/false);
    CHECK(buy.taxRate == doctest::Approx(0.1));
    CHECK_FALSE(buy.taxOnPayer);

    auto const sell = tradeTaxPlan(TradeDirection::Sell, 0.1, true, /*adminShop=*/false);
    CHECK(sell.taxRate == doctest::Approx(0.1));
    CHECK(sell.taxOnPayer);

    // 收不走（记分板）：整笔不算，两个方向都归零
    for (auto direction : {TradeDirection::Buy, TradeDirection::Sell}) {
        auto const none = tradeTaxPlan(direction, 0.1, /*backendSupportsMoneySink=*/false, /*adminShop=*/false);
        CHECK(none.taxRate == 0.0);
        CHECK_FALSE(none.taxOnPayer);
    }

    // 没开税：也归零
    auto const off = tradeTaxPlan(TradeDirection::Sell, 0.0, true, /*adminShop=*/false);
    CHECK(off.taxRate == 0.0);
    CHECK_FALSE(off.taxOnPayer);

    // 管理员店：对手方是系统账户，没有可抽的成交额（DESIGN §7.4），两个方向都归零
    for (auto direction : {TradeDirection::Buy, TradeDirection::Sell}) {
        auto const admin = tradeTaxPlan(direction, 0.1, /*backendSupportsMoneySink=*/true, /*adminShop=*/true);
        CHECK(admin.taxRate == 0.0);
        CHECK_FALSE(admin.taxOnPayer);
    }
}

TEST_CASE("付款方含税成本率：只有税压在付款方身上时才是税率") {
    auto request  = soundBuy();
    request.taxRate    = 0.1;
    request.taxOnPayer = false;
    CHECK(payerTaxRate(request) == 0.0); // 买家只付牌面总额

    request.taxOnPayer = true;
    CHECK(payerTaxRate(request) == doctest::Approx(0.1));
}

TEST_CASE("成交方案带出商店税：总额 × 税率向下取整") {
    // 买入方向也要算税。注意这条用例只覆盖 `planTrade`（需要 `request.taxRate` 非 0），
    // 「谁往 request 里填税率」由 `tradeTaxPlan` 那条用例钉住（`prepareCommon` 在 mod 层，测不到）。
    auto request      = soundBuy();
    request.requested = 3;
    request.taxRate   = 0.1;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->total == std::int64_t{30});
    CHECK(decision.plan->tax == std::int64_t{3});
}

TEST_CASE("税压在店主身上（买入）：买家的份数上限不受税影响") {
    auto request      = soundBuy();
    request.requested = 4;
    request.unitPrice = 10;
    request.payerFunds = 40; // 正好够 4 份牌面金额
    request.goodsUnits = 4;
    request.roomUnits  = 4;
    request.taxRate    = 0.1;
    request.taxOnPayer = false;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 4); // 买家仍只付 40
    CHECK(decision.plan->tax == std::int64_t{4});
}

TEST_CASE("税压在付款方身上（卖出）：份数上限含税") {
    auto request       = soundSell();
    request.unitPrice  = 10;
    request.payerFunds = 40; // 店主只够 3 份含税成本（33），不够 4 份（44）
    request.goodsUnits = 4;
    request.roomUnits  = 4;
    request.requested  = 4;
    request.taxRate    = 0.1;
    request.taxOnPayer = true;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 3);
    CHECK(decision.plan->total == std::int64_t{30});
    CHECK(decision.plan->tax == std::int64_t{3});
}

TEST_CASE("税为 0 时方案里的税也是 0（不产生第二笔转账）") {
    auto request      = soundBuy();
    request.requested = 3;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->tax == std::int64_t{0});
}

TEST_CASE("卖出方向：份数上限按含税成本算，店主不会多卖一份再失败") {
    auto request        = soundSell();
    request.unitPrice   = 10;
    request.payerFunds  = 100; // 店主余额
    request.goodsUnits  = 10;
    request.roomUnits   = 10;
    request.requested   = 10;
    request.taxRate     = 0.1; // 一份含税 11，100 块只够 9 份
    request.taxOnPayer  = true;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 9);
    CHECK(decision.plan->total == std::int64_t{90});
    CHECK(decision.plan->tax == std::int64_t{9});
}

TEST_CASE("停业：closed 压过所有上限与输入校验，返回 ShopClosed") {
    auto request          = soundBuy();
    request.closed        = true;
    request.goodsUnits    = 0; // 缺货
    request.roomUnits     = 0; // 背包满
    request.payerFunds    = 0; // 没钱
    request.amountPerUnit = 64;

    auto const decision = planTrade(request);
    CHECK_FALSE(decision.ok());
    CHECK(decision.rejection == TradeRejection::ShopClosed);

    // 连同「输入本身不合法」也压过：停业是整家店的状态，不是玩家填错了什么
    auto broken          = request;
    broken.amountPerUnit = 0;
    CHECK(planTrade(broken).rejection == TradeRejection::ShopClosed);
}

TEST_CASE("停业：卖出方向同样拒绝") {
    auto request   = soundSell();
    request.closed = true;
    CHECK(planTrade(request).rejection == TradeRejection::ShopClosed);
}

TEST_CASE("停业：恢复营业后一切照旧") {
    auto request   = soundBuy();
    request.closed = false;

    auto const decision = planTrade(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 1);
}

TEST_CASE("停业：文案是「这家店暂停营业」，两个方向同一句") {
    CHECK(tradeRejectionMessage(TradeDirection::Buy, TradeRejection::ShopClosed) == "这家店暂停营业");
    CHECK(tradeRejectionMessage(TradeDirection::Sell, TradeRejection::ShopClosed) == "这家店暂停营业");
}

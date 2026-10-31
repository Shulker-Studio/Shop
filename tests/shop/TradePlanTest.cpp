#include "shop/TradePlan.h"

#include <doctest/doctest.h>
#include <limits>

namespace {

using namespace ss::shop;

/// 一价 10、一份 64 件、余额充足、商店箱够货、背包放得下的正常买入。
BuyRequest soundRequest() {
    BuyRequest request;
    request.requested     = 1;
    request.amountPerUnit = 64;
    request.unitPrice     = 10;
    request.funds         = 100;
    request.stockUnits    = 5;
    request.backpackUnits = 5;
    request.allowPartial  = true;
    request.ownerOnline   = true;
    return request;
}

} // namespace

TEST_CASE("买一份：成交份数、件数、总价三者一致") {
    auto const decision = planBuy(soundRequest());

    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 1);
    CHECK(decision.plan->items == 64);
    CHECK(decision.plan->total == std::int64_t{10});
}

TEST_CASE("买多份：总价 = 单价 × 份数，恒为整数") {
    auto request      = soundRequest();
    request.requested = 4;
    request.funds     = 40;

    auto const decision = planBuy(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 4);
    CHECK(decision.plan->items == 256);
    CHECK(decision.plan->total == std::int64_t{40});
}

TEST_CASE("余额不足：整笔拒绝，不给出半份方案") {
    auto request  = soundRequest();
    request.funds = 5;

    auto const decision = planBuy(request);
    CHECK_FALSE(decision.ok());
    CHECK(decision.rejection == TradeRejection::NotEnoughMoney);
}

TEST_CASE("箱子空了：报缺货，不报缺钱") {
    auto request       = soundRequest();
    request.stockUnits = 0;
    request.funds      = 0;

    auto const decision = planBuy(request);
    CHECK_FALSE(decision.ok());
    CHECK(decision.rejection == TradeRejection::NotEnoughStock);
}

TEST_CASE("背包放不下：报背包满") {
    auto request          = soundRequest();
    request.backpackUnits = 0;

    auto const decision = planBuy(request);
    CHECK_FALSE(decision.ok());
    CHECK(decision.rejection == TradeRejection::BackpackFull);
}

TEST_CASE("部分成交：箱子只够 2 份时按 2 份成交，总价按 2 份重算") {
    auto request       = soundRequest();
    request.requested  = 4;
    request.funds      = 100;
    request.stockUnits = 2;

    auto const decision = planBuy(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 2);
    CHECK(decision.plan->items == 128);
    CHECK(decision.plan->total == std::int64_t{20});
}

TEST_CASE("部分成交：余额只够 3 份时按 3 份成交") {
    auto request      = soundRequest();
    request.requested = 4;
    request.funds     = 35;

    auto const decision = planBuy(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 3);
    CHECK(decision.plan->total == std::int64_t{30});
}

TEST_CASE("部分成交：背包只放得下 1 份时按 1 份成交") {
    auto request          = soundRequest();
    request.requested     = 4;
    request.backpackUnits = 1;

    auto const decision = planBuy(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 1);
    CHECK(decision.plan->items == 64);
}

TEST_CASE("关掉部分成交：箱子不够请求量就整笔拒绝") {
    auto request         = soundRequest();
    request.requested    = 4;
    request.stockUnits   = 2;
    request.allowPartial = false;

    auto const decision = planBuy(request);
    CHECK_FALSE(decision.ok());
    CHECK(decision.rejection == TradeRejection::NotEnoughStock);
}

TEST_CASE("关掉部分成交：钱只够一部分时整笔拒绝") {
    auto request         = soundRequest();
    request.requested    = 4;
    request.funds        = 35;
    request.allowPartial = false;

    auto const decision = planBuy(request);
    CHECK_FALSE(decision.ok());
    CHECK(decision.rejection == TradeRejection::NotEnoughMoney);
}

TEST_CASE("关掉部分成交：刚好够时不误伤") {
    auto request          = soundRequest();
    request.requested     = 2;
    request.funds         = 20;
    request.stockUnits    = 2;
    request.backpackUnits = 2;
    request.allowPartial  = false;

    auto const decision = planBuy(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 2);
    CHECK(decision.plan->total == std::int64_t{20});
}

TEST_CASE("店主离线：后端不支持离线记账就整笔拒绝") {
    auto request        = soundRequest();
    request.ownerOnline = false;

    auto const decision = planBuy(request);
    CHECK_FALSE(decision.ok());
    CHECK(decision.rejection == TradeRejection::OwnerOffline);
}

TEST_CASE("店主离线：后端支持离线记账照常成交") {
    auto request                   = soundRequest();
    request.ownerOnline            = false;
    request.backendSupportsOffline = true;

    auto const decision = planBuy(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 1);
}

TEST_CASE("免费商店：钱不是限制，总价为 0") {
    auto request      = soundRequest();
    request.unitPrice = 0;
    request.funds     = 0;

    auto const decision = planBuy(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->total == std::int64_t{0});
}

TEST_CASE("非法输入：请求份数、份价、每份件数都必须合法") {
    auto request      = soundRequest();
    request.requested = 0;
    CHECK(planBuy(request).rejection == TradeRejection::InvalidRequest);

    request           = soundRequest();
    request.unitPrice = -1;
    CHECK(planBuy(request).rejection == TradeRejection::InvalidRequest);

    auto noAmount          = soundRequest();
    noAmount.amountPerUnit = 0;
    CHECK(planBuy(noAmount).rejection == TradeRejection::InvalidRequest);

    noAmount.amountPerUnit = -64;
    CHECK(planBuy(noAmount).rejection == TradeRejection::InvalidRequest);
}

TEST_CASE("件数溢出 int 时拒绝，不给出回绕的件数") {
    auto request          = soundRequest();
    request.requested     = std::numeric_limits<int>::max();
    request.stockUnits    = std::numeric_limits<int>::max();
    request.backpackUnits = std::numeric_limits<int>::max();
    request.funds         = std::numeric_limits<std::int64_t>::max();

    auto const decision = planBuy(request);
    CHECK_FALSE(decision.ok());
    CHECK(decision.rejection == TradeRejection::InvalidRequest);
}

TEST_CASE("每条拒绝原因都有玩家看得懂的提示") {
    CHECK_FALSE(buyRejectionMessage(TradeRejection::NotEnoughStock).empty());
    CHECK_FALSE(buyRejectionMessage(TradeRejection::NotEnoughMoney).empty());
    CHECK_FALSE(buyRejectionMessage(TradeRejection::BackpackFull).empty());
    CHECK_FALSE(buyRejectionMessage(TradeRejection::OwnerOffline).empty());
    CHECK_FALSE(buyRejectionMessage(TradeRejection::InvalidRequest).empty());
    CHECK(buyRejectionMessage(TradeRejection::None).empty());
}

TEST_CASE("份数天花板：maxShopAmount 也进上限（DESIGN §10.1 第 3 步）") {
    auto request      = soundRequest();
    request.requested = 10;
    request.maxUnits  = 2;

    // 允许部分成交：按天花板成交 2 份
    auto decision = planBuy(request);
    REQUIRE(decision.ok());
    CHECK(decision.plan->units == 2);
    CHECK(decision.plan->total == std::int64_t{20});

    // 不允许：整笔拒绝，别缩水成交
    request.allowPartial = false;
    CHECK_FALSE(planBuy(request).ok());
}

TEST_CASE("份数天花板：不额外限制时与可成交份数/余额/背包的最小值一致") {
    CHECK(maxBuyUnits(100, 5, 4, 10, 1000) == 4);
    CHECK(maxBuyUnits(2, 5, 4, 10, 1000) == 2);
    CHECK(maxBuyUnits(100, 5, 4, 10, 35) == 3);
    CHECK(maxBuyUnits(100, 0, 4, 10, 1000) == 0);
    CHECK(maxBuyUnits(0, 5, 4, 10, 1000) == 0);
    CHECK(maxBuyUnits(100, 5, 4, 0, 0) == 4); // 免费店：钱不是限制
}

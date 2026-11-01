#include "shop/TradePlan.h"

#include "shop/Economy.h"
#include "shop/ShopInfo.h"
#include "shop/TradePricing.h"

#include <algorithm>
#include <limits>

namespace ss::shop {
namespace {

/// 三条上限里哪一条把成交份数压到了请求量以下。
///
/// 顺序即优先级：缺货优先（箱子空了跟买家有多少钱无关），最后才轮到单次上限——
/// 那一条是服务器配置，不是玩家或这家店的问题。
TradeRejection blockingReason(TradeRequest const& request, int units, int affordableUnits) {
    if (request.goodsUnits < units) {
        return TradeRejection::NotEnoughGoods;
    }
    if (affordableUnits < units) {
        return TradeRejection::NotEnoughFunds;
    }
    if (request.roomUnits < units) {
        return TradeRejection::NoRoomToReceive;
    }
    return TradeRejection::OverUnitLimit;
}

/// 后端不能给离线账户记账时，店主不在线整笔拒绝：钱没地方进（买入）、
/// 也没地方出（卖出）（DESIGN §8.5）。判据与表单藏按钮共用 `economy::ownerUnreachable`。
bool ownerOfflineHere(TradeRequest const& request) {
    return economy::ownerUnreachable(request.ownerOnline, request.backendSupportsOffline, request.requireOwnerOnline);
}

} // namespace

std::string_view directionVerb(TradeDirection direction) {
    return direction == TradeDirection::Sell ? "卖" : "买";
}

TradeTaxPlan tradeTaxPlan(
    TradeDirection direction,
    double         configuredRate,
    bool           backendSupportsMoneySink,
    bool           adminShop
) {
    if (adminShop || !backendSupportsMoneySink || !(configuredRate > 0.0)) {
        // 管理员店（对手方是系统账户）、收不走的后端（没有系统账户的税）、或本来就没开税：
        // 两个字段都归零，第二笔整步跳过。
        return {};
    }
    return {configuredRate, direction == TradeDirection::Sell};
}

double payerTaxRate(TradeRequest const& request) { return request.taxOnPayer ? request.taxRate : 0.0; }

int maxTradeUnits(
    int          maxUnits,
    int          goodsUnits,
    int          roomUnits,
    std::int64_t unitPrice,
    std::int64_t payerFunds,
    double       taxRate
) {
    if (maxUnits <= 0) {
        return 0;
    }
    return maxTradeUnits(maxUnits, goodsUnits, roomUnits, affordableUnitsWithTax(unitPrice, payerFunds, taxRate));
}

int maxTradeUnits(int maxUnits, int goodsUnits, int roomUnits, int affordableUnits) {
    if (maxUnits <= 0) {
        return 0;
    }
    return std::min(maxUnits, tradeUnits(std::numeric_limits<int>::max(), goodsUnits, roomUnits, affordableUnits));
}

TradeDecision planTrade(TradeRequest const& request) {
    if (request.requested <= 0 || request.unitPrice < 0 || request.amountPerUnit <= 0) {
        return {.rejection = TradeRejection::InvalidRequest};
    }

    // 先报输入非法，再报「这家店现在能不能交易」：离线是整家店的属性，压过份数不足。
    if (ownerOfflineHere(request)) {
        return {.rejection = TradeRejection::OwnerOffline};
    }

    // 税额本身永远按 `taxRate` 算；但只有「税要压在付款方身上」时（卖出方向的店主）
    // 才把税算进他的份数上限——买入方向买家仍只付牌面总额。
    int const affordable = affordableUnitsWithTax(request.unitPrice, request.payerFunds, payerTaxRate(request));
    int const    allowed = maxTradeUnits(request.maxUnits, request.goodsUnits, request.roomUnits, affordable);

    if (allowed <= 0) {
        // 谁挡住了就报谁：缺货优先，因为箱子/背包空了跟钱多钱少无关。
        if (request.goodsUnits <= 0) {
            return {.rejection = TradeRejection::NotEnoughGoods};
        }
        if (affordable <= 0) {
            return {.rejection = TradeRejection::NotEnoughFunds};
        }
        if (request.roomUnits <= 0) {
            return {.rejection = TradeRejection::NoRoomToReceive};
        }
        // 只剩「份数天花板 <= 0」这一种：配置非法，不是玩家的问题。
        return {.rejection = TradeRejection::InvalidRequest};
    }

    int const units = std::min(request.requested, allowed);

    // 关掉部分成交时，能成交的份数少于请求量就整笔拒绝（DESIGN §10.3）。
    // 拒绝原因要说清是哪一条不够：「货不够」「余额不够」「装不下」是三件事。
    if (!request.allowPartial && units < request.requested) {
        return {.rejection = blockingReason(request, request.requested, affordable)};
    }

    // 件数与金额都按实际成交份数重算，中间不出现小数（DESIGN §10.1 第 3 条）。
    std::int64_t const items = std::int64_t{units} * request.amountPerUnit;
    if (items > std::numeric_limits<int>::max()) {
        return {.rejection = TradeRejection::InvalidRequest};
    }
    auto const total = totalPrice(request.unitPrice, units);
    if (!total) {
        return {.rejection = TradeRejection::InvalidRequest};
    }

    return {.plan = TradePlan{units, static_cast<int>(items), *total, shopTax(*total, request.taxRate)}};
}

std::string tradeRejectionMessage(TradeDirection direction, TradeRejection rejection) {
    bool const selling = direction == TradeDirection::Sell;
    switch (rejection) {
    case TradeRejection::OwnerOffline:
        return ownerOfflineReason();
    case TradeRejection::NotEnoughGoods:
        return selling ? "你手里没有这么多" : "商店的货不够";
    case TradeRejection::NotEnoughFunds:
        return selling ? "店主的钱不够，付不起这一笔" : "你的钱不够";
    case TradeRejection::NoRoomToReceive:
        return selling ? "商店箱装不下这些物品" : "背包放不下这些物品";
    case TradeRejection::OverUnitLimit:
        return "一次" + std::string{directionVerb(direction)} + "的份数超过了服务器允许的单次上限";
    case TradeRejection::InvalidRequest:
        return "这笔交易不成立（份数或价格不合法）";
    case TradeRejection::None:
        break;
    }
    return {};
}

} // namespace ss::shop

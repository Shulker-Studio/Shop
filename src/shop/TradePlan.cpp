#include "shop/TradePlan.h"

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
TradeLimit blockingLimit(TradeLimits const& limits, int units) {
    if (limits.goodsUnits < units) {
        return TradeLimit::Goods;
    }
    if (affordableUnits(limits.unitPrice, limits.payerFunds) < units) {
        return TradeLimit::Funds;
    }
    if (limits.roomUnits < units) {
        return TradeLimit::Room;
    }
    return TradeLimit::Cap;
}

/// 交易被拒 → 拒绝原因。买卖两边只有这一步的映射不同（说法见
/// `tradeRejectionMessage`），所以映射表只写一遍。
TradeRejection rejectionOf(TradeLimit limit) {
    switch (limit) {
    case TradeLimit::Invalid:
        return TradeRejection::InvalidRequest;
    case TradeLimit::Goods:
        return TradeRejection::NotEnoughGoods;
    case TradeLimit::Funds:
        return TradeRejection::NotEnoughFunds;
    case TradeLimit::Room:
        return TradeRejection::NoRoomToReceive;
    case TradeLimit::Cap:
        return TradeRejection::OverUnitLimit;
    case TradeLimit::None:
        break;
    }
    return TradeRejection::InvalidRequest;
}

/// 后端不能给离线账户记账时，店主不在线整笔拒绝：钱没地方进（买入）、
/// 也没地方出（卖出）（DESIGN §8.5）。支持离线的后端（P1 的 LegacyMoney / builtin）不看这个标记。
bool ownerUnreachable(bool ownerOnline, bool backendSupportsOffline) { return !backendSupportsOffline && !ownerOnline; }

} // namespace

int maxTradeUnits(int maxUnits, int goodsUnits, int roomUnits, std::int64_t unitPrice, std::int64_t payerFunds) {
    if (maxUnits <= 0) {
        return 0;
    }
    int const affordable = affordableUnits(unitPrice, payerFunds);
    return std::min(maxUnits, tradeUnits(std::numeric_limits<int>::max(), goodsUnits, roomUnits, affordable));
}

TradeAllowance tradeAllowance(TradeLimits const& limits) {
    if (limits.requested <= 0 || limits.unitPrice < 0 || limits.amountPerUnit <= 0) {
        return {.blockedBy = TradeLimit::Invalid};
    }

    int const allowed =
        maxTradeUnits(limits.maxUnits, limits.goodsUnits, limits.roomUnits, limits.unitPrice, limits.payerFunds);

    if (allowed <= 0) {
        // 谁挡住了就报谁：缺货优先，因为箱子/背包空了跟钱多钱少无关。
        if (limits.goodsUnits <= 0) {
            return {.blockedBy = TradeLimit::Goods};
        }
        if (affordableUnits(limits.unitPrice, limits.payerFunds) <= 0) {
            return {.blockedBy = TradeLimit::Funds};
        }
        if (limits.roomUnits <= 0) {
            return {.blockedBy = TradeLimit::Room};
        }
        // 只剩「份数天花板 <= 0」这一种：配置非法，不是玩家的问题。
        return {.blockedBy = TradeLimit::Invalid};
    }

    int const units = std::min(limits.requested, allowed);

    // 关掉部分成交时，能成交的份数少于请求量就整笔拒绝（DESIGN §10.3）。
    // 拒绝原因要说清是哪一条不够：「箱子装不下」「余额不够」「货不够」是三件事。
    if (!limits.allowPartial && units < limits.requested) {
        return {.blockedBy = blockingLimit(limits, limits.requested)};
    }

    // 件数与总价都按实际成交份数重算，中间不出现小数（DESIGN §10.1 第 3 条）。
    std::int64_t const items = std::int64_t{units} * limits.amountPerUnit;
    if (items > std::numeric_limits<int>::max()) {
        return {.blockedBy = TradeLimit::Invalid};
    }
    auto const total = totalPrice(limits.unitPrice, units);
    if (!total) {
        return {.blockedBy = TradeLimit::Invalid};
    }

    return {
        .plan = TradePlan{units, static_cast<int>(items), *total}
    };
}

TradeDecision planBuy(BuyRequest const& request) {
    auto const allowance = tradeAllowance(
        TradeLimits{
            .requested     = request.requested,
            .amountPerUnit = request.amountPerUnit,
            .unitPrice     = request.unitPrice,
            .goodsUnits    = request.stockUnits,
            .roomUnits     = request.backpackUnits,
            .payerFunds    = request.funds,
            .maxUnits      = request.maxUnits,
            .allowPartial  = request.allowPartial,
        }
    );
    // 输入本身就不成立时先报输入，再轮到「这家店现在能不能交易」。
    if (allowance.blockedBy == TradeLimit::Invalid) {
        return {.rejection = TradeRejection::InvalidRequest};
    }
    if (ownerUnreachable(request.ownerOnline, request.backendSupportsOffline)) {
        return {.rejection = TradeRejection::OwnerOffline};
    }
    if (!allowance.ok()) {
        return {.rejection = rejectionOf(allowance.blockedBy)};
    }
    return {.plan = allowance.plan};
}

TradeDecision planSell(SellRequest const& request) {
    auto const allowance = tradeAllowance(
        TradeLimits{
            .requested     = request.requested,
            .amountPerUnit = request.amountPerUnit,
            .unitPrice     = request.unitPrice,
            .goodsUnits    = request.playerUnits,
            .roomUnits     = request.boxUnits,
            .payerFunds    = request.ownerFunds,
            .maxUnits      = request.maxUnits,
            .allowPartial  = request.allowPartial,
        }
    );
    if (allowance.blockedBy == TradeLimit::Invalid) {
        return {.rejection = TradeRejection::InvalidRequest};
    }
    if (ownerUnreachable(request.ownerOnline, request.backendSupportsOffline)) {
        return {.rejection = TradeRejection::OwnerOffline};
    }
    if (!allowance.ok()) {
        return {.rejection = rejectionOf(allowance.blockedBy)};
    }
    return {.plan = allowance.plan};
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
        return selling ? "一次卖的份数超过了服务器允许的单次上限" : "一次买的份数超过了服务器允许的单次上限";
    case TradeRejection::InvalidRequest:
        return "这笔交易不成立（份数或价格不合法）";
    case TradeRejection::None:
        break;
    }
    return {};
}

} // namespace ss::shop

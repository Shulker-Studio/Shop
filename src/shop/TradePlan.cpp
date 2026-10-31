#include "shop/TradePlan.h"

#include "shop/ShopInfo.h"
#include "shop/TradePricing.h"

#include <algorithm>
#include <limits>

namespace ss::shop {

int maxBuyUnits(int maxUnits, int stockUnits, int backpackUnits, std::int64_t unitPrice, std::int64_t funds) {
    if (maxUnits <= 0) {
        return 0;
    }
    int const affordable = affordableUnits(unitPrice, funds);
    return std::min(maxUnits, tradeUnits(std::numeric_limits<int>::max(), stockUnits, backpackUnits, affordable));
}

BuyDecision planBuy(BuyRequest const& request) {
    if (request.requested <= 0 || request.unitPrice < 0 || request.amountPerUnit <= 0) {
        return {.rejection = TradeRejection::InvalidRequest};
    }

    // 后端不能给离线账户记账时，店主不在线整笔拒绝：钱没地方进（DESIGN §8.5）。
    // 支持离线的后端（P1 的 LegacyMoney / builtin）不看这个标记。
    if (!request.backendSupportsOffline && !request.ownerOnline) {
        return {.rejection = TradeRejection::OwnerOffline};
    }

    int const allowed =
        maxBuyUnits(request.maxUnits, request.stockUnits, request.backpackUnits, request.unitPrice, request.funds);

    if (allowed <= 0) {
        // 谁挡住了就报谁：缺货优先，因为箱子空了跟买家有多少钱无关。
        if (request.stockUnits <= 0) {
            return {.rejection = TradeRejection::NotEnoughStock};
        }
        if (affordableUnits(request.unitPrice, request.funds) <= 0) {
            return {.rejection = TradeRejection::NotEnoughMoney};
        }
        if (request.backpackUnits <= 0) {
            return {.rejection = TradeRejection::BackpackFull};
        }
        // 只剩「份数天花板 <= 0」这一种：配置非法，不是玩家的问题。
        return {.rejection = TradeRejection::InvalidRequest};
    }

    int const units = std::min(request.requested, allowed);

    // 关掉部分成交时，能成交的份数少于请求量就整笔拒绝（DESIGN §10.3）。
    if (!request.allowPartial && units < request.requested) {
        if (request.stockUnits < request.requested) {
            return {.rejection = TradeRejection::NotEnoughStock};
        }
        if (affordableUnits(request.unitPrice, request.funds) < request.requested) {
            return {.rejection = TradeRejection::NotEnoughMoney};
        }
        if (request.backpackUnits < request.requested) {
            return {.rejection = TradeRejection::BackpackFull};
        }
        // 三条都不是瓶颈，那就是撞到了 `maxShopAmount` 这条单次上限。
        return {.rejection = TradeRejection::OverUnitLimit};
    }

    // 件数与总价都按实际成交份数重算，中间不出现小数（DESIGN §10.1 第 3 条）。
    std::int64_t const items = std::int64_t{units} * request.amountPerUnit;
    if (items > std::numeric_limits<int>::max()) {
        return {.rejection = TradeRejection::InvalidRequest};
    }
    auto const total = totalPrice(request.unitPrice, units);
    if (!total) {
        return {.rejection = TradeRejection::InvalidRequest};
    }

    return {
        .plan = BuyPlan{units, static_cast<int>(items), *total}
    };
}

std::string buyRejectionMessage(TradeRejection rejection) {
    switch (rejection) {
    case TradeRejection::OwnerOffline:
        return ownerOfflineReason();
    case TradeRejection::NotEnoughStock:
        return "商店的货不够";
    case TradeRejection::NotEnoughMoney:
        return "你的钱不够";
    case TradeRejection::OverUnitLimit:
        return "一次买的份数超过了服务器允许的单次上限";
    case TradeRejection::BackpackFull:
        return "背包放不下这些物品";
    case TradeRejection::InvalidRequest:
        return "这笔交易不成立（份数或价格不合法）";
    case TradeRejection::None:
        break;
    }
    return {};
}

} // namespace ss::shop

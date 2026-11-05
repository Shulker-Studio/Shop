#include "core/shop/ShopInfoData.h"

namespace ss::shop {

std::optional<std::int64_t> unitPriceOf(ShopInfoData const& data, TradeDirection direction) {
    return direction == TradeDirection::Sell ? data.sellPrice : data.buyPrice;
}

} // namespace ss::shop

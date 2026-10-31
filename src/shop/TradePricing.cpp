#include "shop/TradePricing.h"

#include <algorithm>
#include <limits>

namespace ss::shop {

std::optional<std::int64_t> totalPrice(std::int64_t unitPrice, int units) {
    if (unitPrice < 0 || units < 0) {
        return std::nullopt;
    }
    if (units == 0 || unitPrice == 0) {
        return std::int64_t{0};
    }
    if (unitPrice > std::numeric_limits<std::int64_t>::max() / units) {
        return std::nullopt;
    }
    return unitPrice * units;
}

int affordableUnits(std::int64_t unitPrice, std::int64_t funds) {
    if (unitPrice <= 0) {
        // 免费商店：钱不是限制
        return std::numeric_limits<int>::max();
    }
    if (funds <= 0) {
        return 0;
    }
    const std::int64_t units = funds / unitPrice;
    return static_cast<int>(std::min<std::int64_t>(units, std::numeric_limits<int>::max()));
}

int tradeUnits(int requested, int stockUnits, int backpackUnits, int affordable) {
    if (requested <= 0) {
        return 0;
    }
    const int units = std::min({requested, stockUnits, backpackUnits, affordable});
    return std::max(units, 0);
}

int unitsFromItems(int itemCount, int amountPerUnit) {
    if (itemCount <= 0 || amountPerUnit <= 0) {
        return 0;
    }
    return itemCount / amountPerUnit;
}

} // namespace ss::shop

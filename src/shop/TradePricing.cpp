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

std::int64_t shopTax(std::int64_t total, double rate) {
    // `!(rate > 0.0)` 一并吞掉 NaN 与负数：没有税，也不产生负数税。
    if (total <= 0 || !(rate > 0.0)) {
        return 0;
    }
    if (rate >= 1.0) {
        return total; // 全额征税：店主实收 0，但不会出现比成交额还大的税
    }
    double const raw = static_cast<double>(total) * rate;
    if (!(raw > 0.0)) {
        return 0;
    }
    if (raw >= static_cast<double>(total)) {
        return total;
    }
    // `total` 与 `rate` 都是正的，`raw` 落在 (0, total) 里，截断即向下取整。
    return static_cast<std::int64_t>(raw);
}

int affordableUnitsWithTax(std::int64_t unitPrice, std::int64_t funds, double rate) {
    if (!(rate > 0.0)) {
        // 没税：与不含税的口径完全一致。
        return affordableUnits(unitPrice, funds);
    }
    if (unitPrice <= 0 || funds <= 0) {
        return affordableUnits(unitPrice, funds); // 免费店 / 没钱
    }
    // 含税后只会买得更少，所以不含税份数是一个安全上界，再二分到精确值。
    //
    // 不用「资金 ÷ (1 + 税率)」估计：double 的 1.1 是 1.10000000000000008…，
    // `110 / 1.1` 会落成 99.999…，截断后就少算了一份（店主明明付得起 10 份）。
    // 二分的谓词用的就是真正会收的 `shopTax`，所以预校验与收税不会掰成两个口径。
    int lo = 0;
    int hi = affordableUnits(unitPrice, funds);
    while (lo < hi) {
        int const mid   = lo + (hi - lo + 1) / 2;
        auto const total = totalPrice(unitPrice, mid);
        if (!total || *total > funds) {
            hi = mid - 1;
            continue;
        }
        // `total <= funds`，所以 `funds - total >= 0`：这样比不会溢出。
        std::int64_t const tax = shopTax(*total, rate);
        if (tax <= funds - *total) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

} // namespace ss::shop

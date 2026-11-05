#include "core/trade/UnitCount.h"

#include <algorithm>

namespace ss::shop {

UnitCount::UnitCount(int finiteUnits) : units(std::max(0, finiteUnits)), unlimited(false) {}

UnitCount UnitCount::makeFinite(int units) { return UnitCount{units}; }

UnitCount UnitCount::makeUnlimited() {
    UnitCount stock;
    stock.units.reset(); // 「无限」没有份数：两种真相不能同时成立
    stock.unlimited = true;
    return stock;
}

bool UnitCount::valid() const { return !(unlimited && units.has_value()); }

std::optional<int> UnitCount::finiteUnits() const {
    if (unlimited) {
        return std::nullopt;
    }
    return units;
}

bool UnitCount::covers(int wanted) const {
    if (unlimited) {
        return true;
    }
    return units.value_or(0) >= wanted;
}

bool UnitCount::empty() const { return !unlimited && units.value_or(0) <= 0; }

} // namespace ss::shop

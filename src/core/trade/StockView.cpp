#include "core/trade/StockView.h"

#include <algorithm>

namespace ss::shop {

StockView::StockView(int finiteUnits) : units(std::max(0, finiteUnits)), unlimited(false) {}

StockView StockView::makeFinite(int units) { return StockView{units}; }

StockView StockView::makeUnlimited() {
    StockView stock;
    stock.units.reset(); // 「无限」没有份数：两种真相不能同时成立
    stock.unlimited = true;
    return stock;
}

bool StockView::valid() const { return !(unlimited && units.has_value()); }

std::optional<int> StockView::finiteUnits() const {
    if (unlimited) {
        return std::nullopt;
    }
    return units;
}

bool StockView::covers(int wanted) const {
    if (unlimited) {
        return true;
    }
    return units.value_or(0) >= wanted;
}

bool StockView::empty() const { return !unlimited && units.value_or(0) <= 0; }

} // namespace ss::shop

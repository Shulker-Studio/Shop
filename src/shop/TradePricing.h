#pragma once

#include <cstdint>
#include <optional>

namespace ss::shop {

/// 总价 = 一份的价格 × 份数。
///
/// 份价为负、份数为负、或乘法溢出 `int64` 时返回 `std::nullopt`。
std::optional<std::int64_t> totalPrice(std::int64_t unitPrice, int units);

/// 资金能承受多少份（向下取整）。
///
/// 免费商店（份价 <= 0）不设上限，返回 `INT_MAX`。
int affordableUnits(std::int64_t unitPrice, std::int64_t funds);

/// 部分成交：在请求量与各上限之间取最小值。
///
/// 任一项 <= 0 时返回 0（视为无法成交）；上限传 `INT_MAX` 表示不限制。
int tradeUnits(int requested, int stockUnits, int backpackUnits, int affordable);

/// 件数 → 份数（向下取整）。商店箱的货与背包容量共用这一个算式，别在别处再除一遍。
///
/// 件数为负、或每份件数 <= 0 时返回 0，不抛错——牌面写了 0 件的商店只是不能成交。
int unitsFromItems(int itemCount, int amountPerUnit);

} // namespace ss::shop

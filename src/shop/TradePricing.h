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

} // namespace ss::shop

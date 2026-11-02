#pragma once

#include "shop/StockView.h"

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

/// 部分成交：在请求量与各上限之间取最小值（DESIGN §10.1 第 3 步 / §10.3）。
///
/// 「货」与「收货方空间」是 `StockView`：**无限那一项不参与 `min`**（管理员店的模板箱）。
/// 不参与的意思不是「传一个很大的数」，而是它根本不进 `min`——见 `StockView::finiteUnits`。
///
/// 请求量或付得起的份数 <= 0 时返回 0（视为无法成交）。
int tradeUnits(int requested, StockView stock, StockView room, int affordable);

/// 件数 → 份数（向下取整）。商店箱的货与背包容量共用这一个算式，别在别处再除一遍。
///
/// 件数为负、或每份件数 <= 0 时返回 0，不抛错——牌面写了 0 件的商店只是不能成交。
int unitsFromItems(int itemCount, int amountPerUnit);

/// 税率的步长：千分之一（0.001 = 0.1%）。
///
/// 配置里只接受这个网格上的值。更细的粒度在小额交易上本来就会被「税额下限 1」吃掉，
/// 只会让 `/shop status` 与流水里出现 `0.00049999` 这种没法读的数字。
inline constexpr double kTaxRateStep = 0.001;

/// 商店税（DESIGN §8.9）：`总额 × 税率`，**向下取整**，余数留给店主；
/// **但不足 1 时按 1 收**。
///
/// 下限 1 是为了堵住「拆单免税」：没有下限时，把一笔大单拆成若干笔小单就能把税拆没。
///
/// 税额永远落在 `[0, total]`：税率为 0 / 负数 / NaN、总额非正时是 0，税率 >= 1 时封顶到 total
/// （配置校验会拒掉这种值，这里只是不让它算出比成交额还大的税）。
std::int64_t shopTax(std::int64_t total, double rate);

/// 税率是否落在千分位网格上（`kTaxRateStep` 的整数倍）。
///
/// 只回答精度问题；越界（< 0 或 >= 1）由调用方的范围检查负责，这里一律返回 false。
bool isTaxRateOnGrid(double rate);

/// 含商店税时，付款方手上的钱还能承受几份（向下取整，保守）。
///
/// 卖出方向是店主付钱，而商店税由店主承担（DESIGN §8.9）——店主实际要付出
/// `总额 + 税`，所以份数上限必须按含税成本算，否则预校验会多算一份，
/// 到第二笔收税时才失败（整笔回滚，白跑一趟）。
///
/// 买入方向按 0 传（买家总支出就是牌面总额，税是从店主的收入里扣的），结果与 `affordableUnits` 一致。
int affordableUnitsWithTax(std::int64_t unitPrice, std::int64_t funds, double rate);

} // namespace ss::shop

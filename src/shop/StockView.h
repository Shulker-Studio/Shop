#pragma once

#include <optional>
#include <string>

namespace ss::shop {

/// 「这一项还能成交几份」（DESIGN §7.4 / §10.1 / §10.3）。
///
/// 用到它的两处是「货」与「收货方的空间」：`ShopInfoData::stockUnits` / `TradeRequest::goodsUnits`
/// （商店箱里的货，或卖出时玩家手里的货）与 `TradeRequest::roomUnits`（买入是背包，卖出是商店箱）。
/// 普通店两项都是有限份数；管理员店的模板箱是**无限**——永远卖不完，也永远收得下（票 01 / 02）。
///
/// **无限不是「很大一个数」**。用 `INT_MAX` / `-1` 当哨兵参与 `min` 有两个后果：
/// `INT_MAX` 会被当真成「真的能成交 INT_MAX 份」（件数乘法、表单档位全跟着走到溢出边缘），
/// 而 `-1` 与「一件都没有」共用一个值之后就没人分得清「无限」与「空」。
/// 所以它是一个显式标志位：`unlimited` 为真时 `units` **必须为空**。
struct StockView {
    /// 有限库存的份数；`unlimited` 为真时为空。默认是「有限 0 份」——与旧字段的默认值一致。
    std::optional<int> units = 0;
    bool               unlimited = false;

    StockView() = default;

    /// 隐式转换 `StockView view = 5;`：写出来就是「有限 5 份」。
    ///
    /// 保留它是因为既有调用点与既有测试写的是 `goodsUnits = 5` 这种形式，
    /// 而那个写法本来就只有一种读法。负数按 0 处理（负数不是合法的份数）。
    StockView(int finiteUnits); // NOLINT(google-explicit-constructor)

    /// 有限库存。负数按 0 处理。
    static StockView makeFinite(int units);

    /// 无限库存：没有「几份」这个数。
    ///
    /// 它与有限状态**互斥**，而构造口只有这一个，所以「无限 + 数字」这种两种真相的组合
    /// 在工厂这一侧造不出来（成员仍是公开的，读的一侧另有兜底，见 `finiteUnits`）。
    static StockView makeUnlimited();

    /// 状态自洽：`unlimited` 与 `units` 不许同时成立。
    [[nodiscard]] bool valid() const;

    /// 有限时的份数；无限时为空——`min` 只在它是数字时才参与，这是「无限不参与 `min`」的唯一出口。
    ///
    /// 非法的「无限 + 数字」也按无限处理：宁可多卖，也不许把一个其实无上限的店卡成 0 份。
    [[nodiscard]] std::optional<int> finiteUnits() const;

    /// 这一项够不够 `wanted` 份。无限恒够。
    [[nodiscard]] bool covers(int wanted) const;

    /// 「一件都没有 / 一格都放不下」：只有有限库存才可能为空，无限永远不空。
    [[nodiscard]] bool empty() const;
};

} // namespace ss::shop

#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace ss::shop {

/// 交易方向。
///
/// 买卖的**算法**是同一个（DESIGN §10.3 的两条公式其实是同一条 `min`），只有
/// 「谁的货、谁收货、谁付钱」和提示文案反过来。所以份数、件数、金额只算一份，
/// 方向只用来决定这些角色与说法。
enum class TradeDirection : std::uint8_t { Buy, Sell };

/// 方向 → 中文动词（`买` / `卖`）。日志与文案里要用这个词的地方都从这里取，
/// 别在每处再写一遍 `direction == TradeDirection::Buy ? "买" : "卖"`。
std::string_view directionVerb(TradeDirection direction);

/// 一次交易被拒的原因（DESIGN §10.2 第 2、3 步）。
///
/// 名字按**角色**取，不按方向取：「货不够」在买入时是商店箱空了，在卖出时是你手里没有；
/// 「钱不够」在买入时是买家余额，在卖出时是店主余额。具体说法由
/// `tradeRejectionMessage(direction, ...)` 决定，别在这里分叉。
///
/// 只放「由数字决定」的原因：「读不到店主」「读不到余额」「管理员店还没实现」
/// 「创造模式不能交易」这类需要世界状态的原因留在 mod 层。
enum class TradeRejection : std::uint8_t {
    None,
    OwnerOffline,    ///< 后端不支持离线记账，店主又不在线（DESIGN §8.5）
    NotEnoughGoods,  ///< 货凑不出请求的份数
    NotEnoughFunds,  ///< 付款方的余额不够
    NoRoomToReceive, ///< 收货方装不下（买入是背包，卖出是商店箱）
    OverUnitLimit,   ///< 请求份数超过服务器单次上限（`maxShopAmount`）
    InvalidRequest,  ///< 请求份数、份价或每份件数非法
};

/// 成交方案：份数、件数、金额三者一致，都由这里算出来，别处不再乘一遍。
struct TradePlan {
    int          units = 0; ///< 实际成交份数
    int          items = 0; ///< 实际搬运件数 = units × 每份件数
    std::int64_t total = 0; ///< 实际金额 = 一份价 × units
};

/// 一笔交易的全部输入（DESIGN §10.1 第 3 步 / §10.3）。**买卖共用这一个结构**：
/// 算法只认「要几份、搬几件、多少钱、三个上限」，角色由调用方装好。
///
/// | 字段 | 买入 | 卖出 |
/// | --- | --- | --- |
/// | `goodsUnits` | 商店箱能凑几份 | 玩家手里有几份 |
/// | `roomUnits` | 背包还能放几份 | 商店箱还能装几份 |
/// | `payerFunds` | 买家余额 | 店主余额 |
struct TradeRequest {
    int          requested     = 1; ///< 玩家要成交几份
    int          amountPerUnit = 0; ///< 一份的件数（牌面第 2 行）
    std::int64_t unitPrice     = 0; ///< 一份的价格（买入价或卖出价）
    int          goodsUnits    = 0; ///< 货现在够几份
    int          roomUnits     = 0; ///< 收货方还能收几份
    /// 付款方当前余额。**读不到时不能填 0**（那会把「读不到」当成「没钱」），
    /// 调用方要么先拒绝，要么这一笔根本不涉及钱（免费店）。
    std::int64_t payerFunds = 0;
    /// 份数天花板（`maxShopAmount`）。DESIGN §10.1 第 3 步把它列在上限里，
    /// 默认 `INT_MAX` 表示调用方不额外限制。
    int  maxUnits               = std::numeric_limits<int>::max();
    bool allowPartial           = true;  ///< `allowPartialTransactions`
    bool ownerOnline            = true;  ///< 店主此刻在不在线
    bool backendSupportsOffline = false; ///< `EconomyProvider::supportsOffline()`
};

struct TradeDecision {
    std::optional<TradePlan> plan;
    TradeRejection           rejection = TradeRejection::None;

    [[nodiscard]] bool ok() const { return plan.has_value(); }
};

/// 这次最多能成交几份：`min(maxUnits, goodsUnits, roomUnits, 付款方余额能付的份数)`
/// （DESIGN §10.1 第 3 步）。
///
/// 份数表单显示的上限与交易执行的上限都调它，别在两处各自 `std::min` 一遍。
int maxTradeUnits(int maxUnits, int goodsUnits, int roomUnits, std::int64_t unitPrice, std::int64_t payerFunds);

/// 算出这次能成交几份、搬几件、多少钱；不能成交就说明是哪一条上限挡住的。
///
/// 纯逻辑：不读世界、不写钱、不发消息。买入与卖出共用这一份算法（DESIGN §10.3）。
TradeDecision planTrade(TradeRequest const& request);

/// 拒绝原因 → 玩家提示（中文）。
///
/// `direction` 只决定「谁的钱」「谁的货」怎么称呼：买入的 `NotEnoughGoods` 是
/// 「商店的货不够」，卖出的是「你手里没有这么多」，所以这一句必须带方向。
std::string tradeRejectionMessage(TradeDirection direction, TradeRejection rejection);

} // namespace ss::shop

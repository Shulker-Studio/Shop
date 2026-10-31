#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace ss::shop {

/// 交易方向。
///
/// 买卖的**算法**是同一个（DESIGN §10.3 的两条公式其实是同一条 `min`），
/// 只有「谁的货、谁收货、谁付钱」和提示文案反过来。所以份数、件数、总价的
/// 计算只写一份，方向只用来决定这些角色和说法。
enum class TradeDirection : std::uint8_t { Buy, Sell };

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

/// 成交方案：份数、件数、总价三者一致，都由这里算出来，别处不再乘一遍。
struct TradePlan {
    int          units = 0; ///< 实际成交份数
    int          items = 0; ///< 实际搬运件数 = units × 每份件数
    std::int64_t total = 0; ///< 实际金额 = 一份价 × units
};

/// 一笔交易里与方向无关的那几个数（DESIGN §10.1 第 3 步 / §10.3）。
///
/// 三个「量」由调用方按方向装好：
///
/// | 字段 | 买入 | 卖出 |
/// | --- | --- | --- |
/// | `goodsUnits` | 商店箱能凑几份 | 玩家手里有几份 |
/// | `roomUnits` | 背包还能放几份 | 商店箱还能装几份 |
/// | `payerFunds` | 买家余额 | 店主余额 |
struct TradeLimits {
    int          requested     = 1; ///< 玩家要成交几份
    int          amountPerUnit = 0; ///< 一份的件数（牌面第 2 行）
    std::int64_t unitPrice     = 0; ///< 一份的价格（买入价或卖出价）
    int          goodsUnits    = 0; ///< 货现在够几份
    int          roomUnits     = 0; ///< 收货方还能收几份
    std::int64_t payerFunds    = 0; ///< 付款方当前余额（**读不到时调用方必须先拒绝**，不能填 0）
    /// 份数天花板（`maxShopAmount`）。DESIGN §10.1 第 3 步把它列在上限里，
    /// 默认 `INT_MAX` 表示调用方不额外限制。
    int  maxUnits     = std::numeric_limits<int>::max();
    bool allowPartial = true; ///< `allowPartialTransactions`
};

/// 三条上限里是哪一条挡住了成交。
///
/// 这是算法内部的结论，不是给玩家看的原因：同一个「货不够」，买入时要说「商店的货不够」，
/// 卖出时要说「你手里没有这么多」。所以这里只报「哪一条」，文案由 `planBuy`/`planSell` 定。
enum class TradeLimit : std::uint8_t {
    None,
    Invalid, ///< 输入本身非法（份数、份价、每份件数）
    Goods,
    Funds,
    Room,
    Cap, ///< 撞到了 `maxShopAmount` 这条单次上限
};

struct TradeAllowance {
    std::optional<TradePlan> plan;
    TradeLimit               blockedBy = TradeLimit::None;

    [[nodiscard]] bool ok() const { return plan.has_value(); }
};

/// 这次最多能成交几份：`min(maxUnits, 货的份数, 收货方容量, 付款方余额能付的份数)`
/// （DESIGN §10.1 第 3 步）。
///
/// 份数表单显示的上限与交易执行的上限都调它，别在两处各自 `std::min` 一遍。
int maxTradeUnits(int maxUnits, int goodsUnits, int roomUnits, std::int64_t unitPrice, std::int64_t payerFunds);

/// 算出这次能成交几份、搬几件、多少钱；不能成交就说明是哪一条上限挡住的。
///
/// 纯逻辑：不读世界、不写钱、不发消息。买入与卖出共用这一份算法（DESIGN §10.3）。
TradeAllowance tradeAllowance(TradeLimits const& limits);

/// 一次买入的全部输入：都是已经算好的数字，不碰引擎（DESIGN §10.3）。
struct BuyRequest {
    int          requested     = 1; ///< 玩家要买几份
    int          amountPerUnit = 0; ///< 一份的件数（牌面第 2 行）
    std::int64_t unitPrice     = 0; ///< 一份的买入价
    std::int64_t funds         = 0; ///< 玩家当前余额
    int          stockUnits    = 0; ///< 商店箱能撑几份
    int          backpackUnits = 0; ///< 背包还能放几份
    /// 份数天花板（`maxShopAmount`）。DESIGN §10.1 第 3 步把它列在上限里，
    /// 默认 `INT_MAX` 表示调用方不额外限制。
    int  maxUnits               = std::numeric_limits<int>::max();
    bool allowPartial           = true;  ///< `allowPartialTransactions`
    bool ownerOnline            = true;  ///< 店主此刻在不在线
    bool backendSupportsOffline = false; ///< `EconomyProvider::supportsOffline()`
};

/// 一次卖出的全部输入，与 `BuyRequest` 一一对应，只是角色反过来。
struct SellRequest {
    int          requested     = 1; ///< 玩家要卖几份
    int          amountPerUnit = 0; ///< 一份的件数（牌面第 2 行）
    std::int64_t unitPrice     = 0; ///< 一份的卖出价（店主付给玩家）
    /// 店主当前余额。**读不到时不能填 0**（那会把「读不到」误报成「店主资金不足」），
    /// 调用方必须先自己拒绝这一笔。
    std::int64_t ownerFunds             = 0;
    int          playerUnits            = 0;                               ///< 玩家手里的同种商品够几份
    int          boxUnits               = 0;                               ///< 商店箱还能装下几份
    int          maxUnits               = std::numeric_limits<int>::max(); ///< `maxShopAmount`
    bool         allowPartial           = true;
    bool         ownerOnline            = true;
    bool         backendSupportsOffline = false;
};

struct TradeDecision {
    std::optional<TradePlan> plan;
    TradeRejection           rejection = TradeRejection::None;

    [[nodiscard]] bool ok() const { return plan.has_value(); }
};

/// 算出这次买入能成交几份、扣多少钱；不能成交就给一个说得清的原因。
TradeDecision planBuy(BuyRequest const& request);

/// 算出这次卖出能成交几份、能收多少钱；不能成交就给一个说得清的原因。
///
/// 与 `planBuy` 共用同一份 `tradeAllowance`：部分成交开关、单次上限、
/// 「谁挡住了」的优先级只有一个说法（DESIGN §10.3）。
TradeDecision planSell(SellRequest const& request);

/// 拒绝原因 → 玩家提示（中文）。
///
/// `direction` 只决定「谁的钱」「谁的货」怎么称呼：买入的 `NotEnoughGoods` 是
/// 「商店的货不够」，卖出的是「你手里没有这么多」，所以这一句必须带方向。
std::string tradeRejectionMessage(TradeDirection direction, TradeRejection rejection);

} // namespace ss::shop

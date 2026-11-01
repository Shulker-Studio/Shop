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
    ShopClosed,      ///< 这家店停业中（牌面第 3 行的 `:x`）：比任何上限都优先
    OwnerOffline,    ///< 后端不支持离线记账，店主又不在线（DESIGN §8.5）
    NotEnoughGoods,  ///< 货凑不出请求的份数
    NotEnoughFunds,  ///< 付款方的余额不够
    NoRoomToReceive, ///< 收货方装不下（买入是背包，卖出是商店箱）
    OverUnitLimit,   ///< 请求份数超过服务器单次上限（`maxShopAmount`）
    InvalidRequest,  ///< 请求份数、份价或每份件数非法
};

/// 商店税在这笔交易里的样子（DESIGN §8.9）。两个字段直接装进 `TradeRequest`。
struct TradeTaxPlan {
    /// 这笔生意要被抽多少税（= `total × taxRate`）。后端收不走时是 0。
    double taxRate = 0.0;
    /// 这份税要不要**额外压在付款方身上**（卖出方向的店主）。买入方向是 false：
    /// 买家只付牌面总额，税从店主的收入里扣。
    bool taxOnPayer = false;
};

/// 按方向与后端能力算出这两个字段（DESIGN §8.9）。**mod 层不要自己写 `direction == Sell`**：
/// 一个方向判断写错，结果要么买入一分税都不收，要么把买家的份数上限按含税价算
/// （买家明明只付牌面总额，却被判「钱不够」）——这两种错都真的发生过，所以把判断收到这里。
///
/// `backendSupportsMoneySink` 是 `EconomyProvider::supportsMoneySink()`：没有「付给系统」的账户
/// （记分板）时，这道税收不走——算出来只会让店主少收、买家少付，而没有任何地方记着它，所以整笔不算。
///
/// `adminShop` 为真时也没有税：管理员店的对手方是系统账户（DESIGN §7.4），买卖两边的钱
/// 本来就靠销毁 / 凭空记账，再抽一道税只是把买家的钱多烧一笔，没有任何人收到它。
TradeTaxPlan tradeTaxPlan(
    TradeDirection direction,
    double         configuredRate,
    bool           backendSupportsMoneySink,
    bool           adminShop
);

/// 成交方案：份数、件数、金额三者一致，都由这里算出来，别处不再乘一遍。
struct TradePlan {
    int          units = 0; ///< 实际成交份数
    int          items = 0; ///< 实际搬运件数 = units × 每份件数
    std::int64_t total = 0; ///< 实际金额（牌面金额）= 一份价 × units
    /// 总额里的商店税（DESIGN §8.9），已向下取整，永远不超过 `total`。
    ///
    /// 店主承担：买入时店主实收 `total - tax`（买家仍付 `total`），
    /// 卖出时店主在 `total` 之外另付一笔 `tax`（玩家仍收 `total`）。
    /// 后端没有「能把钱付给系统」的账户时这里恒为 0——`taxRate` 由调用方按后端能力置 0。
    std::int64_t tax = 0;
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
    /// `requireOwnerOnline`（DESIGN §8.5）：只对不支持离线记账的后端有意义。
    bool requireOwnerOnline = true;
    /// 这家店停业中（牌面第 3 行的 `:x`，票 03）。像 `ownerOnline` 一样是**整家店**的属性：
    /// 停业时任何一笔都不成立，与要几份、箱子里有没有货无关。
    bool closed = false;
    /// 商店税率（DESIGN §8.9）：这笔交易的税 = `total × taxRate`，向下取整。
    /// **两个方向都要传**——税是从这笔生意里抽的，与谁承担那一笔无关。
    /// 后端不能销毁余额（记分板）时由调用方填 0：那样税根本收不走，算出来只会让账对不上。
    /// 管理员店也填 0：对手方是系统账户，没有可抽的成交额（`tradeTaxPlan` 已经判好）。
    double taxRate = 0.0;
    /// 这笔税是否**额外压在付款方身上**：卖出方向是 true（店主另付 `tax`），
    /// 买入方向是 false（税从店主的收入里扣，买家总支出不变）。
    ///
    /// 它只影响「付款方还付得起几份」，不影响税额本身。两件事混成一个字段的话，
    /// 买入方向会把税率当成 0，结果一分税都不收（10 的评审正好抓到过这个）。
    /// 填什么由 `tradeTaxPlan(direction, rate, supportsMoneySink)` 决定。
    bool taxOnPayer = false;
};

/// 付款方的含税成本率：只有 `taxOnPayer` 为 true 时才是税率，否则 0。
/// `planTrade` 与两个方向的份数上限都用它，别在别处再写一次这个三元。
double payerTaxRate(TradeRequest const& request);

struct TradeDecision {
    std::optional<TradePlan> plan;
    TradeRejection           rejection = TradeRejection::None;

    [[nodiscard]] bool ok() const { return plan.has_value(); }
};

/// 这次最多能成交几份：`min(maxUnits, goodsUnits, roomUnits, 付款方余额能付的份数)`
/// （DESIGN §10.1 第 3 步）。
///
/// `taxRate` 非 0 时按**含税成本**算付款方的份数上限（见 `affordableUnitsWithTax`），
/// 调用方只为「要额外掏这份税」的付款方传非 0（卖出方向的店主）。
/// 份数表单显示的上限与交易执行的上限都调它，别在两处各自 `std::min` 一遍。
int maxTradeUnits(
    int          maxUnits,
    int          goodsUnits,
    int          roomUnits,
    std::int64_t unitPrice,
    std::int64_t payerFunds,
    double       taxRate = 0.0
);

/// 同上，但「付款方能承受几份」已经算好了（`planTrade` 要拿它区分是哪一条上限挡住的，
/// 自己算一次再传进来，免得同一个二分跑两三遍）。
int maxTradeUnits(int maxUnits, int goodsUnits, int roomUnits, int affordableUnits);

/// 算出这次能成交几份、搬几件、多少钱；不能成交就说明是哪一条上限挡住的。
///
/// 纯逻辑：不读世界、不写钱、不发消息。买入与卖出共用这一份算法（DESIGN §10.3）。
///
/// `request.closed` 是第一条判据：停业是整家店的状态，比缺货、没钱、份数超限
/// （以及输入本身不合法）都优先——否则停业的店会先报「商店的货不够」这类不相干的原因。
TradeDecision planTrade(TradeRequest const& request);

/// 拒绝原因 → 玩家提示（中文）。
///
/// `direction` 只决定「谁的钱」「谁的货」怎么称呼：买入的 `NotEnoughGoods` 是
/// 「商店的货不够」，卖出的是「你手里没有这么多」，所以这一句必须带方向。
std::string tradeRejectionMessage(TradeDirection direction, TradeRejection rejection);

} // namespace ss::shop

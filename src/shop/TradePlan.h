#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace ss::shop {

/// 一次买入被拒的原因（DESIGN §10.2 第 2、3 步）。
///
/// 只放「由数字决定」的原因：店主离线与后端能力有关，但判定输入也是布尔量。
/// 「读不到店主」「管理员店还没实现」「创造模式不能交易」这类需要世界状态的原因留在 mod 层。
enum class TradeRejection : std::uint8_t {
    None,
    OwnerOffline,   ///< 后端不支持离线记账，店主又不在线（DESIGN §8.5）
    NotEnoughStock, ///< 商店箱里凑不出请求的份数
    NotEnoughMoney, ///< 余额不够
    BackpackFull,   ///< 背包放不下
    OverUnitLimit,  ///< 请求份数超过服务器单次上限（`maxShopAmount`）
    InvalidRequest, ///< 请求份数、份价或每份件数非法
};

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

/// 成交方案：份数、件数、总价三者一致，都由这里算出来，别处不再乘一遍。
struct BuyPlan {
    int          units = 0; ///< 实际成交份数
    int          items = 0; ///< 实际搬运件数 = units × 每份件数
    std::int64_t total = 0; ///< 实际扣款 = 一份价 × units
};

struct BuyDecision {
    std::optional<BuyPlan> plan;
    TradeRejection         rejection = TradeRejection::None;

    [[nodiscard]] bool ok() const { return plan.has_value(); }
};

/// 这次最多能买几份：`min(maxUnits, 可成交份数, 余额, 背包)`（DESIGN §10.1 第 3 步）。
///
/// 份数表单显示的上限与交易执行的上限都调它，别各自 `std::min` 一遍。
int maxBuyUnits(int maxUnits, int stockUnits, int backpackUnits, std::int64_t unitPrice, std::int64_t funds);

/// 算出这次买入能成交几份、扣多少钱；不能成交就给一个说得清的原因。
///
/// 纯逻辑：不读世界、不写钱、不发消息。
BuyDecision planBuy(BuyRequest const& request);

/// 拒绝原因 → 玩家提示（中文）。
std::string buyRejectionMessage(TradeRejection rejection);

} // namespace ss::shop

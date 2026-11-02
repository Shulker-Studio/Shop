#pragma once

#include "shop/ContainerFinder.h"
#include "shop/DiscardRecord.h"
#include "shop/TradePlan.h"
#include "shop/TradeRecord.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class BlockPos;
class BlockSource;
class Player;

namespace ss::shop {

struct ShopConfig;

/// 一次交易的结局。
///
/// 三种「没成」必须分开：`NeedsConfirm` 是什么都没动、等玩家点头；
/// `Broken` 是补偿失败（钱或货可能已经错位）；`Rejected` 是干净的拒绝。
/// 用枚举而不是三个 bool：后者能构造出「既成功又坏了」这种不存在的状态。
enum class TradeStatus : std::uint8_t {
    Done,         ///< 成交了
    Rejected,     ///< 没成交，什么都没动
    NeedsConfirm, ///< 没成交，等玩家确认重算后的份数与金额
    Broken,       ///< 补偿失败，钱或货可能已经错位（DESIGN §10.2 第 8 步）
};

struct TradeResult {
    TradeStatus status = TradeStatus::Rejected;
    /// 实际（`NeedsConfirm` 时是待确认的）成交方案：份数、件数、金额。
    TradePlan   plan;
    std::string message; ///< 发给玩家的中文提示；`NeedsConfirm` 时是确认表单的正文
    /// 这一次被销毁的物品堆的留档（管理员店卖出方向，票 02）。
    ///
    /// 记录在 `Done` **之前**造好（SNBT 只有那一刻读得到），落盘在 `Done` **之后**由
    /// `finishTrade` 做：留档失败只 warn，绝不能把一笔已经成交的生意判成失败
    /// （`p1-transaction-log` spec D5）。普通店与买入方向留空。
    std::vector<DiscardRecord> discarded;
    /// 流水用的**交易事实**（`p1-transaction-log`）：谁、在哪家店、买了什么、多少钱一份。
    ///
    /// `buyUnits` / `sellUnits` 一拿到商店快照就填，**失败路径上也带着**（牌没了的时候至少还有
    /// 交易者与坐标）；商店没解析出来时 `owner` / `item` 是空的，但仍然能定位到哪个人哪块牌。
    /// 结论（`ok` / `fail` / `frozen`）与 `ts` 由 `finishTrade` 补——它才是知道终态的地方。
    std::optional<TradeFacts> logFacts;
    /// 失败/冻结的**机器可读原因**（`no_money` / `frozen_pair` / `compensation_failed` …）。
    /// `Done` 时为空。两份短名表：`tradeRejectionReasonName`（由数字判定的原因）与 mod 侧的
    /// `RejectReason`（世界状态），两边产出同一种东西。
    std::string_view          logReason;

    [[nodiscard]] bool done() const { return status == TradeStatus::Done; }
    [[nodiscard]] bool needsConfirm() const { return status == TradeStatus::NeedsConfirm; }
};

/// 玩家在份数表单上提交的东西。买卖共用：表单机制是同一套（DESIGN §10.1 第 3 步）。
///
/// `expectedTotal` 是表单给他看的那个金额：两屏之间店主改价或箱子变少时，
/// 实际金额会对不上，这时必须先把真实金额摆出来确认（DESIGN §10.3），
/// 不能拿一个他没见过的数字直接成交。
struct TradeSubmission {
    int units = 1;
    /// 表单上写的「共 N 个」：店主改了第 2 行的份量时，份数与金额都可能不变，
    /// 只有件数会变（看到 64 个、只拿到 1 个），所以件数也要对。
    std::optional<int> expectedItems;
    /// 表单上写的金额（买入是扣款、卖出是收入）。空 = 没有可对照的金额。
    std::optional<std::int64_t> expectedTotal;
    /// 这次提交是否来自确认表单。只用来跳过一次提交冷却（确认按钮紧接着上一次提交），
    /// **不**用来跳过比对：确认之后状态又变的话还会再弹一次确认。
    bool confirmed = false;
};

/// 真正执行一次买入：动钱、动货。必须在服务端线程调用。
///
/// 如果实际能成交的份数、件数或金额与 `submission` 里玩家看到的不一致，**什么都不做**，
/// 返回 `NeedsConfirm` 让调用方先把真实值摆给玩家看；确认表单会把重算后的值作为新的
/// 期望值再提交一次，值没再变就成交，又变了就再确认一轮——绝不静默按新值扣款。
///
/// 失败时不留半成品：钱扣了货没给就退款，货从箱子里拿出来却塞不进背包就放回去。
TradeResult buyUnits(
    BlockSource&           region,
    Cell const&            sign,
    Player&                buyer,
    ShopConfig const&      config,
    TradeSubmission const& submission
);

/// 真正执行一次卖出：货从玩家背包进商店箱、钱从店主进玩家余额。必须在服务端线程调用。
///
/// 与 `buyUnits` 对称：同一份 `planTrade` 算份数、同一套快照与补偿收尾、同一个冻结名单。
/// 方向反过来体现在三件事上：货取自玩家背包、收货方是商店箱、付钱的是店主
/// （见实现里关于“为什么没有复制窗口”的顺序说明）。
TradeResult sellUnits(
    BlockSource&           region,
    Cell const&            sign,
    Player&                seller,
    ShopConfig const&      config,
    TradeSubmission const& submission
);

/// 信息表单上「购买」按钮的入口：弹出选份数的表单（DESIGN §10.1 第 3 步）。
///
/// 表单回调在哪个线程跑是不确定的（DESIGN §14 待验），所以回调里**一个世界状态都不碰**：
/// 只捕获「谁的 XUID、哪个维度、哪块牌」，真正要读商店箱/余额/背包时先切回服务端线程。
void requestBuyForm(Player& viewer, Cell const& sign);

/// 信息表单上「出售」按钮的入口，与 `requestBuyForm` 同一套机制。
void requestSellForm(Player& viewer, Cell const& sign);

/// 份数表单的提交入口。`submission` 来自表单，到这儿已经不算数了：
/// `buyUnits` 会按最新的可成交份数/余额/背包重新算一遍（DESIGN §10.4）。
/// 实际值与玩家看到的不一致时会先弹确认表单。
void submitBuy(Player& buyer, Cell const& sign, TradeSubmission const& submission);

/// 卖出份数表单的提交入口，与 `submitBuy` 同一套机制。
void submitSell(Player& seller, Cell const& sign, TradeSubmission const& submission);

/// 按配置准备提交冷却（`shopInteractionCooldownMs`）。重复调用无副作用。
void initTradeService(ShopConfig const& config);

/// 模组关闭时调用：只清提交冷却。
///
/// **冻结名单不在这里清**：`/ll reload Shop` 与关闭进程在我们的 `disable()` 里是同一条路径，
/// 用它清名单等于让运维为了改一个无关配置重载一次，就把「钱货还没对上」的交易对静默解冻了。
/// 但这只挡住了「主动清」：加载器若把 DLL 整块卸载再载入，静态对象仍会消失。
/// 要真跨过那个边界必须把冻结记录落盘（未做）；关闭时的 WARN 是最低限度的提示。
void resetTradeService();

/// 当前被冻结的「玩家 + 商店」对数。给关闭时的告警与将来的 `/shop unfreeze` 用。
[[nodiscard]] std::size_t frozenTradeCount();

} // namespace ss::shop

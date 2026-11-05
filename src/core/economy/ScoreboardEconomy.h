#pragma once

#include "core/economy/Economy.h"

#include <memory>
#include <string>

namespace ss::shop {

/// 记分板经济后端（DESIGN §8.6 第 1 条）：余额 = 玩家在某个记分板目标上的分值。
///
/// **支持离线账户**（2026-11-01）：记分板身份的内部键是 `PlayerScoreboardId`（= 玩家的
/// `ActorUniqueID`），它存在玩家存档里，所以人不在线也能拿到——照 LSE 的
/// `mc.addPlayerScore(uuid, …)`（「可修改离线玩家计分板」）同一条路：
/// `XUID → PlayerInfo 的 uuid → DBStorage 的 player_<uuid> → ServerId → UniqueID → getId(...)`。
///
/// 用 ID 而不是名字：改名、重名都不影响，也不会记到同名的假玩家身份上。
///
/// 仍然是「只支持**有身份**的离线玩家」：`getId` 返回 `INVALID`（这个人本世界还没有过记分板身份）
/// 时安全失败，不硬建——按名字建出来的是假玩家身份，真人上线后会多出一行同名的。
///
/// 分值内部是 `int32`，每次写入前都过 `economy::toScoreValue`，超出范围直接拒绝，
/// 不静默截断（DESIGN §8.2）。
class ScoreboardEconomy final : public economy::EconomyProvider {
public:
    /// 目标不存在时按配置决定建不建（DESIGN §8.4 / §15 的 `createObjectiveIfMissing`）。
    ///
    /// 目标在这里（而不是构造函数里）解析：`enable()` 时世界往往还没起来，
    /// 真正要用钱的时候才拿得到 `Level`（调用方先确认过 `getLevel()` 非空）。
    ///
    /// @param createIfMissing 目标不存在时是否用 `dummy` 判据建一个（`Scoreboard::getCriteria`
    ///        + `Scoreboard::addObjective`，即原版 `/scoreboard objectives add <名字> dummy`）。
    /// @param issue 拿不到时写明原因：没开自动创建是 `ObjectiveMissing`，
    ///        开了但没建成（判据缺失 / 引擎拒绝）是 `ObjectiveCreateFailed`。
    /// @return 空指针表示这个后端不可用，调用方降级到下一个候选。
    static std::shared_ptr<ScoreboardEconomy>
    create(std::string objectiveName, bool createIfMissing, economy::ProbeIssue& issue);

    std::string_view name() const override { return "scoreboard"; }
    bool             supportsOffline() const override { return true; }
    /// 管理员店的资金侧（DESIGN §7.4）：钱进 = 扣掉付款方的分（销毁），钱出 = 给收款方加分
    /// （凭空记账）。两笔都只需要一个在线玩家，而交易的两头总有一头是玩家。
    bool supportsSystemAccount() const override { return true; }

    /// 商店税（DESIGN §8.9）：**能**。付给系统账户就是「只扣付款方、谁都不加」，
    /// 记分板上那就是一笔真销毁，与 LegacyMoney 的 `LLMoney_Trans(from, "", n)` 同义。
    ///
    /// 早先这里返回 false（理由是「记分板没有能把钱移出经济系统的账户」，10 落地时把它
    /// 排除在税之外），2026-11-01 真机后改掉：污染的不是「有没有账户」而是「有没有对手方」，
    /// 税本来就不需要对手方。两个后端现在在税上行为一致，服主换后端不会突然开始/停止收税。
    bool supportsMoneySink() const override { return true; }

    std::optional<std::int64_t> get(economy::Account const& account) const override;
    bool                        set(economy::Account const& account, std::int64_t amount) override;
    bool                        add(economy::Account const& account, std::int64_t amount) override;
    bool reduce(economy::Account const& account, std::int64_t amount, bool allowNegative = false) override;

private:
    explicit ScoreboardEconomy(std::string objectiveName);

    /// 写入一个已经过范围检查的分值。
    bool writeScore(economy::Account const& account, int value) const;

    std::string mObjectiveName;
};

} // namespace ss::shop

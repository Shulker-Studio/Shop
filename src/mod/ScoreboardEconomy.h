#pragma once

#include "shop/Economy.h"

#include <memory>
#include <string>

namespace ss::shop {

/// 记分板经济后端（DESIGN §8.6 第 1 条）：余额 = 玩家在某个记分板目标上的分值。
///
/// 只支持在线玩家：记分板以 `ScoreboardId` 为键，离线玩家没有可写的 ID，
/// 所以 `supportsOffline()` 是 false（DESIGN §8.5）。
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
    bool             supportsOffline() const override { return false; }

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

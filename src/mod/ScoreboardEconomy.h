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
    /// 目标不存在时返回空指针：调用方降级到空实现。
    ///
    /// 目标在这里（而不是构造函数里）解析：`enable()` 时世界往往还没起来，
    /// 真正要用钱的时候才拿得到 `Level`。
    ///
    /// **不自动建目标**：那是 10 的 `createObjectiveIfMissing`（DESIGN §8.4 / §15）。
    /// 04 只认已经存在的目标，没有就让服主自己 `/scoreboard objectives add`。
    static std::shared_ptr<ScoreboardEconomy> create(std::string objectiveName);

    std::string_view name() const override { return "scoreboard"; }
    bool             supportsOffline() const override { return false; }

    std::int64_t get(economy::Account const& account) const override;
    bool         set(economy::Account const& account, std::int64_t amount) override;
    bool         add(economy::Account const& account, std::int64_t amount) override;
    bool         reduce(economy::Account const& account, std::int64_t amount, bool allowNegative = false) override;

private:
    explicit ScoreboardEconomy(std::string objectiveName);

    /// 写入一个已经过范围检查的分值。
    bool writeScore(economy::Account const& account, int value) const;

    std::string mObjectiveName;
};

} // namespace ss::shop

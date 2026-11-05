#pragma once

#include "core/sign/SignGeometry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ss::shop {

/// 连点冷却：同一玩家对同一块牌子在窗口内只放行一次（DESIGN §10.4）。
///
/// 键是「玩家 XUID + 维度 + 牌子坐标」，所以两个玩家同时点同一块牌、或同一玩家点两块牌都不互相影响。
/// **维度必须进键**：`Cell` 只有整数坐标，主世界与下界/末地完全可能各有一块同坐标的牌，
/// 少了维度就会让两个不相干的商店共享一个冷却窗口（玩家在 A 店点完，回 B 店点第一下被吞）。
/// 时钟由调用方注入（毫秒），因此这里不碰 `std::chrono`，可以纯逻辑测试。
class ClickCooldown {
public:
    /// 同时跟踪的点击数上限。超过后先清过期项，再清空——这是防刷兜底，
    /// 不是正常路径（正常路径下 `prune` 就够）。
    static constexpr std::size_t kMaxTracked = 512;

    /// `cooldownMs` 由配置提供（`shopInteractionCooldownMs`），这里不给默认值：
    /// 250 这个数只能有一处定义，否则两边会慢慢走神。
    explicit ClickCooldown(std::int64_t cooldownMs);

    /// 尝试放行一次。窗口内重复点击返回 false，且**不刷新**窗口。
    ///
    /// 窗口语义：距上次放行满 `cooldownMs` 即可再次放行（`>=`，不是 `>`）。
    /// `cooldownMs <= 0` 时永远放行，也不记录任何东西。
    ///
    /// `dimensionId` 是牌子所在维度：不同维度的同坐标牌子是两家店，不能共享冷却。
    bool allow(std::string_view xuid, int dimensionId, Cell const& sign, std::int64_t nowMs);

    /// 丢掉所有已经在窗口外的记录。
    void prune(std::int64_t nowMs);

    /// 当前跟踪的记录数（测试与诊断用）。
    [[nodiscard]] std::size_t tracked() const { return mLastHit.size(); }

private:
    std::int64_t                                  mCooldownMs;
    std::unordered_map<std::string, std::int64_t> mLastHit;
};

} // namespace ss::shop

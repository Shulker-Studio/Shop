#pragma once

#include "shop/ContainerFinder.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ss::shop {

/// 连点冷却：同一玩家对同一块牌子在窗口内只放行一次（DESIGN §10.4）。
///
/// 键是「玩家 XUID + 牌子坐标」，所以两个玩家同时点同一块牌、或同一玩家点两块牌都不互相影响。
/// 时钟由调用方注入（毫秒），因此这里不碰 `std::chrono`，可以纯逻辑测试。
class TradeGate {
public:
    /// 同时跟踪的点击数上限。超过后先清过期项，再清空——这是防刷兜底，
    /// 不是正常路径（正常路径下 `prune` 就够）。
    static constexpr std::size_t kMaxTracked = 512;

    /// `cooldownMs` 由配置提供（`shopInteractionCooldownMs`），这里不给默认值：
    /// 250 这个数只能有一处定义，否则两边会慢慢走神。
    explicit TradeGate(std::int64_t cooldownMs);

    /// 尝试放行一次。窗口内重复点击返回 false，且**不刷新**窗口。
    ///
    /// 窗口语义：距上次放行满 `cooldownMs` 即可再次放行（`>=`，不是 `>`）。
    /// `cooldownMs <= 0` 时永远放行，也不记录任何东西。
    bool allow(std::string_view xuid, Cell const& sign, std::int64_t nowMs);

    /// 丢掉所有已经在窗口外的记录。
    void prune(std::int64_t nowMs);

    /// 当前跟踪的记录数（测试与诊断用）。
    [[nodiscard]] std::size_t tracked() const { return mLastHit.size(); }

private:
    std::int64_t                                  mCooldownMs;
    std::unordered_map<std::string, std::int64_t> mLastHit;
};

} // namespace ss::shop

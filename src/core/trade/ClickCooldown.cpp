#include "core/trade/ClickCooldown.h"

#include <string>

namespace ss::shop {
namespace {

/// XUID、维度与坐标拼成一个键。分隔符用 `\x1f`（ASCII 单元分隔符）：
/// XUID 是纯数字、维度与坐标是整数，都不可能包含它，拼不出歧义。
///
/// 维度是键的一部分：`Cell` 本身不带维度，少了它，主世界 (10,64,10) 与下界 (10,64,10)
/// 会共用同一个冷却记录。
std::string keyOf(std::string_view xuid, int dimensionId, Cell const& sign) {
    std::string key;
    key.reserve(xuid.size() + 32);
    key.append(xuid);
    key.push_back('\x1f');
    key.append(std::to_string(dimensionId));
    key.push_back('\x1f');
    key.append(std::to_string(sign.x));
    key.push_back(',');
    key.append(std::to_string(sign.y));
    key.push_back(',');
    key.append(std::to_string(sign.z));
    return key;
}

} // namespace

ClickCooldown::ClickCooldown(std::int64_t cooldownMs) : mCooldownMs(cooldownMs) {}

bool ClickCooldown::allow(std::string_view xuid, int dimensionId, Cell const& sign, std::int64_t nowMs) {
    if (mCooldownMs <= 0) {
        return true; // 冷却关掉了：不记录，表永远空着
    }

    // 防刷兜底：正常玩家点不出这么多不同的牌子，能到这一步说明有人在刷。
    if (mLastHit.size() >= kMaxTracked) {
        prune(nowMs);
        if (mLastHit.size() >= kMaxTracked) {
            mLastHit.clear();
        }
    }

    std::string key = keyOf(xuid, dimensionId, sign);
    if (auto const it = mLastHit.find(key); it != mLastHit.end() && nowMs - it->second < mCooldownMs) {
        // 被拒的点击不刷新窗口：否则连点会把自己越推越远
        return false;
    }
    mLastHit.insert_or_assign(std::move(key), nowMs);
    return true;
}

void ClickCooldown::prune(std::int64_t nowMs) {
    for (auto it = mLastHit.begin(); it != mLastHit.end();) {
        if (nowMs - it->second >= mCooldownMs) {
            it = mLastHit.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace ss::shop

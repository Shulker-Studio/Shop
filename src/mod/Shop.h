#pragma once

#include "ll/api/mod/NativeMod.h"

#include "mod/Config.h"

#include <optional>

namespace ss::shop {

class Shop {
public:
    static Shop& getInstance();

    Shop() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    /// 配置在 `load()` 里读取并校验；非法时模组拒绝加载。
    [[nodiscard]] std::optional<ShopConfig> const& config() const { return mConfig; }

    /// @return 模组是否加载成功。
    bool load();

    /// @return 模组是否启用成功。
    bool enable();

    /// @return 模组是否关闭成功。
    bool disable();

private:
    ll::mod::NativeMod&       mSelf;
    std::optional<ShopConfig> mConfig;
};

/// 当前可用的配置；模组没启用（未加载或配置非法）时为空。
///
/// 破坏防护（08）与内容防偷（09）的每一条 hook 都先问它：读不到配置就原样放行原版
/// ——宁可在空配置上放过一次搬运，也不要对着一份不存在的配置误判。
[[nodiscard]] ShopConfig const* currentShopConfig();

} // namespace ss::shop

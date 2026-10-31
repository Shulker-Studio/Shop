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

} // namespace ss::shop

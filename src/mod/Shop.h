#pragma once

#include "ll/api/mod/NativeMod.h"

namespace ss::shop {

class Shop {
public:
    static Shop& getInstance();

    Shop() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    /// @return 模组是否加载成功。
    bool load();

    /// @return 模组是否启用成功。
    bool enable();

    /// @return 模组是否关闭成功。
    bool disable();

private:
    ll::mod::NativeMod& mSelf;
};

} // namespace ss::shop

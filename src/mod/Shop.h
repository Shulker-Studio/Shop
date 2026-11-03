#pragma once

#include "ll/api/mod/NativeMod.h"

#include "mod/Config.h"
#include "shop/ItemBanList.h"

#include <optional>

namespace ss::shop {

class Shop {
public:
    static Shop& getInstance();

    Shop() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    /// 配置在 `load()` 里读取并校验；非法时模组拒绝加载。
    [[nodiscard]] std::optional<ShopConfig> const& config() const { return mConfig; }

    /// 牌面解析用的那三个字段（`maxShopAmount` / `adminShopName` / 价格规则）。
    ///
    /// 从 `mConfig` 派生一次存在这里，热路径直接拿：漏斗、投掷器、铜傀儡的 hook 是每 tick 跑的，
    /// 而 `ShopSignConfig` 里带一个 `std::string`——以前每次现算（`toSignConfig`）都要拷一个字符串。
    /// 模组没加载时为空，调用方必须当「没有牌面配置」处理。
    [[nodiscard]] ShopSignConfig const* signConfig() const { return mSignConfig ? &*mSignConfig : nullptr; }

    /// 禁售物品表（`config.bannedItems` 编译好的正则）。
    ///
    /// 与 `signConfig()` 不同，这里**永远有一份**（可能为空表）：注册与点牌那几条路径
    /// 都是“命中就拦”，没有“配置不在了所以只能放行”的语义。模组没加载时就是一份空表。
    [[nodiscard]] ItemBanList const& banList() const { return mBanList; }

    /// 重新读 `config.json`（`/shop reload`）。
    ///
    /// **失败不动当前配置**：热重载读到一个坏文件就把 `mConfig` 清掉，等于交易与全部保护
    /// 一起关掉——那比「配置没更新」糟得多。所以这里只在解析成功时才替换。
    ///
    /// 成功后会重建两样「启动时才建」的东西：提交冷却窗口（`gSubmitGate`）与经济后端选择
    /// （`resetEconomy` + `initEconomyService`）。不重挂任何 hook：hook 里一律现取配置。
    ///
    /// @return 失败原因（一句给人看的话）；成功时为空。
    [[nodiscard]] std::optional<std::string> reloadConfig();

    /// @return 模组是否加载成功。
    bool load();

    /// @return 模组是否启用成功。
    bool enable();

    /// @return 模组是否关闭成功。
    bool disable();

private:
    /// 把 `mConfig` 投影成两份缓存（`mSignConfig` 与 `mBanList`）。
    /// `load()` / `reloadConfig()` 成功后各调一次，`disable()` 时清空。
    void refreshDerivedConfig();

    ll::mod::NativeMod&            mSelf;
    std::optional<ShopConfig>      mConfig;
    std::optional<ShopSignConfig>  mSignConfig;
    ItemBanList                    mBanList;
};

/// 当前可用的配置；模组没启用（未加载或配置非法）时为空。
///
/// 破坏防护（08）与内容防偷（09）的每一条 hook 都先问它：读不到配置就原样放行原版
/// ——宁可在空配置上放过一次搬运，也不要对着一份不存在的配置误判。
[[nodiscard]] ShopConfig const* currentShopConfig();

/// 当前可用的牌面配置（`Shop::signConfig()` 的全局入口）。
///
/// 与 `currentShopConfig()` 成对使用：两者要么都有值、要么都没有。
/// 拿不到时调用方必须放弃（当作「没有商店牌」），不要退回去现算一份——
/// 那正是这里要省掉的那次字符串拷贝。
[[nodiscard]] ShopSignConfig const* currentSignConfig();

/// 当前生效的禁售表（配置里的正则已经编译好）。
///
/// **模组没启用时是一份空表**（什么都没禁），而不是“没有禁售表”：调用方判的是
/// “这件物品要不要拦”，拿不到表时给一个空表让代码只有一条路径。
[[nodiscard]] ItemBanList const& currentBanList();

} // namespace ss::shop

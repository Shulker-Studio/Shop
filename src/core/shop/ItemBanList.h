#pragma once

#include <cstddef>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace ss::shop {

/// 禁售物品表（配置 `bannedItems`）：一组**正则**，拿去匹配物品 ID（`minecraft:bedrock`）。
///
/// 为什么匹配 ID 而不是展示名：展示名随语言变（`钻石剑` / `Diamond Sword`），
/// 也随资源包变；ID 是数据层的名字，写一次就永远对得上。
///
/// **整串匹配**（`std::regex_match`）而不是查找：`stone` 这种写法若按查找会顺手把
/// `minecraft:redstone` 与 `minecraft:cobblestone` 一起禁掉——一个只写了一半的规则
/// 变成一把过宽的刀，服主还没法从前缀上看出自己禁了什么。整串匹配下它什么也禁不到，
/// 加载时再补一句“要写命名空间”的提示（见 `Config.cpp`），错在安全的那一侧。
///
/// 大小写不敏感、允许 `.*` 通配。空白会被裁掉，空规则直接跳过（不算错）。
class ItemBanList {
public:
    ItemBanList() = default;

    /// 编译一批规则。编译失败的那几条**不会**生效，但会被记下来（`invalidPatterns`）：
    /// 丢掉一条禁售规则等于静默放行，所以配置层拿到这份清单会拒绝加载
    /// （见 `Config.cpp` 的 `validate`），运行期不该看到非空。
    explicit ItemBanList(std::vector<std::string> const& patterns);

    /// 这条物品 ID 是否禁售。
    [[nodiscard]] bool banned(std::string_view itemId) const;

    /// 生效的规则条数（空规则不算）。
    [[nodiscard]] std::size_t size() const { return mRules.size(); }
    [[nodiscard]] bool        empty() const { return mRules.empty(); }

    /// 编译失败的那几条（原文，已裁掉空白）。
    [[nodiscard]] std::vector<std::string> const& invalidPatterns() const { return mInvalid; }

private:
    std::vector<std::regex>  mRules;
    std::vector<std::string> mInvalid;
};

} // namespace ss::shop

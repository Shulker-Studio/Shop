#pragma once

#include <string_view>

namespace ss::shop {

/// 这个物品 ID 是不是**收纳袋**（`minecraft:bundle`、`minecraft:red_bundle` 这类，带颜色的也一样）。
///
/// 收纳袋**永远不能当商品**，与禁售表无关：现代基岩版把收纳袋的内容存在服务端的动态容器注册表里
/// （`StorageItemComponent` 那套 `FullContainerName` + `bundle_id`），拿到的只是引用，
/// 服务端没有可用的读取入口（`StorageItemUtility::getStorageItemID` 是客户端-only）。
/// 于是「盒内到底装了什么」查不出来，既不能核对有没有禁售物品，也没法把内容算进商品身份。
///
/// 判据是**名字以 `bundle` 结尾**，不看命名空间：原版带颜色的那几个叫 `<颜色>_bundle`，
/// 第三方插件造的同名形状物品也走同一条规则。宁可多拦一个自定义物品，也不要放过一条
/// 「把禁售品装进袋子」的渠道。
///
/// 潜影盒不在此列：它的内容就在物品 NBT 的 `Items` 里，读得出来。
inline bool isBundleItemId(std::string_view itemId) {
    auto const colon = itemId.rfind(':');
    auto const name  = colon == std::string_view::npos ? itemId : itemId.substr(colon + 1);
    constexpr std::string_view kSuffix = "bundle";
    return name.size() >= kSuffix.size() && name.substr(name.size() - kSuffix.size()) == kSuffix;
}

} // namespace ss::shop

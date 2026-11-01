#pragma once

#include <optional>

namespace ss::shop {

/// 模板箱的**样品槽**：合并槽位里**第一个非空的那一格**（DESIGN §7.4，P1 票 03）。
///
/// 管理员店的容器不是库存，是模板：里面那件东西只回答「这家店卖什么」。而「第一格」指的是
/// **第一个非空槽位**，不是固定的 0 号槽——箱子里 0 号空着、1 号放了钻石，钻石就该是样品。
/// 写成固定槽号会在「前面几格空着」时判成「没有商品」，那正是票 03 要修的那种错。
///
/// `slots` 是合并后的总格数，`isEmpty(slot)` 回答「第 slot 格（按合并顺序）是空的吗」。
/// 谓词是参数而不是写死 `ItemStack`：这条规则本身是纯逻辑，`src/mod` 只提供「空不空」，
/// 于是「第一个非空」可以在单测里直接断言（0 号空 / 1 号有 / 全空）。
///
/// 整个箱子空着时返回空——**没有样品槽不是「卖光了」**，而是「这家店还没设商品」。
template <class IsEmpty>
std::optional<int> sampleSlotIndex(int slots, IsEmpty&& isEmpty) {
    for (int slot = 0; slot < slots; ++slot) {
        if (!isEmpty(slot)) {
            return slot;
        }
    }
    return std::nullopt;
}

} // namespace ss::shop

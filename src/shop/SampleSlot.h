#pragma once

#include <optional>
#include <vector>

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

/// 可卖的槽位顺序：**从最后一格往前**，并且**跳过样品槽**。
///
/// 两条规则合起来就是「样品槽永远不出售」：
/// - 跳过样品槽：那一格是商品定义的实物兜底。卖光它之后牌子第 4 行的商品名就没有依据了，
///   店会静悄悄变回「还没设商品」——店主必须自己盯着别把最后一件卖掉。
/// - 从后往前：先动离样品槽最远的那一端，前面的格子留得久，店主一眼能看出「还没动过的有哪些」。
///
/// 返回的是**合并槽位下标**（`mergedSlots` 的顺序，双箱先附着那一半）。计数与扣货都按它走，
/// 于是「哪几格可卖、按什么顺序卖」只有这一处定义——不会出现「数的时候算了、扣的时候没算」。
///
/// `sampleSlot` 为空 = 没有样品槽的概念（玩家背包、空箱子）：这时就是单纯倒序。
inline std::vector<int> saleableSlotOrder(int slots, std::optional<int> sampleSlot) {
    std::vector<int> order;
    if (slots <= 0) {
        return order;
    }
    order.reserve(static_cast<std::size_t>(slots));
    for (int slot = slots - 1; slot >= 0; --slot) {
        if (sampleSlot.has_value() && slot == *sampleSlot) {
            continue;
        }
        order.push_back(slot);
    }
    return order;
}

} // namespace ss::shop

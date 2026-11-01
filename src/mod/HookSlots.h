#pragma once

#include <span>

namespace ss::shop {

/// 一条 hook 的注册/反注册与它的状态。
///
/// 破坏防护（08）与内容防偷（09）的所有 hook 接线方式完全相同：挂上、记账、
/// 反注册时按逆序解开。所以这里是一张表 + 两个函数，而不是每个 hook 文件各写一遍
/// 同形的循环——加一条 hook 只是往表里加一行。
struct HookSlot {
    char const* name;
    int (*attach)();
    bool (*detach)();
    bool attached = false; ///< 已经挂过了，不重复挂、也不漏反注册（不代表「挂成功了」）
    /// 最近一次 `attach()` 的返回值。**同样不代表成功**（见上），只给 `/shop status` 对照
    /// 符号漂移用：同一个二进制上返回码从 0 变成非 0，就是符号或版本变了。
    int lastReturn = 0;
};

/// 挂上表里还没挂的那几条。
///
/// **返回值不拿来判成败**：`ll::memory::hook` 只是把 `pl::hook` 的 int 转发出去，
/// 0 还是 1 代表成功在 LL 文档里没写（参考实现 ChestLock 直接忽略）。非 0 只记一条 warn
/// 供真机排查「符号漂移」，真正的判据是功能是否生效。几条各自独立：一条不响不该拖垮其他几条。
void attachHooks(std::span<HookSlot> slots);

/// 按注册的逆序解开已经挂上的那几条（没挂上的不动）。
void detachHooks(std::span<HookSlot> slots);

/// 两张 hook 名单，给 `/shop status` 如实列出「已尝试注册 + 返回码」。
/// 只暴露只读视图：接线只在各自的 `register*Hooks()` 里做。
std::span<HookSlot const> destructionHookSlots();
std::span<HookSlot const> contentHookSlots();

} // namespace ss::shop

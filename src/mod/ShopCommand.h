#pragma once

namespace ss::shop {

/// 注册 `/shop` 命令（`status` / `debug <on|off>`）。
///
/// 这是本模组唯一的命令入口。它的用途**只有观测**：查询当前状态、开关调试日志。
/// 不提供经济查询、不提供店铺管理——那些属于 P1，而且现在没有索引可查。
///
/// 注册失败不影响模组启用（命令是可选工具）；失败时打一条 error。
void registerShopCommand();

} // namespace ss::shop

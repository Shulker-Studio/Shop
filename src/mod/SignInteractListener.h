#pragma once

namespace ss::shop {

/// 注册方块交互监听（点商店牌 → 表单）。重复调用无副作用。
void registerSignInteractListener();

/// 注销监听。模组关闭时调用。
void unregisterSignInteractListener();

} // namespace ss::shop

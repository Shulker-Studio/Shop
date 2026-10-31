#pragma once

namespace ss::shop {

/// 注册告示牌编辑监听（正面）。重复调用无副作用。
void registerSignEditListener();

/// 注销监听。模组关闭时调用。
void unregisterSignEditListener();

} // namespace ss::shop

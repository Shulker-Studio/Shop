#include "mod/Shop.h"

#include "ll/api/mod/RegisterHelper.h"

namespace ss::shop {

Shop& Shop::getInstance() {
    static Shop instance;
    return instance;
}

bool Shop::load() {
    getSelf().getLogger().debug("Loading...");
    // 读取配置、初始化存储
    return true;
}

bool Shop::enable() {
    getSelf().getLogger().debug("Enabling...");
    // 注册事件监听、命令、经济后端探测
    return true;
}

bool Shop::disable() {
    getSelf().getLogger().debug("Disabling...");
    // 注销监听、刷新待写数据
    return true;
}

} // namespace ss::shop

LL_REGISTER_MOD(ss::shop::Shop, ss::shop::Shop::getInstance());

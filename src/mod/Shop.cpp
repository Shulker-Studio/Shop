#include "mod/Shop.h"

#include "mod/ContainerProtectionListener.h"
#include "mod/EconomyService.h"
#include "mod/SignEditListener.h"
#include "mod/SignInteractListener.h"
#include "mod/TradeService.h"

#include "ll/api/mod/RegisterHelper.h"

namespace ss::shop {

Shop& Shop::getInstance() {
    static Shop instance;
    return instance;
}

bool Shop::load() {
    auto& logger = getSelf().getLogger();
    logger.debug("Loading...");

    mConfig = loadShopConfig(getSelf().getConfigDir() / "config.json", logger);
    if (!mConfig) {
        logger.error("配置不可用，Shop 拒绝加载");
        return false;
    }
    return true;
}

bool Shop::enable() {
    getSelf().getLogger().debug("Enabling...");
    if (mConfig) {
        initTradeService(*mConfig);
    }
    registerSignEditListener();
    registerSignInteractListener();
    registerContainerProtectionListener();
    return true;
}

bool Shop::disable() {
    getSelf().getLogger().debug("Disabling...");
    unregisterContainerProtectionListener();
    unregisterSignInteractListener();
    unregisterSignEditListener();
    resetTradeService();
    resetEconomy();
    mConfig.reset();
    return true;
}

} // namespace ss::shop

LL_REGISTER_MOD(ss::shop::Shop, ss::shop::Shop::getInstance());

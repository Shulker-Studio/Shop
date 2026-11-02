#include "mod/Shop.h"
#include "mod/ShopCommand.h"

#include "mod/ContainerProtectionListener.h"
#include "mod/ContentProtectionHooks.h"
#include "mod/DebugLog.h"
#include "mod/DestructionProtectionHooks.h"
#include "mod/EconomyService.h"
#include "mod/LogFile.h"
#include "mod/SignEditListener.h"
#include "mod/SignInteractListener.h"
#include "mod/TradeService.h"

#include "ll/api/mod/RegisterHelper.h"

namespace ss::shop {

Shop& Shop::getInstance() {
    static Shop instance;
    return instance;
}

ShopConfig const* currentShopConfig() {
    auto const& config = Shop::getInstance().config();
    return config ? &*config : nullptr;
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
        // 经济后端也是懒解析：现在世界往往还没起来，所以这里只挂启动监听 + 打税告警。
        initEconomyService(*mConfig);
        if (mConfig->transactionLog.enabled) {
            // 旧布局（文件平铺在 `logs/` 根下）搬进 `transactions/` 与 `discarded/` 子目录。
            // 只在开着流水时搬：关掉流水的服主不会期望模组去动他的目录。
            // 幂等，`/ll reload Shop` 再跑一次也安全。
            migrateLegacyLogs();
        }
    }
    registerSignEditListener();
    registerSignInteractListener();
    registerContainerProtectionListener();
    registerDestructionProtectionHooks();
    registerContentProtectionHooks();
    // 命令只服务观测：注册失败不影响模组，不影响上面的任何一条接线。
    registerShopCommand();
    return true;
}

bool Shop::disable() {
    getSelf().getLogger().debug("Disabling...");
    unregisterContentProtectionHooks();
    unregisterDestructionProtectionHooks();
    unregisterContainerProtectionListener();
    unregisterSignInteractListener();
    unregisterSignEditListener();
    resetTradeService();
    resetEconomy();
    // 调试开关不跨启用周期：换一次配置重启后还开着 debug，控制台会被刷满。
    setShopDebugEnabled(false);
    mConfig.reset();
    return true;
}

} // namespace ss::shop

LL_REGISTER_MOD(ss::shop::Shop, ss::shop::Shop::getInstance());

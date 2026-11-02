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

ShopSignConfig const* currentSignConfig() { return Shop::getInstance().signConfig(); }

void Shop::refreshSignConfig() {
    mSignConfig = mConfig ? std::optional<ShopSignConfig>{toSignConfig(*mConfig)} : std::nullopt;
}

bool Shop::load() {
    auto& logger = getSelf().getLogger();
    logger.debug("Loading...");

    mConfig = loadShopConfig(getSelf().getConfigDir() / "config.json", logger);
    if (!mConfig) {
        // 拒绝加载是有意的（带着半份配置跑更糟），但后果必须说清楚：
        // 不只是「不能交易」——保护那些 hook 也一条都不会挂，箱子完全不设防。
        logger.error(
            "配置不可用，Shop 拒绝加载：交易、商店箱保护、破坏防护、内容防偷**全部不会启用**。"
            "修好 config.json 后重启服务器（模组没加载时连 /shop status 都没有）"
        );
        return false;
    }
    refreshSignConfig();
    return true;
}

std::optional<std::string> Shop::reloadConfig() {
    auto& logger = getSelf().getLogger();
    auto  loaded = loadShopConfig(getSelf().getConfigDir() / "config.json", logger);
    if (!loaded) {
        // 保持旧配置不动：这是本函数唯一一条重要规则（见头文件）。
        return "新配置无效，已保留当前配置（原因见控制台）";
    }

    // 原地赋值而不是换对象：所有现取 `config()` 指针的地方（hook、监听器）继续有效。
    *mConfig = std::move(*loaded);
    refreshSignConfig();

    // 这两样是启动时建的，配置变了必须重建：
    // - 提交冷却窗口来自 `shopInteractionCooldownMs`；
    // - 后端选择来自 `economy.backend`，且解析结果是懒缓存。
    // 冻结名单不受影响（`resetTradeService` 不碰它）。
    resetTradeService();
    initTradeService(*mConfig);
    resetEconomy();
    initEconomyService(*mConfig);

    logger.info(
        "配置已重新读取：{}（maxShopAmount={}、economy.backend={}、shopTaxRate={}、流水{}）",
        (getSelf().getConfigDir() / "config.json").string(),
        mConfig->maxShopAmount,
        mConfig->economy.backend,
        mConfig->tax.shopTaxRate,
        mConfig->transactionLog.enabled ? "开" : "关"
    );
    return std::nullopt;
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
            // 压缩只在启动时做一次（以前是每写一行流水就扫目录 + 同步 gzip，
            // 那笔卡顿落在成交那一拍的 tick 线程上）。顺序必须先是搬家、再是压缩：
            // 先搬完旧布局，压缩才能只扫两个受控子目录。
            // 这里同步压完：服主本来就在等启动，卡在启动比卡在交易好。
            compressOldLogsOnStartup();
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
    if (auto const frozen = frozenTradeCount(); frozen > 0) {
        getSelf().getLogger().warn(
            "模组正在卸载，但还有 {} 对「玩家 + 商店」处于冻结状态（补偿失败、尚未人工核对）。"
            "冻结记录不随重载清除，进程退出后才会消失；请在重启前把日志里的 fatal 记录核一遍",
            frozen
        );
    }
    resetTradeService();
    resetEconomy();
    // 调试开关不跨启用周期：换一次配置重启后还开着 debug，控制台会被刷满。
    setShopDebugEnabled(false);
    // 日志失败提示也不跨启用周期：重载通常意味着服主正在修磁盘/权限，新的 warn 才是有用的。
    resetLogWarnings();
    mConfig.reset();
    mSignConfig.reset();
    return true;
}

} // namespace ss::shop

LL_REGISTER_MOD(ss::shop::Shop, ss::shop::Shop::getInstance());

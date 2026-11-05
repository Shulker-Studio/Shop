#include "Shop.h"
#include "util/i18n/Locale.h"
#include "util/i18n/Message.h"
#include "command/ShopCommand.h"
#include "command/TestCommand.h"

#include "event/ContainerProtectionListener.h"
#include "event/ContentProtectionHooks.h"
#include "util/log/Debug.h"
#include "event/DestructionProtectionHooks.h"
#include "core/economy/Service.h"
#include "util/log/Files.h"
#include "event/SignEditListener.h"
#include "event/SignInteractListener.h"
#include "core/trade/TradeService.h"

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

ItemBanList const& currentBanList() { return Shop::getInstance().banList(); }

void Shop::refreshDerivedConfig() {
    mSignConfig = mConfig ? std::optional<ShopSignConfig>{toSignConfig(*mConfig)} : std::nullopt;
    // 禁售表在这里一起重建：它也是从 `mConfig` 投影出来的，而且**必须**跟配置同时生效
    // ——`/shop reload` 之后还拿旧正则拦东西是说不通的。
    mBanList = mConfig ? ItemBanList{mConfig->bannedItems} : ItemBanList{};
}

bool Shop::load() {
    // **必须先载入词表**：下面 `loadShopConfig` 的每一条校验日志都走 `ui::msg`，
    // 晚一步就是三十行 key id 打进控制台，而那一刻恰好是服主最需要读懂的时候。
    // `loadTexts` 只读 `lang/` 目录，不依赖 `config.json`，所以可以在最前面调。
    ui::loadTexts();

    auto& logger = getSelf().getLogger();
    logger.debug(ui::msg("shop.log.lifecycle.loading"));

    mConfig = loadShopConfig(getSelf().getConfigDir() / "config.json", logger);
    if (!mConfig) {
        // 拒绝加载是有意的（带着半份配置跑更糟），但后果必须说清楚：
        // 不只是「不能交易」——保护那些 hook 也一条都不会挂，箱子完全不设防。
        logger.error(ui::msg("shop.log.lifecycle.refused"));
        return false;
    }
    refreshDerivedConfig();
    return true;
}

std::optional<std::string> Shop::reloadConfig() {
    // 这里**故意不重读 `lang/`**：`ll::i18n::I18n` 是全局单例，`load(path)` 对已载入
    // locale 是覆盖还是追加、失败时会不会留下半份词表，LeviLamina 的头文件里只有声明，
    // 没有可查的实现。在没有实机验证之前，不拿一条"改完语言文件 /shop reload 就生效"
    // 的承诺去换一次可能半坏的热重载——语言文件是发布产物，改它就重启/`/ll reload`。
    auto& logger = getSelf().getLogger();
    auto  loaded = loadShopConfig(getSelf().getConfigDir() / "config.json", logger);
    if (!loaded) {
        // 保持旧配置不动：这是本函数唯一一条重要规则（见头文件）。
        return "shop.command.reload.failed";
    }

    // 原地赋值而不是换对象：所有现取 `config()` 指针的地方（hook、监听器）继续有效。
    *mConfig = std::move(*loaded);
    refreshDerivedConfig();

    // 这两样是启动时建的，配置变了必须重建：
    // - 提交冷却窗口来自 `shopInteractionCooldownMs`；
    // - 后端选择来自 `economy.backend`，且解析结果是懒缓存。
    // 冻结名单不受影响（`resetTradeService` 不碰它）。
    resetTradeService();
    initTradeService(*mConfig);
    resetEconomy();
    initEconomyService(*mConfig);

    logger.info(ui::msg(
            "shop.log.lifecycle.reloaded",
            (getSelf().getConfigDir() / "config.json").string(),
            mConfig->maxShopAmount,
            mConfig->economy.backend,
            mConfig->tax.shopTaxRate,
            ui::plain(text::keyed(
                mConfig->transactionLog.enabled ? "shop.log.config.log.on" : "shop.log.config.log.off"
            ))
        )
    );
    return std::nullopt;
}

bool Shop::enable() {
    getSelf().getLogger().debug(ui::msg("shop.log.lifecycle.enabling"));
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
    registerTestCommand();
    return true;
}

bool Shop::disable() {
    getSelf().getLogger().debug(ui::msg("shop.log.lifecycle.disabling"));
    unregisterContentProtectionHooks();
    unregisterDestructionProtectionHooks();
    unregisterContainerProtectionListener();
    unregisterSignInteractListener();
    unregisterSignEditListener();
    if (auto const frozen = frozenTradeCount(); frozen > 0) {
        getSelf().getLogger().warn(ui::msg("shop.log.lifecycle.frozen_on_disable", frozen)
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
    mBanList = ItemBanList{};
    return true;
}

} // namespace ss::shop

LL_REGISTER_MOD(ss::shop::Shop, ss::shop::Shop::getInstance());

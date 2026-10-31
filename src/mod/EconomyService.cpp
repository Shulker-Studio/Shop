#include "mod/EconomyService.h"

#include "mod/Config.h"
#include "mod/ScoreboardEconomy.h"
#include "mod/Shop.h"

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include <memory>
#include <optional>

namespace ss::shop {
namespace {

/// 选出来的后端；为空表示还没定下来。
std::shared_ptr<economy::EconomyProvider> gProvider;

/// 解析不出来时的兜底：余额 0，交易必然失败，而不是白送。
economy::NullEconomy gNullBackend;

/// 这些警告每次启用只打一次，别刷屏。
bool gFallbackWarned = false;
bool gNotReadyWarned = false;

void resolveProvider(ShopConfig const& config) {
    auto& logger = Shop::getInstance().getSelf().getLogger();

    // 校验阶段已经拦过一次；走到这儿还认不出就别猜，直接不提供后端。
    auto const parsed = economy::parseBackend(config.economy.backend);
    if (!parsed) {
        logger.error("economy.backend = \"{}\" 认不出来，本次不提供经济后端", config.economy.backend);
        return;
    }
    auto const backend = *parsed;
    if (backend == economy::Backend::LegacyMoney || backend == economy::Backend::Builtin) {
        // 配置校验已经拒过这两个取值；走到这里说明配置被绕过。
        // 不拿记分板顶替：服主写了 legacy_money 就是想让它记账。
        logger.error("economy.backend = \"{}\" 还没实现（10），本次不提供经济后端", economy::backendName(backend));
        return;
    }

    if (backend == economy::Backend::Null) {
        logger.warn("economy.backend = \"null\"：余额恒为 0，交易会全部失败（仅供开发）");
        gProvider = std::make_shared<economy::NullEconomy>();
        return;
    }

    // `auto` 与 `scoreboard` 在 04 是同一条路：10 才在 auto 前面插 LegacyMoney 探测。
    auto const level = ll::service::bedrock::getLevel();
    if (!level) {
        // 世界还没起来：这次先不缓存，下一次用到钱的时候再试。
        if (!gNotReadyWarned) {
            logger.debug("世界还没就绪，经济后端推迟到首次交易时再解析");
            gNotReadyWarned = true;
        }
        return;
    }

    if (auto scoreboard = ScoreboardEconomy::create(config.economy.scoreboardObjective)) {
        logger.info("经济后端：记分板（目标 \"{}\"，不支持离线店主）", config.economy.scoreboardObjective);
        gProvider = std::move(scoreboard);
        return;
    }

    if (!gFallbackWarned) {
        logger.error(
            "没有可用的经济后端：记分板目标 \"{}\" 不存在"
            "（用 `/scoreboard objectives add <名字> dummy` 建一个；自动创建是 10 的事），交易将全部失败",
            config.economy.scoreboardObjective
        );
        gFallbackWarned = true;
    }
}

} // namespace

economy::EconomyProvider& economyBackend() {
    if (gProvider) {
        return *gProvider;
    }
    auto const& config = Shop::getInstance().config();
    if (config) {
        resolveProvider(*config);
    }
    return gProvider ? *gProvider : gNullBackend;
}

bool economyBackendAvailable() { return economyBackend().available(); }

Player* onlinePlayer(std::string_view xuid) {
    if (xuid.empty()) {
        return nullptr;
    }
    auto level = ll::service::bedrock::getLevel();
    if (!level) {
        return nullptr;
    }
    return level->getPlayerByXuid(std::string{xuid});
}

void resetEconomy() {
    gProvider.reset();
    gFallbackWarned = false;
    gNotReadyWarned = false;
}

} // namespace ss::shop

#include "mod/EconomyService.h"

#include "mod/Config.h"
#include "mod/LegacyMoneyEconomy.h"
#include "mod/ScoreboardEconomy.h"
#include "mod/Shop.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include <memory>
#include <optional>
#include <utility>

namespace ss::shop {
namespace {

using economy::Backend;
using economy::BackendCandidates;
using economy::BackendSelection;
using economy::ProbeIssue;

/// 选出来的后端；为空表示还没有可记账的后端（或还没解析）。
std::shared_ptr<economy::EconomyProvider> gProvider;

/// 「已经解析过一次」——包括解析结果是「一个都没有」。
///
/// 不能只看 `gProvider` 是否为空：解析失败时它也是空，那样每次动钱都会重探一遍
/// （白白重复建目标、刷日志）。世界还没起来是唯一的例外，那时 `gResolved` 保持 false。
bool gResolved = false;

/// 解析不出来时的兜底：余额读不到，交易必然失败，而不是白送。
economy::NullEconomy gNullBackend;

/// 这些警告每次启用只打一次，别刷屏。
bool gNotReadyWarned = false;
bool gNoBackendWarned = false;
bool gTaxWarned = false;
bool gTaxBackendWarned = false;

/// 世界起来后立刻探一次，把最终选择打进日志（DESIGN §8.4 第 4 条）。
ll::event::ListenerPtr gServerStartedListener;

/// 一个候选后端探测不到时的中文提示（只在「一个后端都没有」时给服主看，所以写得具体些）。
std::string noBackendHint(ShopConfig const& config, BackendSelection const& selection) {
    std::string hint = "没有可记账的经济后端：交易会全部失败。";
    for (auto const& [backend, issue] : selection.skipped) {
        if (backend == Backend::LegacyMoney && issue == ProbeIssue::ModuleMissing) {
            hint += " 装 LegacyMoney（或带它的 LegacyScriptEngine）后重载本模组即可自动使用它；";
        }
        if (backend == Backend::Scoreboard && issue == ProbeIssue::ObjectiveMissing) {
            hint += " 用 `/scoreboard objectives add " + config.economy.scoreboardObjective
                  + " dummy` 建一个，或把 `economy.createObjectiveIfMissing` 设为 true；";
        }
    }
    hint += " 改完配置或建好目标后需要重载模组。";
    return hint;
}

/// 探测两个候选后端（只在可能用到时才探、才建目标），交给纯函数做选择。
///
/// @return false = 世界还没起来，这次先不定（下次动钱时再试）。
bool resolveProvider(ShopConfig const& config) {
    auto& logger = Shop::getInstance().getSelf().getLogger();

    // 配置校验已经拦过一次；走到这儿还认不出就别猜，直接不提供后端。
    auto const parsed = economy::parseBackend(config.economy.backend);
    if (!parsed) {
        logger.error("economy.backend = \"{}\" 认不出来，本次不提供经济后端", config.economy.backend);
        gResolved = true; // 不再重试：配置错了，重试一万次也还是错
        return true;
    }
    Backend const backend = *parsed;

    if (backend == Backend::Builtin) {
        // 配置校验已经拒过这个取值；走到这里说明配置被绕过。
        logger.error("economy.backend = \"builtin\" 是 P1 的计划（自建账本），本次不提供经济后端");
        gResolved = true;
        return true;
    }

    if (backend == Backend::Null) {
        logger.warn("economy.backend = \"null\"：余额恒为 0，交易会全部失败（仅供开发）");
        gResolved = true;
        return true;
    }

    BackendCandidates candidates;

    // 「探哪几个」也由纯逻辑决定（`probesFor`），mod 层只负责实际去探。
    auto const probes = economy::probesFor(backend);

    // 1. LegacyMoney：模块探测不需要世界，`enable()` 之后随时能问。
    std::shared_ptr<LegacyMoneyEconomy> legacy;
    if (probes.legacyMoney) {
        ProbeIssue issue = ProbeIssue::None;
        legacy           = LegacyMoneyEconomy::create(issue);
        candidates.legacyMoney = {legacy != nullptr, issue};
    }

    // 2. 记分板：只有「想要 legacy 但没探到」或「显式要 scoreboard」时才碰它——
    //    前一级可用就不碰，免得白白在服务器的记分板上建一个用不到的目标。
    //    这里才需要世界（目标在 `Level` 的记分板上），也是唯一会建目标的地方。
    std::shared_ptr<ScoreboardEconomy> scoreboard;
    if (probes.scoreboard && !candidates.legacyMoney.available) {
        auto const level = ll::service::bedrock::getLevel();
        if (!level) {
            if (!gNotReadyWarned) {
                logger.debug("世界还没就绪，经济后端推迟到世界启动后再解析");
                gNotReadyWarned = true;
            }
            return false;
        }
        ProbeIssue issue = ProbeIssue::None;
        scoreboard       = ScoreboardEconomy::create(
            config.economy.scoreboardObjective,
            config.economy.createObjectiveIfMissing,
            issue
        );
        candidates.scoreboard = {scoreboard != nullptr, issue};
    }

    // 3. 选谁由纯函数决定（DESIGN §8.4 的候选顺序只在 `shop/Economy.cpp` 写一次）。
    BackendSelection const selection = economy::selectBackend(backend, candidates);
    std::string const      text      = economy::backendSelectionText(backend, selection);

    switch (selection.chosen) {
    case Backend::LegacyMoney:
        gProvider = std::move(legacy);
        logger.info("{}（支持离线店主，每笔交易一次原子转账）", text);
        break;
    case Backend::Scoreboard:
        gProvider = std::move(scoreboard);
        logger.info("{}（只支持在线店主）", text);
        if (config.tax.shopTaxRate > 0.0 && !gTaxBackendWarned) {
            // 记分板没有「付给系统」的账户，收不走的那笔税只能不收（否则账目对不上）。
            gTaxBackendWarned = true;
            logger.warn(
                "tax.shopTaxRate = {} 但当前后端是记分板：它没有能把钱移出经济系统的账户，"
                "商店税在这个后端上不生效（要收税请装 LegacyMoney）",
                config.tax.shopTaxRate
            );
        }
        break;
    case Backend::Auto:
    case Backend::Builtin:
    case Backend::Null:
        // 空实现：不留 `gProvider`，`economyBackend()` 会返回 `gNullBackend`。
        if (!gNoBackendWarned) {
            gNoBackendWarned = true;
            logger.error("{}。{}", text, noBackendHint(config, selection));
        }
        break;
    }

    gResolved = true;
    return true;
}

/// 解析一次（如果需要），返回能不能记账。
void ensureResolved() {
    if (gResolved) {
        return;
    }
    auto const& config = Shop::getInstance().config();
    if (config) {
        resolveProvider(*config);
    }
}

void warnTaxConflict(ShopConfig const& config) {
    if (config.tax.shopTaxRate <= 0.0 || gTaxWarned) {
        return;
    }
    gTaxWarned = true;
    // `pay_tax` 在 LegacyMoney 自己的配置里，没有任何导出能读它（DESIGN §8.7 第 8 条），
    // 所以这里只能无条件提醒：两笔转账都会被它再抽一次，店主被二次抽税、补偿金额还会漂移。
    Shop::getInstance().getSelf().getLogger().warn(
        "tax.shopTaxRate = {}：商店税已开启。请确认 LegacyMoney 的配置里 pay_tax 已设为 0——"
        "本模组读不到那个值；若 pay_tax 仍大于 0，每笔转账的收款方都会被再扣一次税，"
        "店主会被抽两次，补偿金额也会与实际到账不一致（DESIGN §8.9）",
        config.tax.shopTaxRate
    );
}

} // namespace

void initEconomyService(ShopConfig const& config) {
    warnTaxConflict(config);

    if (gServerStartedListener) {
        return;
    }
    // 世界一就绪就探一次：把最终选中的后端写进启动日志（DESIGN §8.4 第 4 条）。
    // 回调里只调 `economyBackend()`，探测逻辑与首次动钱时完全同一条。
    gServerStartedListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::server::ServerStartedEvent>(
        [](ll::event::server::ServerStartedEvent&) { economyBackend(); }
    );
    if (!gServerStartedListener) {
        Shop::getInstance().getSelf().getLogger().warn("注册服务器启动监听失败：经济后端推迟到首次交易时再探测");
    }
}

economy::EconomyProvider& economyBackend() {
    ensureResolved();
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

bool accountsRecordable(economy::EconomyProvider& provider, std::string_view payer, std::string_view receiver) {
    // `get` 返回空 = 这个账户现在不可记账（不在线、没有这个账户），不是余额 0。
    return provider.get(economy::Account{payer}).has_value() && provider.get(economy::Account{receiver}).has_value();
}

void resetEconomy() {
    if (gServerStartedListener) {
        ll::event::EventBus::getInstance().removeListener<ll::event::server::ServerStartedEvent>(gServerStartedListener);
        gServerStartedListener.reset();
    }
    gProvider.reset();
    gResolved        = false;
    gNotReadyWarned  = false;
    gNoBackendWarned = false;
    gTaxWarned       = false;
    gTaxBackendWarned = false;
}

} // namespace ss::shop

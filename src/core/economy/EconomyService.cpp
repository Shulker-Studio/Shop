#include "core/economy/EconomyService.h"

#include "util/i18n/TextRender.h"

#include "util/log/DebugLog.h"

#include "util/config/Config.h"
#include "core/economy/LegacyMoneyEconomy.h"
#include "core/economy/ScoreboardEconomy.h"
#include "Shop.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
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

/// 最近一次解析出的「配置值 → 选用值」文本，与启动日志同一句。
/// `/shop status` 直接显示它：日志不实时刷盘，这条文本不能只活在日志里。
std::string gSelectionText;

/// 世界起来后立刻探一次，把最终选择打进日志（DESIGN §8.4 第 4 条）。
ll::event::ListenerPtr gServerStartedListener;
/// 玩家进服时再兜一次：真机上 `ServerStartedEvent` 并不可靠（见 `initEconomyService`）。
ll::event::ListenerPtr gPlayerJoinListener;

/// 一个候选后端探测不到时的提示（只在「一个后端都没有」时给服主看，所以写得具体些）。
Text noBackendHint(ShopConfig const& config, Backend configured, BackendSelection const& selection) {
    TextArgs parts;
    parts.push_back(text::keyed(
        "shop.log.economy.no_backend_intro",
        Tone::Default,
        {text::lit(std::string{economy::backendName(configured)})}
    ));
    for (auto const& [backend, issue] : selection.skipped) {
        if (backend == Backend::LegacyMoney && issue == ProbeIssue::ModuleMissing) {
            parts.push_back(text::keyed("shop.log.economy.hint.legacy_missing"));
        }
        if (backend == Backend::LegacyMoney && issue == ProbeIssue::ExportsMissing) {
            parts.push_back(text::keyed("shop.log.economy.hint.legacy_exports"));
        }
        if (backend == Backend::Scoreboard && issue == ProbeIssue::ObjectiveMissing) {
            parts.push_back(text::keyed(
                "shop.log.economy.hint.objective_missing",
                Tone::Default,
                {text::lit(config.economy.scoreboardObjective)}
            ));
        }
        if (backend == Backend::Scoreboard && issue == ProbeIssue::ObjectiveCreateFailed) {
            parts.push_back(text::keyed(
                "shop.log.economy.hint.objective_create_failed",
                Tone::Default,
                {text::lit(config.economy.scoreboardObjective)}
            ));
        }
    }
    // 把「不会自动换账本」写进日志：服主看到「没有后端」时最容易以为装个 LegacyMoney 就会
    // 自动开始记账，而实际是要求他明确改配置——这正是删掉 `auto` 的原因。
    parts.push_back(text::keyed("shop.log.economy.no_backend_outro"));
    return text::sequence(std::move(parts));
}


/// 一次解析的收尾：记下要显示给 `/shop status` 的那句话，打一条调试事件，再标记“不再重试”。
///
/// 所有早退分支都必须走这里——否则 `/shop status` 会把「解析过了但没后端」错报成
/// 「尚未解析（世界未就绪）」，那正好是 D6 要查的两件不同的事。
void finishResolution(std::string selectionText) {
    gSelectionText = std::move(selectionText);
    gResolved      = true;
    shopDebugEvent(
        "economy_resolution",
        {dbgField("selection", gSelectionText)}
    );
}

/// 探测两个候选后端（只在可能用到时才探、才建目标），交给纯函数做选择。
///
/// @return false = 世界还没起来，这次先不定（下次动钱时再试）。
bool resolveProvider(ShopConfig const& config) {
    auto& logger = Shop::getInstance().getSelf().getLogger();

    // 配置校验已经拦过一次；走到这儿还认不出就别猜，直接不提供后端。
    auto const parsed = economy::parseBackend(config.economy.backend);
    if (!parsed) {
        logger.error(ui::msg("shop.log.economy.backend_unrecognized", config.economy.backend));
        finishResolution(text::render(text::keyed(
            "shop.log.economy.unrecognized_selection",
            Tone::Default,
            {text::lit(config.economy.backend)}
        )));
        return true;
    }
    Backend const backend = *parsed;

    if (backend == Backend::Builtin) {
        // 配置校验已经拒过这个取值；走到这里说明配置被绕过。
        logger.error(ui::msg("shop.log.economy.backend_builtin"));
        finishResolution(text::render(text::keyed("shop.log.economy.builtin_selection")));
        return true;
    }

    if (backend == Backend::Null) {
        logger.warn(ui::msg("shop.log.economy.backend_null"));
        finishResolution(text::render(text::keyed("shop.log.economy.null_selection")));
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

    // 2. 记分板：只有配置写着 `scoreboard` 才碰它（没有降级链了，不再需要看别的后端脸色）。
    //    这里才需要世界（目标在 `Level` 的记分板上），也是唯一会建目标的地方。
    std::shared_ptr<ScoreboardEconomy> scoreboard;
    if (probes.scoreboard) {
        auto const level = ll::service::bedrock::getLevel();
        if (!level) {
            if (!gNotReadyWarned) {
                logger.debug(ui::msg("shop.log.economy.world_not_ready"));
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
    Text const             text      = economy::backendSelectionText(backend, selection);
    gSelectionText                   = text::render(text);

    switch (selection.chosen) {
    case Backend::LegacyMoney:
        gProvider = std::move(legacy);
        logger.info(ui::msg("shop.log.economy.legacy_selected", ui::plain(text)));
        break;
    case Backend::Scoreboard:
        gProvider = std::move(scoreboard);
        logger.info(ui::msg("shop.log.economy.scoreboard_selected", ui::plain(text)));
        break;
    case Backend::Builtin:
    case Backend::Null:
        // 空实现：不留 `gProvider`，`economyBackend()` 会返回 `gNullBackend`。
        if (!gNoBackendWarned) {
            gNoBackendWarned = true;
            logger.error(ui::msg(
                    "shop.log.economy.no_backend",
                    ui::plain(text),
                    ui::plain(noBackendHint(config, backend, selection))
                )
            );
        }
        break;
    }

    // 税要一个「能把钱移出经济系统」的动作：当前两个后端都支持（空收款方 = 只扣付款方），
    // 留着这道判据是为了将来加后端时不用再想起来。
    if (gProvider && config.tax.shopTaxRate > 0.0 && !gProvider->supportsMoneySink() && !gTaxBackendWarned) {
        gTaxBackendWarned = true;
        logger.warn(ui::msg("shop.log.economy.tax_not_supported", config.tax.shopTaxRate));
    }

    finishResolution(gSelectionText);
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
    if (gTaxWarned) {
        return;
    }
    // 只有 legacy_money 才受 `pay_tax` 影响：记分板不经过 LegacyMoney。
    //
    // **不以 `shopTaxRate > 0` 为前提**：即使不开商店税，每一笔转账（含补偿退款）
    // 都会被 LegacyMoney 的 `pay_tax` 再抽一次——而退款是按原金额反向转的，
    // 收款方到账少了那一截，于是「钱货两清」在账上就永远差一笔。
    // 这个值没有任何导出能读（DESIGN §8.7 第 8 条），只能无条件提醒。
    if (config.economy.backend != "legacy_money") {
        return;
    }
    gTaxWarned = true;
    Shop::getInstance().getSelf().getLogger().warn(ui::msg("shop.log.economy.pay_tax_warning", config.tax.shopTaxRate)
    );
}

} // namespace

void initEconomyService(ShopConfig const& config) {
    warnTaxConflict(config);

    if (gServerStartedListener && gPlayerJoinListener) {
        return;
    }
    auto& bus    = ll::event::EventBus::getInstance();
    auto& logger = Shop::getInstance().getSelf().getLogger();

    // 世界一就绪就探一次：把最终选中的后端写进启动日志（DESIGN §8.4 第 4 条）。
    // 回调里只调 `economyBackend()`，探测逻辑与首次动钱时完全同一条。
    if (!gServerStartedListener) {
        gServerStartedListener = bus.emplaceListener<ll::event::server::ServerStartedEvent>(
            [](ll::event::server::ServerStartedEvent&) { economyBackend(); }
        );
        if (!gServerStartedListener) {
            logger.warn(ui::msg("shop.log.economy.server_started_listener_failed"));
        }
    }

    // 真机发现（2026-11-01，LL 26.51.9）：世界已经就绪、玩家已经进服，`/shop status` 仍然显示
    // 「尚未解析」——即 `ServerStartedEvent` 并没有在世界的就绪时刻送到。这不能算「等首次交易」
    // 就完事：没交易过的服一整天都会显示没后端，排查时极容易误判成后端不可用。
    //
    // 所以再挂一条「玩家进服」：进服必然意味着世界已经在跑，比 `ServerStartedEvent` 可靠得多，
    // 而且它正是服主第一次需要看余额的时刻。两条都留着：谁先到都只解析一次（`ensureResolved` 幂等）。
    if (!gPlayerJoinListener) {
        gPlayerJoinListener = bus.emplaceListener<ll::event::player::PlayerJoinEvent>(
            [](ll::event::player::PlayerJoinEvent&) { economyBackend(); }
        );
        if (!gPlayerJoinListener) {
            logger.warn(ui::msg("shop.log.economy.player_join_listener_failed"));
        }
    }
}

std::string const& economySelectionText() { return gSelectionText; }

Text economyUnresolvedReason() {
    if (gResolved) {
        return {}; // 已解析却没选型文本只可能是空实现那条路，此时文本本来就非空
    }
    // 「世界还没起来」和「世界起来了但解析没跑」是两件事，`/shop status` 不许把它们混成一句话。
    return text::keyed(
        ll::service::bedrock::getLevel() ? "shop.status.economy.pending_ready"
                                         : "shop.status.economy.pending_world"
    );
}

economy::EconomyProvider& economyBackend() {
    ensureResolved();
    return gProvider ? *gProvider : gNullBackend;
}

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
    //
    // 空账户 = 系统账户（管理员店，DESIGN §7.4）：它本来就不是一个能 `get` 的账户，
    // 能不能用它已由 `supportsSystemAccount()` 在预校验里答过。拿 `get("")` 去问
    // 一定读不到，那样每一笔管理员店交易都会被拦下。
    auto const recordable = [&provider](std::string_view account) {
        return account.empty() || provider.get(economy::Account{account}).has_value();
    };
    return recordable(payer) && recordable(receiver);
}

void resetEconomy() {
    auto& bus = ll::event::EventBus::getInstance();
    if (gServerStartedListener) {
        bus.removeListener<ll::event::server::ServerStartedEvent>(gServerStartedListener);
        gServerStartedListener.reset();
    }
    if (gPlayerJoinListener) {
        bus.removeListener<ll::event::player::PlayerJoinEvent>(gPlayerJoinListener);
        gPlayerJoinListener.reset();
    }
    gProvider.reset();
    gResolved        = false;
    gNotReadyWarned  = false;
    gNoBackendWarned = false;
    gTaxWarned       = false;
    gTaxBackendWarned = false;
    // 选型文本也必须清：`/ll reload Shop` 之后、第一次动钱之前，
    // `/shop status` 不该再显示上一份配置选中的后端（那会让服主以为新配置已经生效）。
    gSelectionText.clear();
}

} // namespace ss::shop

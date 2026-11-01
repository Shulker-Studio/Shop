#include "mod/SignEditListener.h"

#include "mod/ContainerFinderBds.h"
#include "mod/DebugLog.h"
#include "mod/Shop.h"
#include "mod/ShopOwner.h"
#include "mod/ShopService.h"

#include "ila/event/world/actor/player/PlayerEditSignEvent.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/actor/SignTextSide.h"

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

namespace ss::shop {
namespace {

ll::event::ListenerPtr gSignEditListener;
ll::event::ListenerPtr gSignEditedListener;

/// 一次编辑的归属交接：Before 阶段判出，紧接着的 After 阶段用（DESIGN §5.4）。
struct PendingOwnerHandoff {
    Cell             sign;
    int              dimension = 0; ///< `DimensionType` 的整数值：`Cell` 只描述维度内的一格
    std::string      editorXuid;
    SignOwnerHandoff handoff;
};

/// 待消费的交接。做成一个小环而不是单槽：两个玩家同一 tick 各改各的牌时，
/// 单槽会把前一个人的交接顶掉，那条 After 就什么也不写（引擎没写 XUID 的牌子会
/// 静默变成无主牌）。环满时覆盖最旧的一条——Before → After 本来是挨着的，4 条够用。
///
/// 消费规则：从新到旧找第一条「牌位（含维度）+ 编辑者」都对得上的，取走并清掉；
/// 找不到就什么也不做（别的模组、别的手势写的牌子不该动归属）。
constexpr std::size_t kMaxPendingOwnerHandoffs = 4;

std::array<std::optional<PendingOwnerHandoff>, kMaxPendingOwnerHandoffs> gPendingOwnerHandoffs;
std::size_t                                                              gNextPendingOwnerHandoff = 0;

void stashOwnerHandoff(PendingOwnerHandoff pending) {
    gPendingOwnerHandoffs[gNextPendingOwnerHandoff] = std::move(pending);
    gNextPendingOwnerHandoff                        = (gNextPendingOwnerHandoff + 1) % kMaxPendingOwnerHandoffs;
}

/// 取走与这次 After 对得上的那条；没有就返回空。
std::optional<PendingOwnerHandoff> takeOwnerHandoff(Cell const& sign, int dimension, std::string const& editorXuid) {
    for (std::size_t i = 0; i < kMaxPendingOwnerHandoffs; ++i) {
        // 从新的往旧的找（`gNextPendingOwnerHandoff` 的前一格是最新写入的）
        std::size_t const index =
            (gNextPendingOwnerHandoff + kMaxPendingOwnerHandoffs - 1 - i) % kMaxPendingOwnerHandoffs;
        auto& slot = gPendingOwnerHandoffs[index];
        if (!slot || slot->sign != sign || slot->dimension != dimension || slot->editorXuid != editorXuid) {
            continue;
        }
        auto taken = std::move(slot);
        slot.reset();
        return taken;
    }
    return std::nullopt;
}

/// 编辑判定的名字，只给调试日志用。
char const* editOutcomeName(SignEditOutcome outcome) {
    switch (outcome) {
    case SignEditOutcome::NotAShopSign:
        return "NotAShopSign";
    case SignEditOutcome::Rejected:
        return "Rejected";
    case SignEditOutcome::Normalized:
        return "Normalized";
    }
    return "Unknown";
}

using SignEditBeforeEvent = ila::mc::world::actor::player::PlayerEditSignBeforeEvent;
using SignEditAfterEvent  = ila::mc::world::actor::player::PlayerEditSignAfterEvent;

void onSignEdit(SignEditBeforeEvent& event) {
    // 背面是布告板：不读、不写、不校验。
    if (event.textSide() != SignTextSide::Front) {
        return;
    }

    auto const& config = Shop::getInstance().config();
    if (!config) {
        return; // 配置没加载成功时模组不会启用，这里只是兜底
    }

    auto&      player = event.self();
    auto&      region = player.getDimensionBlockSource();
    auto const result = planSignEdit(region, event.pos(), player, event.text(), *config);

    // 编辑路径过去一行日志都没有，真机排查时等于盲区（真机：新牌子被判非法后打不开编辑界面，
    // 只能靠猜）。这里把每次编辑的判定结果记下来，字段与 `sign_decision` 对齐。
    if (shopDebugEnabled()) {
        shopDebugEvent(
            "sign_edit",
            {dbgField("outcome", std::string_view{editOutcomeName(result.outcome)}),
             dbgField("player", player.getXuid()),
             dbgField("pos", dbgPos(region.getDimensionId(), event.pos().x, event.pos().y, event.pos().z)),
             dbgField("reason", result.message)}
        );
    }

    switch (result.outcome) {
    case SignEditOutcome::NotAShopSign:
        return; // 普通告示牌原样放行
    case SignEditOutcome::Rejected: {
        player.sendMessage(result.message);

        // **不 cancel，改成把原文本写回去**（真机 2026-11-01 换来的做法）。
        //
        // 用 cancel 拦下时，客户端那边这次编辑没有正常收尾：之后对这块牌子右键**再也不弹
        // 原版编辑界面**，必须退出重进才行——「显示空白」和「改完还是原样」两种都中。
        // 而「写回原文本」让原版完整走完一次编辑，只是写的仍是原来那几行：牌面不变成
        // 玩家敲的内容，客户端也不再留下半截状态。
        auto* sign = signBlockActorAt(region, event.pos());
        std::string const original = sign != nullptr ? sign->getMessage(SignTextSide::Front) : std::string{};
        event.text() = original;

        // 写回文本会让引擎把 `mLastEditXuid` 记成这次编辑者（§6.5 第 3 条）。改之前如果是
        // 商店牌，就得像规范化那次一样把归属保住——否则 OP 代改一次非法文本就把店抢走了。
        if (auto const before = shopSignOwnerAt(region, event.pos(), *config)) {
            stashOwnerHandoff(
                PendingOwnerHandoff{
                    .sign       = toCell(event.pos()),
                    .dimension  = static_cast<int>(region.getDimensionId()),
                    .editorXuid = player.getXuid(),
                    .handoff    = SignOwnerHandoff{
                        .wasShop           = true,
                        .previousAdminShop = before->adminShop,
                        .previousOwnerXuid = before->xuid,
                    },
                }
            );
        }
        return;
    }
    case SignEditOutcome::Normalized:
        // 改包而不是事后写回：服务端与客户端拿到的是同一份规范化文本。
        event.text() = result.normalizedText;
        // 归属留给 After 阶段（那时文本才真写进去），先把它存下。
        stashOwnerHandoff(
            PendingOwnerHandoff{
                .sign       = toCell(event.pos()),
                .dimension  = static_cast<int>(region.getDimensionId()),
                .editorXuid = player.getXuid(),
                .handoff    = result.ownerHandoff,
            }
        );
        return;
    }
}

/// 牌子已写入后的归属落地。DESIGN §5.4 的 fallback：包路径不保证写 `mLastEditXuid`，
/// 而 ADR-0005 的店主判定依赖它。目标归属来自 Before 阶段存下的交接——不是
/// 「记的 XUID ≠ 编辑者就覆盖」：改牌面不改归属（§5.4）。
void onSignEdited(SignEditAfterEvent const& event) {
    auto& player = event.self();

    // 先认领再判早退：认领了的那条一定要用掉，否则它会在环里被下一次同坐标的编辑捡走。
    auto pending = takeOwnerHandoff(
        toCell(event.pos()),
        static_cast<int>(player.getDimensionBlockSource().getDimensionId()),
        player.getXuid()
    );

    // 背面是布告板：不读、不写、不校验（DESIGN §5.5）。
    if (event.textSide() != SignTextSide::Front) {
        return;
    }

    auto const& config = Shop::getInstance().config();
    if (!config) {
        return;
    }

    if (!pending) {
        return; // 不是我们放行的那次编辑：归属不是我们该动的事
    }

    recordShopOwner(player.getDimensionBlockSource(), event.pos(), player, *config, pending->handoff);
}

} // namespace

void registerSignEditListener() {
    if (gSignEditListener) {
        return;
    }
    auto& bus           = ll::event::EventBus::getInstance();
    gSignEditListener   = bus.emplaceListener<SignEditBeforeEvent>(onSignEdit);
    gSignEditedListener = bus.emplaceListener<SignEditAfterEvent>(onSignEdited);
    if (!gSignEditListener || !gSignEditedListener) {
        Shop::getInstance().getSelf().getLogger().error("注册告示牌编辑监听失败");
    }
}

void unregisterSignEditListener() {
    auto& bus = ll::event::EventBus::getInstance();
    if (gSignEditListener) {
        bus.removeListener<SignEditBeforeEvent>(gSignEditListener);
        gSignEditListener.reset();
    }
    if (gSignEditedListener) {
        bus.removeListener<SignEditAfterEvent>(gSignEditedListener);
        gSignEditedListener.reset();
    }
    for (auto& slot : gPendingOwnerHandoffs) {
        slot.reset();
    }
    gNextPendingOwnerHandoff = 0;
}

} // namespace ss::shop

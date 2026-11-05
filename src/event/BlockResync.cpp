#include "event/BlockResync.h"

#include "ll/api/service/Bedrock.h"

#include "mc/network/packet/BlockActorDataPacket.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/SignBlockActor.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace ss::shop {
namespace {

/// 同一玩家对同一格的重发节流：250ms 内只做一次。
///
/// 真机 2026-11-01：对着被拦下的牌子快速连点，会把客户端顶掉线。重发本身不该是
/// 高频动作，限一次就够了——连点期间客户端看到的东西也不会比上一次更“正确”。
struct LastResync {
    std::string                           xuid;
    int                                   dimension = 0;
    BlockPos                              pos{-1, -1, -1};
    std::chrono::steady_clock::time_point at{};
};

LastResync gLast;

bool withinThrottleWindow(Player const& player, int dimension, BlockPos const& pos) {
    constexpr auto kWindow = std::chrono::milliseconds{250};
    auto const&    xuid    = player.getXuid();
    auto const     now     = std::chrono::steady_clock::now();
    bool const same = gLast.xuid == xuid && gLast.dimension == dimension && gLast.pos.x == pos.x && gLast.pos.y == pos.y
                   && gLast.pos.z == pos.z;
    if (same && now - gLast.at < kWindow) {
        return true;
    }
    gLast = LastResync{.xuid = xuid, .dimension = dimension, .pos = pos, .at = now};
    return false;
}

} // namespace

void resyncBlockActor(Player& player, BlockSource& region, BlockPos const& pos) {
    auto* actor = region.getBlockEntity(pos);
    if (actor == nullptr) {
        return;
    }
    if (withinThrottleWindow(player, static_cast<int>(region.getDimensionId()), pos)) {
        return;
    }

    if (auto packet = actor->_getUpdatePacket(region)) {
        player.sendNetworkPacket(*packet);
        return;
    }

    // `_getUpdatePacket` 在方块实体**没标脏**时返回空——刚放下、还没写过字的牌子就是这种。
    //
    // 这里**不许自己拼 NBT**。早先的退路是 `save(tag, SaveContext{SendOverNetwork})` 再自己
    // 组一个 `BlockActorDataPacket`，真机后果是：牌子显示空白、**打不开编辑界面**、连点几次
    // 客户端直接掉线（2026-11-01 第四报）。原版这个包依赖的不只是 save 出来的那些字段。
    //
    // 正确做法是让原版自己生成：先经 `onChanged`（告示牌自己重写过，会把这一格标脏）
    // 再问一次。还是拿不到就放弃，宁可让客户端多显示一次旧内容，也不发一个拼出来的包。
    if (!actor->isTypeOrDerived(BlockActorType::Sign)) {
        return;
    }
    auto& sign = static_cast<SignBlockActor&>(*actor);
    sign.onChanged(region);
    if (auto packet = actor->_getUpdatePacket(region)) {
        player.sendNetworkPacket(*packet);
    }
}

void resyncBlockActorToDimension(BlockSource& region, BlockPos const& pos) {
    auto* actor = region.getBlockEntity(pos);
    if (actor == nullptr) {
        return;
    }
    auto level = ll::service::bedrock::getLevel();
    if (!level) {
        return;
    }

    // 包只让原版生成，而且**只生成一次**，再广播给整维度：同一份数据不需要每名玩家重新构造
    // （也不能：`_getUpdatePacket` 会把脏标志清掉，第二次就可能拿不到了）。
    std::unique_ptr<BlockActorDataPacket> packet = actor->_getUpdatePacket(region);
    if (!packet) {
        // 没标脏时原版不给包：先 `onChanged` 把这一格标脏再问一次，仍拿不到就放弃。
        // 绝不自己拼 NBT（理由见 `resyncBlockActor`：拼出来的包会让牌子显示空白且打不开）。
        if (!actor->isTypeOrDerived(BlockActorType::Sign)) {
            return;
        }
        static_cast<SignBlockActor&>(*actor).onChanged(region);
        packet = actor->_getUpdatePacket(region);
        if (!packet) {
            return;
        }
    }

    auto const dimension = region.getDimensionId();
    level->forEachPlayer([&](Player& player) {
        if (player.getDimensionId() != dimension) {
            return true; // 不跳维度：别的维度里没有这一格
        }
        player.sendNetworkPacket(*packet);
        return true;
    });
}

} // namespace ss::shop

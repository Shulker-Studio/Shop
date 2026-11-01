#pragma once

#include "mc/world/level/BlockPos.h"

namespace ll::event {
class Event;
}

class BlockSource;
class Player;

namespace ss::shop {

/// 把一格的方块实体（告示牌文字、箱子内容等）重新发一份给某个玩家。
///
/// **为什么需要**：客户端对交互有本地预演——非店主拿染料点牌、或者对着商店牌按下
/// 挖掘键时，客户端会先把「染色 / 牌子碎掉」演出来，方块实体也跟着被本地删掉。
/// 服务端这时 `event.cancel()` 只拦住权威状态，客户端的预演不会自动回滚，
/// 于是出现真机现象：**拦住是对的，但牌面文字空着，退出重进才回来**
/// （2026-11-01 两次踩到：染料/墨囊/蜜蜡点击、拆牌拦截）。
///
/// 所以凡是「拦下但客户端会预演」的地方，取消事件之后都要调它一次。
/// 只发给动手的那个玩家，不广播——别人的客户端本来就没预演过。
///
/// 首选 `BlockActor::_getUpdatePacket`（原版那条路，可能还带额外字段）；
/// 它在「没标脏」时可能返回空，退路是自己按“要发到网络”的语境存一份 NBT 再发。
void resyncBlockActor(Player& player, BlockSource& region, BlockPos const& pos);

} // namespace ss::shop

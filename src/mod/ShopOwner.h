#pragma once

#include "mod/Config.h"
#include "shop/ShopSign.h"

#include <string>

class BlockPos;
class BlockSource;
class ServerPlayer;
class SignBlockActor;

namespace ss::shop {

/// 取这一格的告示牌方块实体；不是告示牌（或没有方块实体）时返回空。
///
/// 告示牌与悬挂牌都算，形态判定交给 `readSignGeometry`。
SignBlockActor* signBlockActorAt(BlockSource& region, BlockPos const& pos);

/// 一块商店牌的店主身份。来源是牌子正面的 `SignBlockActor::Text::mLastEditXuid`
/// （ADR-0005），与第 1 行的文字无关——第 1 行只是显示名。
struct ShopOwner {
    /// 正面记录的编辑者 XUID。引擎没写时是空串（DESIGN §6.5 第 3 条）。
    std::string xuid;
    /// 由 XUID 反查到的当前名字；查不到时退回牌面第 1 行的文字，再查不到就是空串。
    std::string displayName;
    /// 管理员商店（第 1 行 == `adminShopName`）。管理权只给 OP，见 DESIGN §11。
    bool adminShop = false;
};

/// 读取店主。
///
/// `parsedFront` 必须是这块牌子正面文本的解析结果（`parseShopSignText(front, true, config)`）——
/// 传进来而不是在这里重解析，是因为调用方刚算过一次，一次点击不该解析两遍。
///
/// 管理员商店按**牌面文字**判定，不按「此刻是谁在看」判定：如果用点击者的 OP 状态
/// 冒充 `editorIsOp`，非 OP 访客会把已有的管理员店解析成普通店，店主名也会错。
ShopOwner readShopOwner(SignBlockActor& sign, ShopSignResult const& parsedFront);

/// 这块牌的正面是不是一块合法商店牌（只看文本，不看容器）。
bool isShopSignFront(SignBlockActor& sign, ShopSignConfig const& config);

/// 补写店主 XUID：包路径不保证写 `mLastEditXuid`（DESIGN §6.5 第 3 条），
/// 所以在编辑落地后补一次显式写入。
///
/// 三个条件同时成立时动手：正面已是合法商店牌、附着容器可用、记录的 XUID 与本次
/// 编辑者不一致——普通牌、非法牌、失去容器的牌一个字都不碰。
///
/// 注意：这里**不**判编辑者是不是店主。非店主能不能改到正面由 06 负责
/// （DESIGN §9.2：交互阶段拦下 + 提交阶段兜底取消），本函数只管把 ownerID 跟上
/// 实际发生的那次写入，两者职责不重叠。
///
/// 写入的是**当前文本本身**，不 refresh——客户端已经通过包拿到同一份文本，
/// 再刷一次只会抖一下。
///
/// `config` 用完整配置，因为要复用附着容器判定（`shopContainers` 白名单）。
void ensureOwnerRecorded(BlockSource& region, BlockPos const& signPos, ServerPlayer& editor, ShopConfig const& config);

} // namespace ss::shop

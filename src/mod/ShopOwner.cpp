#include "mod/ShopOwner.h"

#include "mod/ContainerFinderBds.h"

#include "ll/api/service/PlayerInfo.h"

#include "mc/server/ServerPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/actor/SignTextSide.h"

namespace ss::shop {

ShopSignResult parseShopSignFront(SignBlockActor& sign, ShopSignConfig const& config) {
    return parseShopSignText(sign.getMessage(SignTextSide::Front), /*editorIsOp=*/true, config);
}

bool isShopSignFront(SignBlockActor& sign, ShopSignConfig const& config) {
    return parseShopSignFront(sign, config).ok();
}

ShopOwner readShopOwner(SignBlockActor& sign, ShopSignResult const& parsedFront) {
    ShopOwner owner;

    // 管理员店靠牌面文字判定：规范化时普通店的第 1 行会被写成编辑者名字，
    // 所以牌面上还留着 `adminShopName` 的，只可能是 OP 建的管理员店（或外部工具写入）。
    // 不能拿点击者的 OP 状态冒充 `editorIsOp`：非 OP 访客会把已有的管理员店解析成普通店。
    owner.adminShop = parsedFront.ok() && parsedFront.spec->adminShop;

    // `mLastEditXuid` 是 `SignBlockActor::Text` 上的 `TypedStorage<8, 32, std::string>`，
    // 隐式转成 `std::string const&`。
    if (sign.mTextFront) {
        std::string const& recorded = sign.mTextFront->mLastEditXuid;
        owner.xuid                  = recorded;
    }

    if (!owner.xuid.empty()) {
        if (auto entry = ll::service::PlayerInfo::getInstance().fromXuid(owner.xuid)) {
            owner.displayName = entry->name;
        }
    }
    if (owner.displayName.empty()) {
        // 离线太久、PlayerInfo 里没有记录，或者引擎压根没写 XUID：
        // 退回牌面第 1 行（规范化时它被写成了编辑者的名字）。
        owner.displayName = splitSignText(sign.getMessage(SignTextSide::Front))[SignLine::OwnerLine];
    }
    return owner;
}

void recordShopOwner(
    BlockSource&            region,
    BlockPos const&         signPos,
    ServerPlayer&           editor,
    ShopConfig const&       config,
    SignOwnerHandoff const& handoff
) {
    auto* sign = signBlockActorAt(region, signPos);
    if (sign == nullptr) {
        return;
    }

    auto const signConfig = toSignConfig(config);
    auto const parsed     = parseShopSignFront(*sign, signConfig);
    if (!parsed.ok()) {
        return; // 普通告示牌、非法商店牌：一个字都不碰
    }

    // 附着容器不可用（牌子被挪走了、容器被换掉了）就不算商店，不写店主。
    // 只判 `.ref`（附着格是不是容器），与禁售无关：传 `nullptr` 让这条路径不受禁售影响。
    ContainerLookupOptions const options{config.shopContainers, signConfig, nullptr};
    if (!inspectAttachedContainer(region, signPos, options).ref) {
        return;
    }

    if (!sign->mTextFront) {
        return;
    }

    // 目标归属：改之前已经是同一家商店就保住原店主；第一次成为商店（或管理员店
    // 与普通店互转）就写给编辑者。判定只调 `keepsPreviousOwner`，与第 1 行写谁同源。
    bool const  keepPrevious = handoff.keepsPreviousOwner(parsed.spec->adminShop);
    std::string desired      = keepPrevious ? handoff.previousOwnerXuid : editor.getXuid();

    std::string const& recorded = sign->mTextFront->mLastEditXuid;
    if (recorded == desired) {
        return; // 牌上已经是对的（引擎自己写对了，或上次就写过了）
    }

    // 文本原样写回，只把 ownerID 纠成目标值（不 refresh，见头文件）。
    sign->setMessageForServerScripingOnly(SignTextSide::Front, sign->getMessage(SignTextSide::Front), desired);
}

} // namespace ss::shop

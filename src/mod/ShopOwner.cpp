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

bool isShopSignFront(SignBlockActor& sign, ShopSignConfig const& config) {
    // 这里传 `editorIsOp = true` 只影响 `adminShop` 字段，不影响 `ok()`：
    // 判定「是不是商店牌」和第 1 行是不是管理员名无关。
    return parseShopSignText(sign.getMessage(SignTextSide::Front), true, config).ok();
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

void ensureOwnerRecorded(BlockSource& region, BlockPos const& signPos, ServerPlayer& editor, ShopConfig const& config) {
    auto* sign = signBlockActorAt(region, signPos);
    if (sign == nullptr) {
        return;
    }

    auto const signConfig = toSignConfig(config);
    if (!isShopSignFront(*sign, signConfig)) {
        return; // 普通告示牌、非法商店牌：一个字都不碰
    }

    // 附着容器不可用（牌子被挪走了、容器被换掉了）就不算商店，不写店主。
    ContainerLookupOptions const options{config.shopContainers, signConfig};
    if (!inspectAttachedContainer(region, signPos, options).ref) {
        return;
    }

    if (!sign->mTextFront) {
        return;
    }
    std::string const& recorded = sign->mTextFront->mLastEditXuid;
    if (recorded == editor.getXuid()) {
        return; // 引擎已经写好了，什么也不用做
    }

    // 文本原样写回，只把 ownerID 换成这次的编辑者。
    sign->setMessageForServerScripingOnly(SignTextSide::Front, sign->getMessage(SignTextSide::Front), editor.getXuid());
}

} // namespace ss::shop

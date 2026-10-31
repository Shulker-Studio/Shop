#include "mod/ContainerFinderBds.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/Container.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/states/BlockState.h"
#include "mc/world/level/block/states/BlockStateInstance.h"
#include "shop/ShopSign.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ss::shop {
namespace {

bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

/// 方块状态的运行时名字带命名空间（`minecraft:facing_direction`），比较时两边都认。
bool stateNameIs(std::string_view fullName, std::string_view wanted) {
    constexpr std::string_view kNamespace = "minecraft:";
    if (fullName == wanted) {
        return true;
    }
    return fullName.size() == kNamespace.size() + wanted.size() && fullName.starts_with(kNamespace)
        && fullName.substr(kNamespace.size()) == wanted;
}

/// 按名字在方块的方块状态表里找状态 ID。找不到返回空。
std::optional<uint64> stateIdByName(BlockType const& type, std::string_view name) {
    for (auto const& [id, instance] : type.mStates.get()) {
        HashedString const& stateName = instance.mState->mName;
        if (stateNameIs(stateName.getString(), name)) {
            return id;
        }
    }
    return std::nullopt;
}

std::optional<int> readIntState(Block const& block, std::string_view name) {
    auto const id = stateIdByName(block.getBlockType(), name);
    if (!id) {
        return std::nullopt;
    }
    return block.getState<int>(*id);
}

std::optional<bool> readBoolState(Block const& block, std::string_view name) {
    auto const id = stateIdByName(block.getBlockType(), name);
    if (!id) {
        return std::nullopt;
    }
    return block.getState<bool>(*id);
}

/// `facing_direction`（2 北 / 3 南 / 4 西 / 5 东）或 `cardinal_direction`
/// （`Direction::Type`：0 南 / 1 西 / 2 北 / 3 东）→ 水平朝向。
///
/// 朝向是牌子正面**朝外**的方向，附着点在它的反方向（PowerNukkit 的
/// `BlockWallSign::onUpdate` 用 `getBlockFace().getOpposite()` 找支撑方块）。
std::optional<HorizontalFacing> readHorizontalFacing(Block const& block) {
    if (auto facing = readIntState(block, "facing_direction")) {
        switch (*facing) {
        case 2:
            return HorizontalFacing::North;
        case 3:
            return HorizontalFacing::South;
        case 4:
            return HorizontalFacing::West;
        case 5:
            return HorizontalFacing::East;
        default:
            return std::nullopt;
        }
    }
    if (auto cardinal = readIntState(block, "cardinal_direction")) {
        switch (*cardinal) {
        case 0:
            return HorizontalFacing::South;
        case 1:
            return HorizontalFacing::West;
        case 2:
            return HorizontalFacing::North;
        case 3:
            return HorizontalFacing::East;
        default:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

BlockPos toBlockPos(Cell const& cell) { return BlockPos{cell.x, cell.y, cell.z}; }

Cell toCell(BlockPos const& pos) { return Cell{pos.x, pos.y, pos.z}; }

SignBlockActor* signBlockActorAt(BlockSource& region, BlockPos const& pos) {
    auto* actor = region.getBlockEntity(pos);
    if (actor == nullptr) {
        return nullptr;
    }
    if (!actor->isTypeOrDerived(BlockActorType::Sign) && !actor->isTypeOrDerived(BlockActorType::HangingSign)) {
        return nullptr;
    }
    return static_cast<SignBlockActor*>(actor);
}

bool isAllowedType(std::string const& typeName, std::vector<std::string> const& allowed) {
    for (auto const& candidate : allowed) {
        if (candidate == typeName) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> firstItemName(Container& container) {
    for (int slot = 0; slot < container.getContainerSize(); ++slot) {
        ItemStack const& stack = container.getItem(slot);
        if (!stack.isNull()) {
            return stack.getName();
        }
    }
    return std::nullopt;
}

/// 这一格上的牌是不是「合法商店牌」。只有合法商店牌才算占用了容器。
bool isShopSign(BlockSource& region, BlockPos const& pos, ShopSignConfig const& signConfig) {
    auto* sign = signBlockActorAt(region, pos);
    if (sign == nullptr) {
        return false;
    }
    return parseShopSignText(sign->getMessage(SignTextSide::Front), false, signConfig).ok();
}

/// 收集容器（含双箱另一半）六个面周围的牌子，交给纯逻辑判定冲突。
std::vector<NeighbourSign>
collectNeighbourSigns(BlockSource& region, ContainerRef const& container, ShopSignConfig const& signConfig) {
    std::vector<NeighbourSign> signs;
    auto const                 collectAround = [&](Cell const& containerCell) {
        for (Cell const& cell : neighboursOf(containerCell)) {
            auto const geometry = readSignGeometry(region, toBlockPos(cell));
            if (!geometry) {
                continue;
            }
            signs.push_back({*geometry, isShopSign(region, toBlockPos(cell), signConfig)});
        }
    };

    collectAround(container.pos);
    if (container.pairedPos) {
        collectAround(*container.pairedPos);
    }
    return signs;
}

} // namespace

std::optional<SignRef> readSignGeometry(BlockSource& region, BlockPos const& signPos) {
    if (signBlockActorAt(region, signPos) == nullptr) {
        return std::nullopt;
    }

    Block const&      block = region.getBlock(signPos);
    std::string const name  = block.getTypeName();

    // 先判悬挂牌：`..._hanging_sign` 也以 `sign` 结尾，但不会以 `wall_sign` 结尾。
    if (endsWith(name, "hanging_sign")) {
        auto const hanging = readBoolState(block, "hanging");
        // 只认「吊在方块底面」：侧挂形态在基岩版里判不出挂在哪一面墙
        // （`facing_direction` 与墙面垂直），猜错会绑到旁边的另一个容器上。
        // 状态读不到也不猜，直接当作形态未知。
        if (!hanging || !*hanging) {
            return std::nullopt;
        }
        return SignRef{toCell(signPos), SignForm::HangingFromCeiling, HorizontalFacing::North};
    }

    if (endsWith(name, "wall_sign")) {
        auto const facing = readHorizontalFacing(block);
        if (!facing) {
            return std::nullopt;
        }
        return SignRef{toCell(signPos), SignForm::Wall, *facing};
    }

    if (endsWith(name, "standing_sign")) {
        return SignRef{toCell(signPos), SignForm::Standing, HorizontalFacing::North};
    }

    return std::nullopt;
}

ContainerLookup
inspectAttachedContainer(BlockSource& region, BlockPos const& signPos, ContainerLookupOptions const& options) {
    auto const geometry = readSignGeometry(region, signPos);
    if (!geometry) {
        return {.error = ContainerError::UnknownSignShape};
    }

    Cell const   attached    = attachedCellOfSign(geometry->pos, geometry->form, geometry->facing);
    BlockPos const attachedPos = toBlockPos(attached);

    auto* actor = region.getBlockEntity(attachedPos);
    if (actor == nullptr
        || (!actor->isTypeOrDerived(BlockActorType::Chest) && !actor->isTypeOrDerived(BlockActorType::BarrelBlock))) {
        return {.error = ContainerError::NotAContainer};
    }
    if (!isAllowedType(region.getBlock(attachedPos).getTypeName(), options.allowedBlockTypes)) {
        return {.error = ContainerError::NotAllowedContainer};
    }

    auto*       chest        = static_cast<ChestBlockActor*>(actor);
    Container*  primary      = chest;
    Container*  secondary    = nullptr;
    ContainerRef ref{attached, primary->getContainerSize(), std::nullopt};
    if (auto* paired = chest->mLargeChestPaired) {
        secondary      = paired;
        ref.pairedPos  = toCell(chest->mLargeChestPairedPosition);
        ref.size      += secondary->getContainerSize();
    }

    auto const signs = collectNeighbourSigns(region, ref, options.signConfig);
    if (isBoundByAnotherShopSign(signs, ref, geometry->pos)) {
        return {.ref = ref, .error = ContainerError::AlreadyBound};
    }

    // 双箱没有统一的全局槽位顺序：按「附着那一半在前、另一半在后」取商品。
    auto itemName = firstItemName(*primary);
    if (!itemName && secondary != nullptr) {
        itemName = firstItemName(*secondary);
    }
    if (!itemName) {
        return {.ref = ref, .error = ContainerError::Empty};
    }

    return {.ref = ref, .error = ContainerError::None, .itemName = std::move(*itemName)};
}

} // namespace ss::shop

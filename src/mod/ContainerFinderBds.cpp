#include "mod/ContainerFinderBds.h"

#include "mod/ShopOwner.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
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

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ss::shop {

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

BlockPos toBlockPos(Cell const& cell) { return BlockPos{cell.x, cell.y, cell.z}; }

Cell toCell(BlockPos const& pos) { return Cell{pos.x, pos.y, pos.z}; }

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

bool isAllowedType(std::string const& typeName, std::vector<std::string> const& allowed) {
    for (auto const& candidate : allowed) {
        if (candidate == typeName) {
            return true;
        }
    }
    return false;
}

/// 按合并后的逻辑槽位遍历：先附着那一半，再另一半。
///
/// 顺序与边界由 `mergedSlots` 说了算，这里不再自己算一遍。
/// `fn` 返回 false 就提前结束（找到商品模板后不用再扫剩下的格子）。
template <class Fn>
void forEachMergedSlot(Container& primary, Container* secondary, int slots, Fn&& fn) {
    for (auto const& slot : mergedSlots(primary, secondary, slots)) {
        if (!fn(slot.container->getItem(slot.slot))) {
            return;
        }
    }
}

/// 商品模板 = 商店箱里（双箱合并）第一个非空槽位的物品（附着那一半优先，DESIGN §5.3）。
ItemStack const* firstNonEmptyStack(Container& primary, Container* secondary, int slots) {
    ItemStack const* product = nullptr;
    forEachMergedSlot(primary, secondary, slots, [&](ItemStack const& stack) {
        if (stack.isNull()) {
            return true;
        }
        product = &stack;
        return false;
    });
    return product;
}

/// 这一格上的牌是不是「合法商店牌」。只有合法商店牌才算占用了容器。
bool isShopSign(BlockSource& region, BlockPos const& pos, ShopSignConfig const& signConfig) {
    auto* sign = signBlockActorAt(region, pos);
    if (sign == nullptr) {
        return false;
    }
    return isShopSignFront(*sign, signConfig);
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

std::int64_t countMatchingItems(Container& primary, Container* secondary, int slots, ItemStackBase const& product) {
    std::int64_t total = 0;
    forEachMergedSlot(primary, secondary, slots, [&](ItemStack const& stack) {
        if (!stack.isNull() && sameShopItem(stack, product)) {
            total += stack.mCount;
        }
        return true;
    });
    return total;
}

std::vector<MergedSlot> mergedSlots(Container& primary, Container* secondary, int size) {
    std::vector<MergedSlot> slots;
    if (size <= 0) {
        return slots;
    }
    slots.reserve(static_cast<std::size_t>(size));

    int remaining = size;
    for (int slot = 0; slot < primary.getContainerSize() && remaining > 0; ++slot, --remaining) {
        slots.push_back({&primary, slot});
    }
    if (secondary == nullptr) {
        return slots;
    }
    int const limit = std::min(secondary->getContainerSize(), remaining);
    for (int slot = 0; slot < limit; ++slot) {
        slots.push_back({secondary, slot});
    }
    return slots;
}

bool sameShopItem(ItemStackBase const& lhs, ItemStackBase const& rhs) {
    if (lhs.getId() != rhs.getId() || lhs.getAuxValue() != rhs.getAuxValue()) {
        return false;
    }
    if (lhs.mUserData == nullptr || rhs.mUserData == nullptr) {
        return lhs.mUserData == rhs.mUserData;
    }
    return lhs.mUserData->equals(*rhs.mUserData);
}

ItemStack const* firstProductStack(AttachedContainer const& attached) {
    if (attached.primary == nullptr) {
        return nullptr;
    }
    return firstNonEmptyStack(*attached.primary, attached.secondary, attached.ref.size);
}

AttachedContainerResult attachedContainerOf(
    BlockSource&                    region,
    BlockPos const&                 signPos,
    std::vector<std::string> const& allowedBlockTypes
) {
    auto const geometry = readSignGeometry(region, signPos);
    if (!geometry) {
        return {.error = ContainerError::UnknownSignShape};
    }

    Cell const     attached    = attachedCellOfSign(geometry->pos, geometry->form, geometry->facing);
    BlockPos const attachedPos = toBlockPos(attached);

    auto* actor = region.getBlockEntity(attachedPos);
    if (actor == nullptr
        || (!actor->isTypeOrDerived(BlockActorType::Chest) && !actor->isTypeOrDerived(BlockActorType::BarrelBlock))) {
        return {.error = ContainerError::NotAContainer};
    }
    if (!isAllowedType(region.getBlock(attachedPos).getTypeName(), allowedBlockTypes)) {
        return {.error = ContainerError::NotAllowedContainer};
    }

    // `BarrelBlockActor` 本身派生自 `ChestBlockActor`（mc/world/level/block/actor/BarrelBlockActor.h），
    // 所以这个向下转型对木桶也合法；木桶永远不会配对，双箱分支自然走不到。
    auto*        chest     = static_cast<ChestBlockActor*>(actor);
    Container*   primary   = chest;
    Container*   secondary = nullptr;
    ContainerRef ref{attached, primary->getContainerSize(), std::nullopt};
    if (auto* paired = chest->mLargeChestPaired) {
        secondary     = paired;
        ref.pairedPos = toCell(chest->mLargeChestPairedPosition);
        ref.size      += secondary->getContainerSize();
    }

    return {.container = AttachedContainer{*geometry, ref, primary, secondary}};
}

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
    auto const attached = attachedContainerOf(region, signPos, options.allowedBlockTypes);
    if (!attached.ok()) {
        return {.error = attached.error};
    }

    AttachedContainer const& box = *attached.container;
    auto const               signs = collectNeighbourSigns(region, box.ref, options.signConfig);
    if (isBoundByAnotherShopSign(signs, box.ref, box.sign.pos)) {
        return {.ref = box.ref, .error = ContainerError::AlreadyBound};
    }

    // 商品 = 商店箱里（双箱合并）第一个非空槽位的物品（附着那一半优先，DESIGN §5.3）。
    ItemStack const* product = firstNonEmptyStack(*box.primary, box.secondary, box.ref.size);
    if (product == nullptr) {
        return {.ref = box.ref, .error = ContainerError::Empty};
    }

    return {
        .ref       = box.ref,
        .error     = ContainerError::None,
        .itemName  = product->getName(),
        .itemCount = countMatchingItems(*box.primary, box.secondary, box.ref.size, *product),
    };
}

} // namespace ss::shop

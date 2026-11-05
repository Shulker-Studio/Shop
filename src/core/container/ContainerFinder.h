#pragma once

#include "util/config/Config.h"
#include "core/shop/SignOwner.h"
#include "core/sign/SignGeometry.h"
#include "core/shop/ItemBanList.h"
#include "core/shop/ShopInfoData.h" // BoxCell：盒内内容要在表单上显示

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class BlockPos;
class BlockSource;
class Container;
class ItemStack;
class ItemStackBase;

namespace ss::shop {

/// 查看附着容器的结果。`None` 表示可以开店。
enum class ContainerError {
    None,
    /// 这一格不是告示牌，或读不出它的形态（例如侧挂的悬挂牌）。
    UnknownSignShape,
    /// 附着的那一格压根不是容器。
    NotAContainer,
    /// 是容器，但类型不在 `shopContainers` 白名单里（服主配置问题）。
    NotAllowedContainer,
    /// 容器是空的：没有商品可卖。
    Empty,
    /// 商品是容器类物品（潜影盒），但**盒内物品不整齐**：混装、或每格数量不同。
    /// 与 `Empty` 是两件事：箱子有货，只是这份货没法用一个说的清的身份定价。
    MixedContainerContents,
    /// 商品被服主禁售（`bannedItems`）：要么商品本身的 ID 命中，要么它盒子里嵌套的
    /// 某一件物品命中。这种店开不起来，已经开着的也交易不了。
    /// **管理员店不会看到这个错误**：禁售只管玩家开的店（见 `ContainerLookupOptions`）。
    BannedItem,
    /// 商品里**有收纳袋**（它自己就是，或者装在潜影盒里）：收纳袋的内容存在服务端的动态容器
    /// 注册表里，读不出来，于是既不能核对里面有什么，也没法把内容算进商品身份 —— 所以不许当商品
    /// （`core/shop/BundleRules.h` 的 `isBundleItemId` + `productHoldsBundle` 的递归）。
    /// 这一条**不分店的性质**：它是能力限制（读不到内容），不是服主的政策，管理员店也一样。
    BundleProduct,
    /// 这个容器已经被另一块合法商店牌绑定了。
    AlreadyBound,
};

struct ContainerLookup {
    /// 找到的容器（双箱已合并）。定位成功时就有值，即使 `error` 是
    /// `Empty`/`AlreadyBound`——这两种情况下容器本身是存在的。
    std::optional<ContainerRef> ref;
    ContainerError              error = ContainerError::None;
    /// 商品（第一个非空槽位的物品）的展示名；容器为空时是空串。
    std::string itemName;
    /// 箱子里**有没有**商品（第一个非空槽位）。`itemName` 不能代替它：
    /// 展示名可能为空，而且「更新商品名」按钮只该在有货时给。
    bool hasProduct = false;
    /// 容器类商品（潜影盒）盒里装的东西，按格内内容去重合并（`productBoxContents`）。
    /// 空 = 不是容器类商品，或盒子是空的。
    ///
    /// **如实给出盒里的一切**（不整齐、被禁售时也照给），但表单只在它只有一项时才显示：
    /// 两种以上就已经被 `MixedContainerContents` 拦下来了，正文那句红字比一份内容清单有用。
    std::vector<BoxCell> boxContents;
    /// 商店箱里与商品**严格匹配**、且**可卖**的件数（双箱合并；类型 + aux + 用户数据/盒内内容）。
    /// 不含样品槽那一份（样品槽永不出售，`saleableSlotOrder`），也不含内容不一致的其他盒。
    /// 可成交份数 = 它 / 一份的件数（见 `unitsFromItems`）。
    std::int64_t saleableItemCount = 0;

    [[nodiscard]] bool ok() const { return error == ContainerError::None && ref.has_value(); }
};

struct ContainerLookupOptions {
    /// 可作商店箱的方块类型（`minecraft:chest` 这类带命名空间的 ID）。
    std::vector<std::string> const& allowedBlockTypes;
    /// 判断「这个容器已经被别的商店牌绑定了」需要解析别人的牌面。
    ShopSignConfig const& signConfig;
    /// 禁售表。**传 `nullptr` 就是不判禁售**，而且这是**管理员店**的唯一写法：
    /// 管理员店是服主自己摆的货架（样品箱、凭空造货），要卖什么由服主决定，
    /// 禁售表是拦玩家的。
    ItemBanList const* bannedItems = nullptr;
};

/// 附着容器的本体：主容器 + 另一半（双箱）。
///
/// `ContainerLookup` 回答「这个容器能不能当商店」；这里只回答「它是哪个对象」，
/// 给真正要读写物品的代码（交易）用。
///
/// `secondary` **只有在引擎没把两半合成一个逻辑容器时**才有值：
/// BDS 的 `ChestBlockActor` 配对之后可能已经把自己当成 54 格逻辑容器（见 `containerAt` 的实测说明），
/// 那时候再把另一半当成第二份库存就会把每一格数两遍。
struct AttachedContainer {
    /// 牌子的几何（位置 + 形态 + 朝向）。定位时已经算过一次，带上免得再读一遍方块状态。
    SignRef      sign;
    ContainerRef ref;
    Container*   primary   = nullptr; ///< 牌子贴着的那一半（或单箱、木桶）
    Container*   secondary = nullptr; ///< 双箱的另一半；木桶、单箱、以及引擎已自己合并时为空
};

/// 商品模板在合并顺序里的位置。
struct ProductSlot {
    int              index = 0;         ///< 合并顺序里的下标，也就是**样品槽**
    ItemStack const* stack = nullptr;   ///< 那一格里的物品（引用箱子槽位，**一改动就失效**）
};

/// 商品模板 = 商店箱里（双箱合并）**第一个非空槽位**及其下标（`sampleSlotIndex`）。
/// 容器空了返回空。
///
/// 下标必须一起返回：样品槽永不出售（`saleableSlotOrder`），而“第几格”只有这里算得出来。
/// 只给 `ItemStack const*` 会让调用方再扫一遍箱子找位置——同一件事两个真相。
std::optional<ProductSlot> productSlotOf(AttachedContainer const& attached);

struct AttachedContainerResult {
    std::optional<AttachedContainer> container;
    ContainerError                   error = ContainerError::None;

    [[nodiscard]] bool ok() const { return container.has_value(); }
};

/// 合并槽位：先附着那一半、再另一半。
///
/// 双箱没有全局索引，跨两半的「第 N 格」只能靠这个顺序推出来，
/// 所以顺序与边界只在这里定义一次。
struct MergedSlot {
    Container* container = nullptr;
    int        slot      = 0;
};

/// 只定位附着容器：不判断商店占用、不读商品。
///
/// 失败原因复用 `ContainerError`：`UnknownSignShape` / `NotAContainer` / `NotAllowedContainer`。
AttachedContainerResult
attachedContainerOf(BlockSource& region, BlockPos const& signPos, std::vector<std::string> const& allowedBlockTypes);

/// 合并后的全部槽位；`size` 是合并后的总格数（`ContainerRef::size`）。
std::vector<MergedSlot> mergedSlots(Container& primary, Container* secondary, int size);

/// 严格同种：类型 + aux + 用户数据（附魔、自定义名），**容器类物品再加上盒内内容**。
///
/// 只看展示名或 ID 会把「钻石剑」和「附魔钻石剑」算成同一种商品，
/// 那会让一个商店用错价的物品结账（DESIGN §5.3）。
///
/// 容器类物品（潜影盒）的商品身份包含盒内内容，而且是**当作多重集**比的
/// （收纳袋读不出内容，整类不许当商品，见 `productHoldsBundle`）：
/// 盒内每格的物品与数量都要对得上，**格位不算**（空格更不算）。这样一来“装满钻石的盒子”
/// 与“空的盒子”、“64 个钻石的盒子”与“1 个钻石的盒子”都是不同的商品——否则空盒可以
/// 按满盒价卖出、或者 1 个钻石按 64 个卖出，两边都是套利。
bool sameShopItem(ItemStackBase const& lhs, ItemStackBase const& rhs);

/// 商品是不是「容器类物品但盒内不整齐」：有内容的容器，却存在某格和非空第一格不一致
/// （同物品、同数量）；空格不参与判定。非容器类物品一律返回 `false`。
///
/// 判据只有这一份：开店时（`inspectAttachedContainer`）、点牌时（`readShop`）
/// 与提交交易时（`prepareCommon`）都问它，免得“表单说能买、提交说不能”。
bool containerProductMixed(ItemStackBase const& product);

/// 这件商品是不是禁售的：ID 命中禁售表，或者它是容器类物品、而盒内（含多层嵌套）
/// 有任何一件物品的 ID 命中禁售表。
///
/// “嵌套也算”是刻意的：一条 `minecraft:bedrock` 规则如果只看外层，玩家把基岩装进
/// 潜影盒就能绕过去——而盒子的内容也是商品身份的一部分，绕过去的就是“卖禁售品”本身。
/// 读不出盒内结构时退到外层判断（猜错只可能多放行一层，不会把能卖的东西禁掉）。
///
/// 只看 ID，不看展示名与 aux：展示名随语言/资源包变，aux 区分不了颜色相同的同一个 ID。
bool productBanned(ItemStackBase const& product, ItemBanList const& bannedItems);

/// 容器类商品（潜影盒）盒里装的东西，按格内内容去重合并后的清单（给表单显示用）。
///
/// 同一格内容的重复出现合并成一项、件数相加；不是容器类商品、盒子是空的、或 NBT 读不出来时
/// 返回空。**只读**：不判整齐、不判禁售，那些各有各的判据（`containerProductMixed` /
/// `productBanned`）——这里回答的只有“买家看到的是什么”。
[[nodiscard]] std::vector<BoxCell> productBoxContents(ItemStackBase const& product);

/// 商品本身、或它盒子里（任意一层）有没有**收纳袋**。
///
/// 收纳袋的内容存在服务端的动态容器注册表里（`bundle_id`），服务端读不出来——所以它既不能自己
/// 当商品，也不能被装进潜影盒当商品：那一格的 tag 里只有个引用，袋里的东西换了 tag 不会变，
/// 身份比较会继续判"同一种"，等于留了一条偷换的缝。**与禁售表无关**，也**不分普通店/管理员店**。
[[nodiscard]] bool productHoldsBundle(ItemStackBase const& product);

/// 一组容器里（`slots` 是合并后的总格数，单容器就是 `getContainerSize()`）
/// 与 `product` **严格同种**的总件数。
///
/// `sampleSlot` 非空时只数**可卖的那些格**（`saleableSlotOrder`：倒序、跳过样品槽），
/// 于是“总件数”与“可卖件数”是同一个函数、同一个谓词，只是范围不同。
/// 商店箱的商品统计与「玩家手里有几份」都用它：容器是参数，比较在那里只有一份。
std::int64_t countMatchingItems(
    Container&            primary,
    Container*            secondary,
    int                   slots,
    ItemStackBase const&  product,
    std::optional<int>    sampleSlot = std::nullopt
);

/// 读出牌子的形态与朝向。不是告示牌、或判不出形态时返回空。
std::optional<SignRef> readSignGeometry(BlockSource& region, BlockPos const& signPos);

/// 引擎坐标 ↔ 纯逻辑坐标。两边的转换只在这里定义。
BlockPos toBlockPos(Cell const& cell);
Cell     toCell(BlockPos const& pos);

/// 找出这一格上的商店牌所附着的容器，并报告开店前需要知道的三件事：
/// 容器身份（双箱已合并）、它是否已被别的商店牌占用、第一个非空槽位是什么。
///
/// 只判断附着的那一格，不扫六面；`signPos` 是正在编辑的牌子，
/// 「已被别的牌绑定」的检查不会把它自己算成冲突。
ContainerLookup
inspectAttachedContainer(BlockSource& region, BlockPos const& signPos, ContainerLookupOptions const& options);

/// 绑在一块容器上的商店牌：几何（拆牌用坐标）+ 店主（保护判定用）。
struct AttachedShopSign {
    SignRef   sign;
    SignOwner owner;
};

/// 反查：这一格容器被哪些商店牌绑着（DESIGN §9.3）。
///
/// 方向与 `inspectAttachedContainer` 相反——那个是「牌子 → 容器」，这个是「容器 → 牌子」。
/// 单箱只看自己六面，双箱连另一半的六面一起看。返回空 = 这一格不是商店箱
/// （普通箱子、不是容器、没有商店牌绑着都返回空）。
///
/// 两处刻意的宽松，与 `inspectAttachedContainer` 不同：
/// - **不看 `shopContainers` 白名单**：改配置不该让一个已经存在的商店箱失去保护；
/// - **牌面用 `looksLikeShopSign`（像商店）而不是 `ok()`**：调小 `maxShopAmount`、
///   改 `adminShopName`、外部工具写脏数据，都不该让保护整体失效（DESIGN §9.2）。
///
/// 只读取：不写方块、不发消息。
std::vector<AttachedShopSign>
attachedShopSignsAt(BlockSource& region, BlockPos const& containerPos, ShopConfig const& config);

/// 反查：附着格正好是这一格的商店牌（DESIGN §6.2 H3e）。
///
/// 与 `attachedShopSignsAt` 是同一根轴上的两个口径：那个问「这一格**容器**绑着哪些牌」
/// （要求这一格是容器、且牌子真的绑在它上面，§9.3），这个只问「哪些牌的附着格是这一格」
/// ——墙牌贴在它侧面、站立牌立在它顶面、吊牌吊在它底面都算，不管这一格是不是容器。
/// 拆掉这一格牌子会物理掉落、商店随之消失，所以就算它只是块石头也要保护：
/// 牌面「像不像商店牌」的判据不看附着容器（§9.2）。
///
/// 只看六个正邻居（附着格必在其中），不扫更大范围。返回空 = 这一格上没挂着商店牌。
std::vector<AttachedShopSign>
shopSignsWithAttachedCell(BlockSource& region, BlockPos const& attachedCell, ShopConfig const& config);

/// 方块状态 `facing_direction` 的原始值（0 下 / 1 上 / 2 北 / 3 南 / 4 西 / 5 东）。
///
/// 投掷器靠它决定把物品喷向哪一格（DESIGN §6.2 H2d）：`pushOutItems` 只拿到 `BlockSource`，
/// 朝向只能从方块状态读。读不到返回空，调用方放行——脏状态不该让一次搬运被误拦。
/// 方向编号换成坐标这一步在纯逻辑层（`core/container/ContentProtection.h` 的 `cellInDirection`）。
std::optional<int> facingDirectionAt(BlockSource& region, BlockPos const& pos);

/// 这一格是不是被商店占用的方块：商店牌本身，或绑着商店牌的容器（DESIGN §9.3）。
///
/// 判据只在这里组装一次：破坏防护（08）的四条 hook 与 07 的攻击/拆除路径都问它，
/// 不各写一份「店主 || 挂着的牌」。
/// 不看 `shopContainers` 白名单，牌面用「像商店」而不是「现在严格合法」（同 §9.2）。
///
/// 实现上先看「有没有方块实体」：商店牌与箱子/木桶都必有，没方块实体的格子一定不是。
/// 爆炸的方块列表动辄上百格，这一句能挡掉大部分，不必每格都解析牌面文本。
///
/// **高频路径（漏斗 / 投掷器 / 铜傀儡）不要用它**，用 `isShopContainerBlockAt`：
/// 那些 hook 只关心容器，而这一支还要判「是不是商店牌」，多出来的成本要乘上
/// 「漏斗数 × 20」——见下一个函数。
bool isShopBlockAt(BlockSource& region, BlockPos const& pos, ShopConfig const& config);

/// 这一格是不是**商店箱**（绑着商店牌的容器）。漏斗 / 投掷器 / 铜傀儡专用。
///
/// 与 `isShopBlockAt` 的差别只有两点，但都是为高频路径量的：
/// 1. 不认「商店牌本身」——那几个 hook 只往容器里搬，牌子不是容器，永远搬不进去；
/// 2. 在六邻居扫描之前多一道「这一格是不是箱子/木桶」的粗筛。漏斗链、熔炉、
///    另一只漏斗都会在这里被挡掉，不必付 `attachedShopSignsAt` 的字符串与牌面解析。
bool isShopContainerBlockAt(BlockSource& region, BlockPos const& pos, ShopConfig const& config);

/// 这一格告示牌是不是一块商店牌；是的话返回它的店主。
///
/// 判据与容器保护一致：只看正面文本「像不像商店牌」，不看附着容器
/// （DESIGN §9.2：箱子被拆了牌面仍然受保护）。普通告示牌返回空。
std::optional<SignOwner> shopSignOwnerAt(BlockSource& region, BlockPos const& signPos, ShopConfig const& config);

} // namespace ss::shop

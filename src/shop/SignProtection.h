#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ss::shop {

/// 「这个人对这块商店牌算什么」——ADR-0005 的店主判定只写在这一个函数里。
///
/// 不解析玩家名、不引入第二套归属：店主 = 牌子正面记录的编辑者 XUID，与第 1 行的
/// 文字无关（第 1 行只是显示名）。OP 先判（DESIGN §11：OP 有全部权限），
/// 所以 OP 同时也是店主时算 `Operator`。
enum class ShopActor : std::uint8_t {
    Other,           ///< 游客
    Owner,           ///< 店主本人
    Operator,        ///< OP
    NoRecordedOwner, ///< 牌子没记下店主 XUID（DESIGN §6.5 第 3 条）：谁都不是店主
};

/// 判定身份。`recordedOwnerXuid` 为空表示牌子没记下店主。
ShopActor shopActorOf(std::string_view recordedOwnerXuid, std::string_view actorXuid, bool actorIsOp);

/// 能不能拿到管理入口（点牌时的表单角色）。
///
/// **fail-closed**：管理入口只给店主与 OP。记录不到店主时不给任何人——一个查不到主人的
/// 店不该把「管理商店」按钮发给路过的每一个人。
bool canManageShop(ShopActor actor);

/// 能不能改这块商店牌的正面文本（DESIGN §9.2）。
///
/// 交互阶段（潜行右键能不能进编辑界面）与提交阶段（这个包要不要丢）用**同一个**判据，
/// 不各写一份：
/// - 游客：不放；
/// - 店主、OP：放；
/// - 记录不到店主：放（fail-open，DESIGN §9.2「失败即放行」）。正常路径一定会写下
///   店主 XUID（`recordShopOwner`），记录缺失的牌不该因此永久锁死；代价是这种异常牌
///   会被第一个改它的人接手，已记在票面的已知边界里。
bool mayEditShopSignText(ShopActor actor);

/// 提交阶段的一道总闸：这块牌的正面文本现在能不能被这个人改。
///
/// 返回空 = 放行；非空 = 直接发给玩家的拒绝理由。「谁能改」与「上蜡冻住了」两条规则
/// 以及两句话都只写在这里，`planSignEdit` 只负责凑参数。
///
/// 顺序是先身份后上蜡：非店主在上蜡的牌上拿到的是「不是你的店」，不泄露「这块牌上了蜡」。
std::optional<std::string>
signTextEditRejection(ShopActor actor, bool waxed, std::string const& ownerName, bool adminShop);

/// 非店主想改别人的商店牌时的提示。管理员商店单独一句：它的管理权只给 OP（§11）。
std::string ownerOnlyEditMessage(std::string const& ownerName, bool adminShop);

/// 牌子上了蜡、改不动时的提示：连店主也要先除蜡（DESIGN §9.2）。
std::string waxedSignEditMessage();

/// 手持物品对告示牌会做的事（DESIGN §9.2 的「牌面防护」那一组）。
///
/// 前四支对应引擎 `SignBlock::SignInteractionResult::Result` 里的 `UseDye` /
/// `UseInkSac` / `UseGlowInkSac` / `UseHoneyComb`；`Axe`（除蜡）不在那个枚举里——
/// 斧头是物品自己的 `useOn` 分支，`FailWaxedSign` 只表示「上蜡的牌不给编辑」。
enum class SignTouch : std::uint8_t {
    None,       ///< 不会改牌子：空手、普通物品
    Dye,        ///< 染料：改文字颜色
    InkSac,     ///< 墨囊：取消发光字
    GlowInkSac, ///< 荧光墨囊：发光字
    HoneyComb,  ///< 蜜蜡：上蜡（冻结编辑）
    Axe,        ///< 斧头：除蜡
};

/// 从物品类型名判 `SignTouch`。
///
/// 归一化（小写、去命名空间前缀、只留字母与数字）让判定不依赖引擎给的是
/// `minecraft:red_dye` 还是展示名——两种写法都会落到同一个键。
///
/// 表是白名单：不认识的一律 `None`。误判成 `None` 只会让店主少一次染色、多弹一次表单，
/// 不会给非店主开出一条改牌的路——方向是安全的。
SignTouch classifySignTouch(std::string_view itemTypeName);

/// 一次点击该做什么。
enum class SignTouchAction : std::uint8_t {
    TakeOver,    ///< 取消原版交互，弹商店表单
    PassThrough, ///< 不拦截：交给原版的染色 / 墨囊 / 上蜡 / 除蜡 / 编辑界面
    Block,       ///< 取消原版交互，并把原因告诉玩家
};

struct SignTouchDecision {
    SignTouchAction action = SignTouchAction::TakeOver;
    /// 只有 `Block` 时非空：发给玩家的一句话。
    std::string message;
};

/// 判定一次点击需要的既成事实。写成结构体而不是一串 bool：五个布尔实参没人读得懂。
struct SignTouchContext {
    SignTouch touch       = SignTouch::None;  ///< 手里那件物品会做什么
    ShopActor actor       = ShopActor::Other; ///< 这个人是谁（`shopActorOf` 的结果）
    bool      sneaking    = false;            ///< 潜行右键 = 想进原版编辑界面改文本
    bool      waxed       = false;            ///< 这块牌已经上蜡
    bool      facingFront = true;             ///< 玩家看的是正面（背面是布告板，DESIGN §5.5）
};

/// 点一块**商店牌**时该怎么做（DESIGN §9.2、§11、§13 第 14～20 条）。
///
/// 调用方已经确认过「这一格是商店牌」（正面文本像商店牌，§5.1.1）。规则：
///
/// - 空手/普通物品 + 看背面 → 放行：背面是布告板，原版编辑界面照弹（§5.5）。
/// - 空手/普通物品 + 看正面 → 弹表单；**潜行**右键则放行原版编辑界面，这是 P0 里
///   唯一能改牌面的入口（§10.1 的 P0 现状）。谁能这样进，问的是
///   `mayEditShopSignText`——与提交阶段同一个判据。上蜡时改不了，直接拦下并提示先除蜡。
/// - 染料 / 墨囊 / 荧光墨囊 / 蜜蜡 / 斧头 → 与看哪一面无关：上蜡改的是整块牌子，
///   染色与墨囊也只该由店主来按。店主与 OP 放行，其余拦下。
/// - 斧头只在牌子真的上了蜡时才是「除蜡操作」；没上蜡时那一下什么都不改，当作普通点击。
SignTouchDecision planSignTouch(SignTouchContext const& context);

} // namespace ss::shop

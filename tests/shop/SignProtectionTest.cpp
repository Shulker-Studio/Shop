#include "shop/SignProtection.h"

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ss::shop;

/// 一次判定的读法：`ctx` 只写关心的那几个事实，其余用 `SignTouchContext` 的默认值
/// （正面、空手、游客、未上蜡）。`touch` 一律由物品名算出来。
SignTouchDecision touch(std::string_view itemName, SignTouchContext ctx = {}) {
    ctx.touch = classifySignTouch(itemName);
    return planSignTouch(ctx);
}

} // namespace

TEST_CASE("物品分类：16 种染料都算「改颜色」") {
    for (auto const* name :
         {"minecraft:red_dye",
          "minecraft:orange_dye",
          "minecraft:yellow_dye",
          "minecraft:lime_dye",
          "minecraft:green_dye",
          "minecraft:cyan_dye",
          "minecraft:light_blue_dye",
          "minecraft:blue_dye",
          "minecraft:purple_dye",
          "minecraft:magenta_dye",
          "minecraft:pink_dye",
          "minecraft:brown_dye",
          "minecraft:white_dye",
          "minecraft:gray_dye",
          "minecraft:light_gray_dye",
          "minecraft:black_dye"}) {
        CHECK(classifySignTouch(name) == SignTouch::Dye);
    }
}

TEST_CASE("物品分类：类型名与展示名算出同一类") {
    CHECK(classifySignTouch("minecraft:red_dye") == SignTouch::Dye);
    CHECK(classifySignTouch("Red Dye") == SignTouch::Dye);
    CHECK(classifySignTouch("RED_DYE") == SignTouch::Dye);
    CHECK(classifySignTouch("minecraft:honeycomb") == SignTouch::HoneyComb);
    CHECK(classifySignTouch("Honeycomb") == SignTouch::HoneyComb);
}

TEST_CASE("物品分类：墨囊与荧光墨囊分开") {
    CHECK(classifySignTouch("minecraft:ink_sac") == SignTouch::InkSac);
    CHECK(classifySignTouch("minecraft:glow_ink_sac") == SignTouch::GlowInkSac);
}

TEST_CASE("物品分类：蜜蜡只认蜜蜡，不认蜜蜡块") {
    CHECK(classifySignTouch("minecraft:honeycomb") == SignTouch::HoneyComb);
    CHECK(classifySignTouch("minecraft:honeycomb_block") == SignTouch::None);
}

TEST_CASE("物品分类：斧头按材质逐个认，镐不算斧头") {
    for (auto const* name :
         {"minecraft:wooden_axe",
          "minecraft:stone_axe",
          "minecraft:copper_axe",
          "minecraft:golden_axe",
          "minecraft:iron_axe",
          "minecraft:diamond_axe",
          "minecraft:netherite_axe"}) {
        CHECK(classifySignTouch(name) == SignTouch::Axe);
    }
    // 「pickaxe」也以 axe 结尾，不能按后缀套
    CHECK(classifySignTouch("minecraft:diamond_pickaxe") == SignTouch::None);
    CHECK(classifySignTouch("minecraft:golden_shovel") == SignTouch::None);
}

TEST_CASE("物品分类：白名单没收录的染料/斧头按形状兜底") {
    // 新版本加颜色或材质时，别静默放行——兜底归到对应的一类
    CHECK(classifySignTouch("minecraft:coral_dye") == SignTouch::Dye);
    CHECK(classifySignTouch("minecraft:emerald_axe") == SignTouch::Axe);
    // 镐也以 axe 结尾，但它不改牌子
    CHECK(classifySignTouch("minecraft:copper_pickaxe") == SignTouch::None);
    CHECK(classifySignTouch("minecraft:axe") == SignTouch::None);
}

TEST_CASE("物品分类：不认识的一律当作不改牌子") {
    CHECK(classifySignTouch("") == SignTouch::None);
    CHECK(classifySignTouch("minecraft:bone_meal") == SignTouch::None);
    CHECK(classifySignTouch("minecraft:lapis_lazuli") == SignTouch::None);
    CHECK(classifySignTouch("minecraft:diamond") == SignTouch::None);
    CHECK(classifySignTouch("minecraft:air") == SignTouch::None);
}

TEST_CASE("身份判定：OP 先判，其次管理员店，再比 XUID") {
    CHECK(shopActorOf("xuid-a", "xuid-a", /*actorIsOp=*/false, /*adminShop=*/false) == ShopActor::Owner);
    CHECK(shopActorOf("xuid-a", "xuid-b", false, false) == ShopActor::Other);
    CHECK(shopActorOf("xuid-a", "xuid-b", /*actorIsOp=*/true, false) == ShopActor::Operator);
    // OP 同时也是店主时仍算 OP（两边的权限一样，operator 更好解释）
    CHECK(shopActorOf("xuid-a", "xuid-a", true, false) == ShopActor::Operator);
    // 牌子没记下店主：谁都不是店主，也不是游客
    CHECK(shopActorOf("", "xuid-a", false, false) == ShopActor::NoRecordedOwner);
    CHECK(shopActorOf("", "xuid-a", true, false) == ShopActor::Operator);
}

TEST_CASE("身份判定：管理员商店只归 OP，牌上记的创建者不靠") {
    // 牌上记的是「当时的创建者」；他被撤权之后不该继续当店主（§11）
    CHECK(shopActorOf("xuid-ex-op", "xuid-ex-op", /*actorIsOp=*/false, /*adminShop=*/true) == ShopActor::Other);
    CHECK(shopActorOf("xuid-ex-op", "xuid-other", false, true) == ShopActor::Other);
    // 记录为空也一样：管理员店的信息不成问题
    CHECK(shopActorOf("", "xuid-other", false, true) == ShopActor::Other);
    // 现任 OP 照旧有全部权限
    CHECK(shopActorOf("xuid-ex-op", "xuid-op", /*actorIsOp=*/true, true) == ShopActor::Operator);
}

TEST_CASE("身份判定：管理入口只给店主与 OP，记录缺失时不发给所有人") {
    CHECK(canManageShop(ShopActor::Owner));
    CHECK(canManageShop(ShopActor::Operator));
    CHECK_FALSE(canManageShop(ShopActor::Other));
    CHECK_FALSE(canManageShop(ShopActor::NoRecordedOwner));
}

TEST_CASE("改牌权限：只有游客被挡，记录缺失时 fail-open") {
    CHECK(mayEditShopSignText(ShopActor::Owner));
    CHECK(mayEditShopSignText(ShopActor::Operator));
    CHECK_FALSE(mayEditShopSignText(ShopActor::Other));
    // DESIGN §9.2：记录缺失的牌不该因此永久锁死（fail-open）
    CHECK(mayEditShopSignText(ShopActor::NoRecordedOwner));
}

TEST_CASE("交互决策：看正面点牌弹商店表单，店主与游客都一样") {
    CHECK(touch("", {.actor = ShopActor::Other}).action == SignTouchAction::TakeOver);
    CHECK(touch("minecraft:diamond", {.actor = ShopActor::Owner}).action == SignTouchAction::TakeOver);
    // 上蜡不影响交易：照样弹表单（§13 第 20 条「商店功能照常可交易」）
    CHECK(touch("", {.actor = ShopActor::Other, .waxed = true}).action == SignTouchAction::TakeOver);
}

TEST_CASE("交互决策：看背面点牌放行，原版编辑界面照弹") {
    CHECK(touch("", {.actor = ShopActor::Other, .facingFront = false}).action == SignTouchAction::PassThrough);
    CHECK(touch("", {.actor = ShopActor::Owner, .facingFront = false}).action == SignTouchAction::PassThrough);
    // 背面是布告板：游客潜行也不接管
    CHECK(
        touch("", {.actor = ShopActor::Other, .sneaking = true, .facingFront = false}).action
        == SignTouchAction::PassThrough
    );
}

TEST_CASE("交互决策：潜行右键是编辑入口，谁能进与提交阶段同一个判据") {
    CHECK(touch("", {.actor = ShopActor::Owner, .sneaking = true}).action == SignTouchAction::PassThrough);
    CHECK(touch("", {.actor = ShopActor::Operator, .sneaking = true}).action == SignTouchAction::PassThrough);
    // 非店主潜行不放行：潜行不能成为绕过交互保护的通道
    CHECK(touch("", {.actor = ShopActor::Other, .sneaking = true}).action == SignTouchAction::TakeOver);
    // 记录缺失的牌：文本是 fail-open 的一侧，第一个改它的人接手（与 `mayEditShopSignText` 一致）
    CHECK(touch("", {.actor = ShopActor::NoRecordedOwner, .sneaking = true}).action == SignTouchAction::PassThrough);
    // 但不潜行的普通点击仍然是表单
    CHECK(touch("", {.actor = ShopActor::NoRecordedOwner}).action == SignTouchAction::TakeOver);
}

TEST_CASE("提交闸：身份与上蜡两条规则、两句话只在一处") {
    // 店主、OP、无主牌都放行
    CHECK_FALSE(signTextEditRejection(ShopActor::Owner, false, "Alex", false).has_value());
    CHECK_FALSE(signTextEditRejection(ShopActor::Operator, false, "Alex", false).has_value());
    CHECK_FALSE(signTextEditRejection(ShopActor::NoRecordedOwner, false, "", false).has_value());
    // 游客不放，理由指名店主
    auto const blocked = signTextEditRejection(ShopActor::Other, false, "Alex", false);
    REQUIRE(blocked.has_value());
    CHECK(blocked->find("Alex") != std::string::npos);
    // 上了蜡的牌连店主与 OP 也不放
    for (auto const actor : {ShopActor::Owner, ShopActor::Operator}) {
        auto const waxed = signTextEditRejection(actor, /*waxed=*/true, "Alex", false);
        REQUIRE(waxed.has_value());
        CHECK(waxed->find("除蜡") != std::string::npos);
    }
    // 先身份后上蜡：非店主在上蜡的牌上拿到的是「不是你的店」，不泄露蜡的状态
    auto const both = signTextEditRejection(ShopActor::Other, /*waxed=*/true, "Alex", false);
    REQUIRE(both.has_value());
    CHECK(both->find("Alex") != std::string::npos);
    CHECK(both->find("除蜡") == std::string::npos);
    // 管理员商店单独一句（§11）
    auto const admin = signTextEditRejection(ShopActor::Other, false, "Alex", /*adminShop=*/true);
    REQUIRE(admin.has_value());
    CHECK(admin->find("OP") != std::string::npos);
}

TEST_CASE("交互决策：上蜡后店主自己也进不了编辑界面，提示先除蜡") {
    auto const decision = touch("", {.actor = ShopActor::Owner, .sneaking = true, .waxed = true});
    CHECK(decision.action == SignTouchAction::Block);
    CHECK(decision.message.find("除蜡") != std::string::npos);
}

TEST_CASE("交互决策：非店主的染料、墨囊、荧光墨囊、蜜蜡、斧头全部拦下") {
    for (auto const* name :
         {"minecraft:red_dye",
          "minecraft:ink_sac",
          "minecraft:glow_ink_sac",
          "minecraft:honeycomb",
          "minecraft:iron_axe"}) {
        auto const decision = touch(name, {.actor = ShopActor::Other, .waxed = true});
        CHECK(decision.action == SignTouchAction::Block);
        CHECK_FALSE(decision.message.empty());
    }
    // 上蜡与否都拦：没上蜡的牌也不能让别人先染个色
    CHECK(touch("minecraft:red_dye", {.actor = ShopActor::Other}).action == SignTouchAction::Block);
    // 潜行也拦：潜行不是非店主的后门
    CHECK(touch("minecraft:honeycomb", {.actor = ShopActor::Other, .sneaking = true}).action == SignTouchAction::Block);
    // 记录缺失的牌也一样：不做权限判定不等于把牌子交出去
    CHECK(touch("minecraft:honeycomb", {.actor = ShopActor::NoRecordedOwner}).action == SignTouchAction::Block);
}

TEST_CASE("交互决策：看背面也不能给别人的商店牌上蜡、染色") {
    // 上蜡改的是整块牌子：从背面刷一下，店主就被冻住了
    CHECK(
        touch("minecraft:honeycomb", {.actor = ShopActor::Other, .facingFront = false}).action == SignTouchAction::Block
    );
    CHECK(
        touch("minecraft:iron_axe", {.actor = ShopActor::Other, .waxed = true, .facingFront = false}).action
        == SignTouchAction::Block
    );
    CHECK(
        touch("minecraft:red_dye", {.actor = ShopActor::Other, .facingFront = false}).action == SignTouchAction::Block
    );
    // 店主从背面也照样放行
    CHECK(
        touch("minecraft:honeycomb", {.actor = ShopActor::Owner, .facingFront = false}).action
        == SignTouchAction::PassThrough
    );
}

TEST_CASE("交互决策：店主的染料、墨囊、荧光墨囊、蜜蜡交给原版处理") {
    for (auto const* name :
         {"minecraft:red_dye", "minecraft:ink_sac", "minecraft:glow_ink_sac", "minecraft:honeycomb"}) {
        auto const decision = touch(name, {.actor = ShopActor::Owner});
        CHECK(decision.action == SignTouchAction::PassThrough);
        CHECK(decision.message.empty());
    }
}

TEST_CASE("交互决策：斧头只在牌子真的上了蜡时才是「除蜡操作」") {
    CHECK(
        touch("minecraft:iron_axe", {.actor = ShopActor::Owner, .waxed = true}).action == SignTouchAction::PassThrough
    );
    // 非店主拿着斧头点已上蜡的牌：除不了蜡（§13 第 18 条）
    CHECK(touch("minecraft:iron_axe", {.actor = ShopActor::Other, .waxed = true}).action == SignTouchAction::Block);
    // 没上蜡就没有蜡可除：这一下什么都不改，就当普通点击（正面弹表单、背面放行），谁都一样
    CHECK(touch("minecraft:iron_axe", {.actor = ShopActor::Owner}).action == SignTouchAction::TakeOver);
    CHECK(touch("minecraft:iron_axe", {.actor = ShopActor::Other}).action == SignTouchAction::TakeOver);
    CHECK(
        touch("minecraft:iron_axe", {.actor = ShopActor::Other, .facingFront = false}).action
        == SignTouchAction::PassThrough
    );
}

TEST_CASE("交互决策：每种操作的拦下提示都说到点子上") {
    CHECK(touch("minecraft:red_dye", {.actor = ShopActor::Other}).message.find("颜色") != std::string::npos);
    CHECK(touch("minecraft:ink_sac", {.actor = ShopActor::Other}).message.find("文字样式") != std::string::npos);
    CHECK(touch("minecraft:glow_ink_sac", {.actor = ShopActor::Other}).message.find("文字样式") != std::string::npos);
    CHECK(touch("minecraft:honeycomb", {.actor = ShopActor::Other}).message.find("上蜡") != std::string::npos);
    CHECK(
        touch("minecraft:iron_axe", {.actor = ShopActor::Other, .waxed = true}).message.find("除蜡")
        != std::string::npos
    );
}

TEST_CASE("交互决策：分类漏判时非店主仍然安全") {
    // 白名单认不出这件物品 → 当作普通点击 → 仍然是「取消 + 弹表单」，不是放行
    auto const decision = touch("minecraft:some_new_dye_item", {.actor = ShopActor::Other});
    CHECK(decision.action == SignTouchAction::TakeOver);
    CHECK(decision.message.empty());
}

TEST_CASE("归属交接：只有「改前已经是同一家商店」才保住原店主") {
    SignOwnerHandoff handoff;
    handoff.wasShop           = true;
    handoff.previousAdminShop = false;
    handoff.previousOwnerXuid = "xuid-a";

    // 改前就是同一家普通商店：保住原店主（OP 代改、引擎自己写 XUID 都靠这条纠回来）
    CHECK(handoff.keepsPreviousOwner(/*newAdminShop=*/false));
    // 改前不是商店 → 这次编辑让它成了商店，归属写编辑者
    CHECK_FALSE(SignOwnerHandoff{.wasShop = false, .previousOwnerXuid = "xuid-a"}.keepsPreviousOwner(false));
    // 改前没记下店主 → fail-open，写编辑者
    CHECK_FALSE(SignOwnerHandoff{.wasShop = true, .previousOwnerXuid = ""}.keepsPreviousOwner(false));
    // 管理员店 ⇄ 普通店互转算换了家店，不保住原店主（§11：管理权只给 OP）
    CHECK_FALSE(handoff.keepsPreviousOwner(/*newAdminShop=*/true));
}

TEST_CASE("改牌提示：分得清有主的、没名字的、管理员商店的") {
    CHECK(ownerOnlyEditMessage("Alex", /*adminShop=*/false).find("Alex") != std::string::npos);
    CHECK(ownerOnlyEditMessage("", /*adminShop=*/false).find("不是你的") != std::string::npos);
    // 空名字不能拼出「是  的」这种半句话
    CHECK(ownerOnlyEditMessage("", false).find("是 ") == std::string::npos);
    // 管理员商店的管理权只给 OP（DESIGN §11）
    CHECK(ownerOnlyEditMessage("Alex", /*adminShop=*/true).find("OP") != std::string::npos);
}

TEST_CASE("拦下提示：各类操作各有说法，无主牌只提 OP") {
    std::vector<std::string> const messages{
        touch("minecraft:red_dye", {.actor = ShopActor::Other}).message,
        touch("minecraft:ink_sac", {.actor = ShopActor::Other}).message,
        touch("minecraft:glow_ink_sac", {.actor = ShopActor::Other}).message,
        touch("minecraft:honeycomb", {.actor = ShopActor::Other}).message,
        touch("minecraft:iron_axe", {.actor = ShopActor::Other, .waxed = true}).message,
    };
    // 五句话都不空；染色、上蜡、除蜡各说各的（墨囊与荧光墨囊共用一句，是有意为之）
    for (auto const& message : messages) {
        CHECK_FALSE(message.empty());
    }
    CHECK(messages[0] != messages[3]);
    CHECK(messages[0] != messages[4]);
    CHECK(messages[3] != messages[4]);

    // 没记下店主的牌：说「只有 OP」，不说「只有店主」——那块牌压根没有店主
    auto const noOwner = touch("minecraft:honeycomb", {.actor = ShopActor::NoRecordedOwner}).message;
    CHECK(noOwner.find("只有 OP") != std::string::npos);
    CHECK(noOwner.find("店主") == std::string::npos);
}

TEST_CASE("上蜡提示：一句话说清要先除蜡") {
    // 「先拿斧头除蜡」：说清要怎么做，不是只说「不行」
    CHECK(waxedSignEditMessage().find("除蜡") != std::string::npos);
    CHECK(waxedSignEditMessage().find("斧头") != std::string::npos);
}

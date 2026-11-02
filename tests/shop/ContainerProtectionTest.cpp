#include "shop/ContainerProtection.h"

#include "../TextHelper.h"

#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ss::shop;

/// 一次判定的读法： 只写关心的那几个事实，其余用默认值（游客、配置允许店主开箱、
/// 店主叫 Alex、普通商店）。身份由调用方给，不在这里重复实现 `shopActorOf`。
ShopBlockDecision decide(
    ShopBlockTouch touch,
    ShopActor      actor,
    bool           allowOwnerOpenChest = true,
    std::string    ownerName           = "Alex",
    bool           adminShop           = false
) {
    return planShopBlockTouch({
        .touch               = touch,
        .actor               = actor,
        .allowOwnerOpenChest = allowOwnerOpenChest,
        .ownerName           = std::move(ownerName),
        .adminShop           = adminShop,
    });
}

} // namespace

TEST_CASE("开箱: 游客打不开商店箱，提示里说得清是谁的店") {
    auto const decision = decide(ShopBlockTouch::OpenChest, ShopActor::Other);
    CHECK_FALSE(decision.allowed);
    CHECK(plainText(decision.message).find("Alex") != std::string::npos);
    CHECK(plainText(decision.message).find("商店箱") != std::string::npos);
}

TEST_CASE("开箱: 配置允许时店主能开自己的商店箱") {
    CHECK(decide(ShopBlockTouch::OpenChest, ShopActor::Owner, /*allowOwnerOpenChest=*/true).allowed);
}

TEST_CASE("开箱: 配置关掉后店主也打不开，提示指向服主开关") {
    auto const decision = decide(ShopBlockTouch::OpenChest, ShopActor::Owner, /*allowOwnerOpenChest=*/false);
    CHECK_FALSE(decision.allowed);
    // 提示要让服主一眼看出是哪个开关，而不是把玩家引到「找店主」那条死路
    CHECK(plainText(decision.message).find("allowOwnerOpenChest") != std::string::npos);
    CHECK(plainText(decision.message).find("OP") != std::string::npos);
}

TEST_CASE("开箱: OP 不受 allowOwnerOpenChest 影响") {
    CHECK(decide(ShopBlockTouch::OpenChest, ShopActor::Operator, /*allowOwnerOpenChest=*/false).allowed);
}

TEST_CASE("开箱: 没记下店主的牌只给 OP (fail-closed)") {
    auto const decision = decide(ShopBlockTouch::OpenChest, ShopActor::NoRecordedOwner);
    CHECK_FALSE(decision.allowed);
    CHECK(plainText(decision.message).find("只有 OP") != std::string::npos);
    // 那块牌压根没有店主，别写「只有店主」
    CHECK(plainText(decision.message).find("只有店主") == std::string::npos);
}

TEST_CASE("拆箱: 只有店主与 OP 拆得掉") {
    CHECK_FALSE(decide(ShopBlockTouch::BreakChest, ShopActor::Other).allowed);
    CHECK(decide(ShopBlockTouch::BreakChest, ShopActor::Owner).allowed);
    CHECK(decide(ShopBlockTouch::BreakChest, ShopActor::Operator).allowed);
}

TEST_CASE("拆牌: 只有店主与 OP 拆得掉") {
    CHECK_FALSE(decide(ShopBlockTouch::BreakSign, ShopActor::Other).allowed);
    CHECK(decide(ShopBlockTouch::BreakSign, ShopActor::Owner).allowed);
    CHECK(decide(ShopBlockTouch::BreakSign, ShopActor::Operator).allowed);
}

TEST_CASE("拆: 没记下店主的箱子与牌都只给 OP，且各自说对是箱还是牌") {
    auto const chest = decide(ShopBlockTouch::BreakChest, ShopActor::NoRecordedOwner);
    CHECK_FALSE(chest.allowed);
    CHECK(plainText(chest.message).find("只有 OP") != std::string::npos);
    CHECK(plainText(chest.message).find("这个箱子") != std::string::npos);

    auto const sign = decide(ShopBlockTouch::BreakSign, ShopActor::NoRecordedOwner);
    CHECK_FALSE(sign.allowed);
    CHECK(plainText(sign.message).find("只有 OP") != std::string::npos);
    CHECK(plainText(sign.message).find("这块牌") != std::string::npos);
}

TEST_CASE("提示: 无主牌的拆牌提示不把「这块商店牌」说两遍") {
    auto const decision = decide(ShopBlockTouch::BreakSign, ShopActor::NoRecordedOwner);
    CHECK_FALSE(decision.allowed);
    CHECK(plainText(decision.message).find("这块牌") != std::string::npos);
    CHECK(plainText(decision.message).find("拆掉这块商店牌") == std::string::npos);
}

TEST_CASE("提示: 开箱、拆箱、拆牌各说各的，不会把牌子说成箱子") {
    std::vector<std::string> const messages{
        plainText(decide(ShopBlockTouch::OpenChest, ShopActor::Other).message),
        plainText(decide(ShopBlockTouch::BreakChest, ShopActor::Other).message),
        plainText(decide(ShopBlockTouch::BreakSign, ShopActor::Other).message),
    };
    for (auto const& message : messages) {
        CHECK_FALSE(message.empty());
    }
    CHECK(messages[0] != messages[1]);
    CHECK(messages[1] != messages[2]);
    CHECK(messages[2].find("商店牌") != std::string::npos);
    CHECK(messages[1].find("商店箱") != std::string::npos);
}

TEST_CASE("提示: 管理员商店只提 OP，不把记在牌上的创建者当成店主") {
    auto const decision =
        decide(ShopBlockTouch::BreakChest, ShopActor::Other, /*allowOwnerOpenChest=*/true, "Alex", /*adminShop=*/true);
    CHECK_FALSE(decision.allowed);
    CHECK(plainText(decision.message).find("OP") != std::string::npos);
    CHECK(plainText(decision.message).find("Alex") == std::string::npos);
    CHECK(plainText(decision.message).find("店主") == std::string::npos);
}

TEST_CASE("提示: 没有店主名时不拼出「是 的商店箱」这种半句话") {
    auto const decision =
        decide(ShopBlockTouch::OpenChest, ShopActor::Other, /*allowOwnerOpenChest=*/true, /*ownerName=*/"");
    CHECK_FALSE(decision.allowed);
    CHECK(plainText(decision.message).find("是 ") == std::string::npos);
    CHECK(plainText(decision.message).find("只有店主") != std::string::npos);
}

TEST_CASE("放行时不带提示: 消息只用来解释拒绝") {
    CHECK(text::empty(decide(ShopBlockTouch::OpenChest, ShopActor::Owner).message));
    CHECK(text::empty(decide(ShopBlockTouch::BreakSign, ShopActor::Operator).message));
}

TEST_CASE("拆依附方块: 放行条件与拆牌相同 (店主与 OP)") {
    CHECK_FALSE(decide(ShopBlockTouch::BreakSignSupport, ShopActor::Other).allowed);
    CHECK_FALSE(decide(ShopBlockTouch::BreakSignSupport, ShopActor::NoRecordedOwner).allowed);
    CHECK(decide(ShopBlockTouch::BreakSignSupport, ShopActor::Owner).allowed);
    CHECK(decide(ShopBlockTouch::BreakSignSupport, ShopActor::Operator).allowed);
    CHECK(text::empty(decide(ShopBlockTouch::BreakSignSupport, ShopActor::Owner).message));
}

TEST_CASE("拆依附方块: 提示点出后果 (牌子会掉、商店会没)，不让人以为只是拆普通方块") {
    auto const decision = decide(ShopBlockTouch::BreakSignSupport, ShopActor::Other);
    CHECK_FALSE(decision.allowed);
    CHECK(plainText(decision.message).find("Alex") != std::string::npos);
    CHECK(plainText(decision.message).find("商店牌") != std::string::npos);
    CHECK(plainText(decision.message).find("商店也就没了") != std::string::npos);
    // 拆的是方块，不是把牌本身说成“商店箱”
    CHECK(plainText(decision.message).find("商店箱") == std::string::npos);
}

TEST_CASE("拆依附方块: 管理员商店只提 OP，且不报店主名") {
    auto const decision = decide(
        ShopBlockTouch::BreakSignSupport,
        ShopActor::Other,
        /*allowOwnerOpenChest=*/true,
        "Alex",
        /*adminShop=*/true
    );
    CHECK_FALSE(decision.allowed);
    CHECK(plainText(decision.message).find("OP") != std::string::npos);
    CHECK(plainText(decision.message).find("Alex") == std::string::npos);
    CHECK(plainText(decision.message).find("管理员商店") != std::string::npos);
}

TEST_CASE("拆依附方块: 无主牌只提 OP，不拼出「是 的」这种半句话") {
    auto const decision =
        decide(ShopBlockTouch::BreakSignSupport, ShopActor::NoRecordedOwner, /*allowOwnerOpenChest=*/true, "");
    CHECK_FALSE(decision.allowed);
    CHECK(plainText(decision.message).find("没有记录店主") != std::string::npos);
    CHECK(plainText(decision.message).find("只有 OP") != std::string::npos);
    CHECK(plainText(decision.message).find("是 ") == std::string::npos);
}

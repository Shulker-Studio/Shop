#include "shop/ItemBanList.h"

#include <doctest/doctest.h>
#include <string>
#include <vector>

namespace {

using namespace ss::shop;

/// 匹配的物品 ID 一律写全（带 `minecraft:` 命名空间）——这正是 `ItemStackBase::getTypeName()` 的形状。
constexpr char const* kBedrock = "minecraft:bedrock";

} // namespace

TEST_CASE("禁售表: 空表什么都不禁") {
    ItemBanList const empty;
    CHECK(empty.empty());
    CHECK(empty.size() == 0);
    CHECK_FALSE(empty.banned(kBedrock));
}

TEST_CASE("禁售表: 一条规则只禁它写得下的那件 ID") {
    ItemBanList const list{std::vector<std::string>{kBedrock}};
    CHECK(list.size() == 1);
    CHECK(list.banned(kBedrock));
    CHECK_FALSE(list.banned("minecraft:dirt"));
    CHECK_FALSE(list.banned(""));
}

TEST_CASE("禁售表: 整串匹配 —— 只写一半的名字禁不到任何东西") {
    // 这是刻意选的语义：按「查找」会顺手禁掉 redstone / cobblestone，
    // 而整串匹配下 `stone` 什么也匹配不到，错在安全的那一侧。
    ItemBanList const list{std::vector<std::string>{"stone"}};
    CHECK_FALSE(list.banned("minecraft:stone"));
    CHECK_FALSE(list.banned("minecraft:redstone"));
    // 整串写成完整 ID 才生效
    CHECK(ItemBanList{std::vector<std::string>{"minecraft:stone"}}.banned("minecraft:stone"));
}

TEST_CASE("禁售表: 正则可以一次禁一批（潜影盒的真实 ID 都以 _shulker_box 结尾）") {
    // 真机实测（`/shop debug on` 的 `product_nbt` 行）：无色的潜影盒叫
    // `minecraft:undyed_shulker_box`，所以所有潜影盒**都**以 `_shulker_box` 结尾，
    // 一条 `.*_shulker_box` 就能全禁，不需要 `(.*_)?` 那种绕法。
    ItemBanList const list{std::vector<std::string>{"minecraft:.*_shulker_box"}};
    CHECK(list.banned("minecraft:undyed_shulker_box"));
    CHECK(list.banned("minecraft:red_shulker_box"));
    CHECK(list.banned("minecraft:blue_shulker_box"));
    CHECK_FALSE(list.banned("minecraft:diamond"));
    // 少写一个下划线就一件也禁不到（整串匹配）：这正是配置加载时会 WARN 的那类写法。
    CHECK_FALSE(ItemBanList{std::vector<std::string>{"minecraft:shulker_box"}}.banned("minecraft:undyed_shulker_box"));

    // `.*` 放在前面同样成立（常见的“所有命令方块”写法）
    ItemBanList const commands{std::vector<std::string>{".*command_block"}};
    CHECK(commands.banned("minecraft:command_block"));
    CHECK(commands.banned("minecraft:repeating_command_block"));
    CHECK_FALSE(commands.banned("minecraft:crafting_table"));
}

TEST_CASE("禁售表: 大小写不敏感") {
    ItemBanList const list{std::vector<std::string>{"Minecraft:Bedrock"}};
    CHECK(list.banned(kBedrock));
}

TEST_CASE("禁售表: 空白规则跳过，不算错") {
    ItemBanList const list{std::vector<std::string>{"", "   ", "\t"}};
    CHECK(list.empty());
    CHECK(list.invalidPatterns().empty());

    // 规则两边的空白会被裁掉，别指望服主记得不写空格
    ItemBanList const padded{std::vector<std::string>{"  " + std::string{kBedrock} + "  "}};
    CHECK(padded.size() == 1);
    CHECK(padded.banned(kBedrock));
}

TEST_CASE("禁售表: 写不成正则的规则会被记下来（配置层据此拒绝加载）") {
    ItemBanList const list{std::vector<std::string>{kBedrock, "minecraft:[", "minecraft:dirt"}};
    REQUIRE(list.invalidPatterns().size() == 1);
    CHECK(list.invalidPatterns()[0] == "minecraft:[");
    // 好的那几条照样生效，坏的那条不会变成“什么都匹配”
    CHECK(list.size() == 2);
    CHECK(list.banned(kBedrock));
    CHECK(list.banned("minecraft:dirt"));
}

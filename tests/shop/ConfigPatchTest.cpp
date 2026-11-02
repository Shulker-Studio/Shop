#include "shop/ConfigPatch.h"

#include <doctest/doctest.h>
#include <string>
#include <vector>

namespace {

using namespace ss::shop;

/// 只想在测试里用的小"默认配置"：字段少但结构够真（有嵌套对象、数组、标量、version）。
nlohmann::ordered_json sampleDefaults() {
    return nlohmann::ordered_json::parse(R"({
        "version": 1,
        "adminShopName": "Admin Shop",
        "shopContainers": ["minecraft:chest", "minecraft:trapped_chest"],
        "maxShopAmount": 3456,
        "economy": {
            "backend": "legacy_money",
            "scoreboardObjective": "money",
            "createObjectiveIfMissing": true
        },
        "transactionLog": {
            "enabled": true,
            "compressAfterDays": 7
        }
    })");
}

std::string joined(std::vector<std::string> const& keys) {
    std::string out;
    for (auto const& key : keys) {
        if (!out.empty()) {
            out += ",";
        }
        out += key;
    }
    return out;
}

} // namespace

TEST_CASE("配置补齐：老文件缺一整个块，按默认值补上（不让模组拒绝加载）") {
    // 这就是真机上踩到的那个：`transactionLog` 是这一版新加的块，老 config.json 里没有。
    auto const user   = nlohmann::ordered_json::parse(R"({
        "version": 1,
        "adminShopName": "我的店",
        "shopContainers": ["minecraft:chest"],
        "maxShopAmount": 64,
        "economy": {"backend": "scoreboard", "scoreboardObjective": "money", "createObjectiveIfMissing": true}
    })");
    auto const merged = mergeConfigDefaults(sampleDefaults(), user);

    CHECK(merged.contains("transactionLog"));
    CHECK(merged["transactionLog"]["enabled"] == true);
    CHECK(merged["transactionLog"]["compressAfterDays"] == 7);
    // 用户写了的照旧
    CHECK(merged["adminShopName"] == "我的店");
    CHECK(merged["maxShopAmount"] == 64);
    CHECK(merged["economy"]["backend"] == "scoreboard");
    // 缺的那块要能报出来（日志里告诉服主补了什么）
    CHECK(joined(missingConfigKeys(sampleDefaults(), user)) == "transactionLog");
}

TEST_CASE("配置补齐：缺单个嵌套键也补，只报那一个") {
    auto const user   = nlohmann::ordered_json::parse(R"({
        "version": 1,
        "adminShopName": "Admin Shop",
        "shopContainers": ["minecraft:chest"],
        "maxShopAmount": 3456,
        "economy": {"backend": "legacy_money", "scoreboardObjective": "money"},
        "transactionLog": {"enabled": false, "compressAfterDays": 0}
    })");
    auto const merged = mergeConfigDefaults(sampleDefaults(), user);

    CHECK(merged["economy"]["createObjectiveIfMissing"] == true);
    CHECK(merged["transactionLog"]["enabled"] == false);       // 用户值优先
    CHECK(merged["transactionLog"]["compressAfterDays"] == 0); // `0` 是合法值，不能被默认的 7 顶掉
    CHECK(joined(missingConfigKeys(sampleDefaults(), user)) == "economy.createObjectiveIfMissing");
}

TEST_CASE("配置补齐：用户的 null 当成没写，不是「删掉这个键」") {
    // nlohmann 的 merge_patch 按 RFC 7386 把 null 当删除。若照搬，`"transactionLog": null`
    // 会把整块删掉，然后在反序列化时又变成「缺字段」——同一个 bug 换个入口再来一遍。
    auto const user   = nlohmann::ordered_json::parse(R"({
        "version": 1,
        "transactionLog": {"enabled": null, "compressAfterDays": null},
        "economy": null
    })");
    auto const merged = mergeConfigDefaults(sampleDefaults(), user);

    CHECK(merged["transactionLog"]["enabled"] == true);
    CHECK(merged["transactionLog"]["compressAfterDays"] == 7);
    CHECK(merged["economy"]["backend"] == "legacy_money");
}

TEST_CASE("配置补齐：version 由代码决定，不从文件继承") {
    // 文件里写什么都不该让内存里的版本号漂移：它决定的是「这份结构是哪一版」。
    auto const user   = nlohmann::ordered_json::parse(R"({"version": 42, "maxShopAmount": 8})");
    auto const merged = mergeConfigDefaults(sampleDefaults(), user);
    CHECK(merged["version"] == 1);
    CHECK(merged["maxShopAmount"] == 8);
}

TEST_CASE("配置补齐：数组整体替换，不做逐项合并") {
    auto const user   = nlohmann::ordered_json::parse(R"({"shopContainers": ["minecraft:barrel"]})");
    auto const merged = mergeConfigDefaults(sampleDefaults(), user);
    CHECK(merged["shopContainers"].size() == 1);
    CHECK(merged["shopContainers"][0] == "minecraft:barrel");
}

TEST_CASE("配置补齐：拼错的键要报出来（它静默不生效，比缺键更难查）") {
    auto const user = nlohmann::ordered_json::parse(R"({
        "version": 1,
        "transactionlog": {"enabled": false},
        "economy": {"backend": "legacy_money", "scoreboardObjectiv": "money"}
    })");
    CHECK(joined(unknownConfigKeys(sampleDefaults(), user)) == "transactionlog,economy.scoreboardObjectiv");
    // 未知键留在合并结果里不影响反序列化（只取认识的字段），但日志里必须提过
    auto const merged = mergeConfigDefaults(sampleDefaults(), user);
    CHECK(merged["transactionLog"]["enabled"] == true); // 默认值，不是拼错那个
}

TEST_CASE("配置补齐：纯函数，不改入参") {
    auto const defaults = sampleDefaults();
    auto const user     = nlohmann::ordered_json::parse(R"({"version": 1, "transactionLog": null})");
    auto const before   = user;

    auto const merged = mergeConfigDefaults(defaults, user);
    (void)missingConfigKeys(defaults, user);
    (void)unknownConfigKeys(defaults, user);

    CHECK(user == before);
    CHECK(defaults == sampleDefaults());
    CHECK(merged != before);
}

TEST_CASE("配置补齐：用户文件不是对象时按默认值跑，不炸") {
    // `[]` / `"abc"` / `null` 这类坏形状由调用方在前面拦，但纯函数自己也不能抛。
    for (auto const& text : {std::string{"[]"}, std::string{"null"}, std::string{"\"abc\""}}) {
        auto const user   = nlohmann::ordered_json::parse(text);
        auto const merged = mergeConfigDefaults(sampleDefaults(), user);
        CHECK(merged == sampleDefaults());
    }
}

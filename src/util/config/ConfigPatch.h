#pragma once

/// 配置文件的「默认值补齐」规则（纯逻辑，不认识 `ShopConfig`，只操作 JSON）。
///
/// **为什么需要它**：LeviLamina 的 `ll::config::loadConfig` 只在**文件缺 `version`、或
/// `version` 与代码里的不同**时才用默认值补齐（`defaultConfigUpdater` 做一次 `merge_patch`），
/// 其余情况直接严格反序列化——少任何一个键就抛 `missing required field`，整个模组拒绝加载。
///
/// 于是「给配置加一个字段」这件事变成了「记得同时 bump version」，忘了就是全服起不来；
/// 而服主手写配置漏掉一个块也会同归于尽。这里把补齐改成**无条件**的：默认值打底、用户值覆盖，
/// 所以加字段、删字段、手写漏键都不会让加载失败，缺的键一律按默认值跑（并回写一份完整文件）。

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ss::shop {

/// 用默认值补齐用户配置：`defaults` 打底，`user` 里同名的键覆盖它（对象递归合并、数组整体替换）。
///
/// `version` **不由文件决定**，取 `defaults` 里的值：它描述的是「这份结构是哪一版」，
/// 让文件里的数字覆盖它会得到一个谁都不认识的版本号。
/// `user` 里的 `null` 当成「没写」而不是「删掉这个键」（否则等价于又制造了一个缺字段）。
/// `user` 不是对象时直接返回 `defaults`。
nlohmann::ordered_json mergeConfigDefaults(nlohmann::ordered_json const& defaults, nlohmann::ordered_json const& user);

/// `user` 里**缺**哪些 `defaults` 有的键（点分路径，如 `transactionLog` /
/// `economy.createObjectiveIfMissing`）。用来在日志里说清「补了什么」，也便于把它当作契约测试。
std::vector<std::string> missingConfigKeys(nlohmann::ordered_json const& defaults, nlohmann::ordered_json const& user);

/// `user` 里哪些键 `defaults` 不认识（点分路径）。
///
/// 拼错的键（`transactionlog`、`scoreboardObjectiv`）不会报错、也不会生效——服主只会看到
/// 「我明明关了流水怎么还在写」。所以宁可每次启动都提醒一句。
std::vector<std::string> unknownConfigKeys(nlohmann::ordered_json const& defaults, nlohmann::ordered_json const& user);

} // namespace ss::shop

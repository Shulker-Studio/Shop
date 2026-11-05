#include "util/config/ConfigPatch.h"

#include <string_view>

namespace ss::shop {
namespace {

/// 递归去掉 `null` 成员。
///
/// `json::merge_patch` 按 RFC 7386 把 `null` 当作「删除这个键」，而我们的语义是「这个键没写」。
/// 两者在 `"transactionLog": null` 上分道扬镳：前者会删掉整块，然后反序列化时又变成缺字段——
/// 同一个 bug 换了个入口。
void pruneNulls(nlohmann::ordered_json& value) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end();) {
            if (it.value().is_null()) {
                it = value.erase(it);
            } else {
                pruneNulls(it.value());
                ++it;
            }
        }
    } else if (value.is_array()) {
        for (auto& element : value) {
            pruneNulls(element);
        }
    }
}

/// 走一遍 `user`，把 `defaults` 不认识的路径收进 `out`。
///
/// 只在两边都是对象时往下走：`defaults` 给的是数组或标量时，`user` 写了什么形状都由它自己负责
/// （形状错会在反序列化时被 `ll::reflection` 顶回来，那里的报错更准）。
void collectUnknown(
    nlohmann::ordered_json const& defaults,
    nlohmann::ordered_json const& user,
    std::string_view              prefix,
    std::vector<std::string>&     out
) {
    if (!user.is_object() || !defaults.is_object()) {
        return;
    }
    for (auto it = user.begin(); it != user.end(); ++it) {
        auto const        known = defaults.find(it.key());
        std::string const path  = std::string{prefix} + it.key();
        if (known == defaults.end()) {
            out.push_back(path);
            continue;
        }
        collectUnknown(*known, it.value(), path + ".", out);
    }
}

/// `missing` 的镜像：`defaults` 有、`user` 没有的路径。
void collectMissing(
    nlohmann::ordered_json const& defaults,
    nlohmann::ordered_json const& user,
    std::string_view              prefix,
    std::vector<std::string>&     out
) {
    if (!defaults.is_object()) {
        return;
    }
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        std::string const path = std::string{prefix} + it.key();
        if (!user.is_object() || !user.contains(it.key())) {
            out.push_back(path);
            continue;
        }
        collectMissing(it.value(), user.at(it.key()), path + ".", out);
    }
}

} // namespace

nlohmann::ordered_json mergeConfigDefaults(nlohmann::ordered_json const& defaults, nlohmann::ordered_json const& user) {
    auto merged = defaults;
    if (!user.is_object()) {
        return merged;
    }
    auto patch = user;
    patch.erase("version"); // 版本号由代码决定，见头文件
    pruneNulls(patch);
    merged.merge_patch(patch);
    return merged;
}

std::vector<std::string> missingConfigKeys(nlohmann::ordered_json const& defaults, nlohmann::ordered_json const& user) {
    std::vector<std::string> out;
    if (!user.is_object()) {
        return out; // 形状整个不对由调用方处理，这里不报「缺了所有键」
    }
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (it.key() == "version") {
            continue; // 缺 version 不是「缺配置」，它由代码补齐
        }
        if (!user.contains(it.key())) {
            out.push_back(it.key());
            continue;
        }
        collectMissing(it.value(), user.at(it.key()), std::string{it.key()} + ".", out);
    }
    return out;
}

std::vector<std::string> unknownConfigKeys(nlohmann::ordered_json const& defaults, nlohmann::ordered_json const& user) {
    std::vector<std::string> out;
    collectUnknown(defaults, user, {}, out);
    return out;
}

} // namespace ss::shop

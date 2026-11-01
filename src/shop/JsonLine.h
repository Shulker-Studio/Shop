#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace ss::shop {

/// 一条 NDJSON 行：键序由 `nlohmann::ordered_json` 决定，末尾一个 `\n`，中文原样写。
///
/// 两个流水文件（业务流水 `TradeRecord` 与销毁留档 `DiscardRecord`）共用它：
/// 「一行一个对象」的转义规则与结尾只能有一份实现。两个文件各写一份转义，早晚会有一个
/// 在某个字符上分叉，然后 `jq` 在那一行上报错而看不出是谁写的。
///
/// 不用 `indent`：一行就是一行，缩进会把它撕成多行，那就不是 NDJSON 了。
/// 非法 UTF-8 换成 U+FFFD 而不是抛异常——本工程 `set_exceptions("none")`，
/// 一条坏名字不值得让整个进程 abort。
std::string jsonLine(nlohmann::ordered_json const& row);

} // namespace ss::shop

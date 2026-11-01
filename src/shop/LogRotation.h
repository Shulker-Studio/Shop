#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ss::shop {

/// 从一条记录的 `ts` 里取日期：`2026-11-01T13:23:12+08:00` → `2026-11-01`。
///
/// 「文件名里的日期就是 `ts` 的前 10 个字符」这条约定只写在这里：业务流水与销毁留档都靠它
/// 从记录推出文件名，两处各 `substr(0, 10)` 一次的话，哪天改了 `ts` 的格式就会漏掉一处。
/// `ts` 不足 10 个字符时原样返回（短到这种程度的记录本来就该被 `tradeRecordProblem` 拦住）。
std::string_view isoDateOf(std::string_view ts);

/// 从流水文件名里取日期：`transactions-2026-11-01.jsonl` → `2026-11-01`。
///
/// **从文件名解析，不看 mtime**：文件被复制、备份还原、`touch` 过之后 mtime 不代表记录是哪天的，
/// 而文件名是当初写记录那一刻定下的。已经压过的（`.jsonl.gz`）与不像我们文件的
/// （`config.json`、日期非法的）一律返回空——「不认识的不要碰」比「猜一个日期压掉」安全：
/// 压错了就是把一个还该能直接 `grep` 的文件变成需要 `zcat` 才能看的。
std::optional<std::string> logFileDate(std::string_view fileName);

/// 这个日期的文件今天该不该压：`false` = 不该。
///
/// 判据是**早于「今天 − days」**：正好 N 天的文件今天不压，N+1 天才压（「超过 N 天」的字面意思，
/// 也让「把 compressAfterDays 改成 7」这种操作有一个确定的边界）。
///
/// 三种情况一律不压：`days <= 0`（`0` 是「永不压缩」的正式取值，负数由配置层拒绝，这里兜底）；
/// 日期解析不出来；日期**在未**来（服主把系统时间调回去过）。宁可留着一个大文件，
/// 也不要把刚写的记录压掉——压缩省的是磁盘，删错了丢的是账。
bool shouldCompress(std::string_view fileDate, std::string_view today, int days);

/// 压缩后的文件名：`x.jsonl` → `x.jsonl.gz`。
/// 已经是 `.gz` 的原样返回（幂等：重试压缩时不会变成 `.gz.gz`）。
std::string compressedLogFileName(std::string_view fileName);

} // namespace ss::shop

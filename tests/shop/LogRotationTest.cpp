#include "shop/LogRotation.h"

#include <doctest/doctest.h>
#include <optional>
#include <string>

namespace {

using namespace ss::shop;

/// `logFileDate` 的结果与期望日期比较。包一层是因为 doctest 打印 `std::optional` 要额外的头。
bool dateIs(std::string_view fileName, char const* expected) {
    auto const date = logFileDate(fileName);
    return date.has_value() && *date == expected;
}

} // namespace

TEST_CASE("流水轮转: 新布局的文件名 (日期就在开头) 也能解析") {
    // 分目录之后文件名不再带类型前缀，`logFileDate` 必须认「日期就是整个名字」这一种。
    CHECK(dateIs("2026-11-01.jsonl", "2026-11-01"));
    CHECK(dateIs("2024-02-29.jsonl", "2024-02-29"));
    CHECK_FALSE(logFileDate("2026-11-01.jsonl.gz").has_value());
    CHECK_FALSE(logFileDate("2026-11-01.jsonl.part").has_value());
    CHECK_FALSE(logFileDate("notes.jsonl").has_value());
}

TEST_CASE("流水轮转: 旧布局的名字不归压缩管 (只由搬家处理)") {
    // `transactions-2026-11-01.jsonl` / `discarded-…` 是搬家（`relocatedLogFile`）的活。
    // 压缩只认裸日期： 否则服主放进日志目录的 `backup-2026-11-01.jsonl` 会被当成我们的文件
    // 压掉并删原文件。
    CHECK_FALSE(logFileDate("transactions-2026-11-01.jsonl").has_value());
    CHECK_FALSE(logFileDate("discarded-2026-11-01.jsonl").has_value());
    CHECK_FALSE(logFileDate("backup-2026-11-01.jsonl").has_value());

    // 不认识的不要碰： 「猜一个日期把它压掉」比留着一个大文件糟得多
    CHECK_FALSE(logFileDate("config.json").has_value());
    CHECK_FALSE(logFileDate("2026-13-01.jsonl").has_value()); // 没有 13 月
    CHECK_FALSE(logFileDate("2026-02-30.jsonl").has_value()); // 2026 不是闰年
    CHECK_FALSE(logFileDate("2026-11-01.jsonl.bak").has_value());
    CHECK_FALSE(logFileDate("2026-11-01").has_value());                  // 没有 .jsonl 后缀
    CHECK_FALSE(logFileDate("transactions2026-11-01.jsonl").has_value()); // 少了分隔用的 `-`
}

TEST_CASE("流水轮转: 正好 N 天不压，N+1 天才压") {
    // today = 2026-11-08、N = 7 → 分界线是 2026-11-01
    CHECK_FALSE(shouldCompress("2026-11-01", "2026-11-08", 7)); // 正好 N 天
    CHECK(shouldCompress("2026-10-31", "2026-11-08", 7));       // N+1 天
    CHECK_FALSE(shouldCompress("2026-11-08", "2026-11-08", 7)); // 今天写的
    CHECK(shouldCompress("2026-10-01", "2026-11-08", 7));       // 更早

    // 跨月、跨年也得对（这就是不用字符串比较日期的原因）
    CHECK_FALSE(shouldCompress("2026-01-01", "2026-01-08", 7));
    CHECK(shouldCompress("2025-12-31", "2026-01-02", 1));
    CHECK_FALSE(shouldCompress("2026-01-01", "2026-01-02", 1));
}

TEST_CASE("流水轮转: days = 0 永不压缩、未来日期不压、解析不出来不碰") {
    CHECK_FALSE(shouldCompress("2020-01-01", "2026-11-08", 0));  // 0 = 永不压缩（正式取值）
    CHECK_FALSE(shouldCompress("2020-01-01", "2026-11-08", -1)); // 配置层拒了负数，这里兜底
    CHECK_FALSE(shouldCompress("2020-01-01", "2026-11-08", -7));
    // 服主把系统时间调回去过： 刚写的记录不能被当成「很旧」
    CHECK_FALSE(shouldCompress("2026-11-20", "2026-11-08", 7));
    // 两头的日期烂掉就什么都不做
    CHECK_FALSE(shouldCompress("", "2026-11-08", 7));
    CHECK_FALSE(shouldCompress("2026-11-01", "", 7));
    CHECK_FALSE(shouldCompress("2026-1-1", "2026-11-08", 7));
}

TEST_CASE("流水轮转: 压缩后的文件名就是加 .gz，且幂等") {
    CHECK(compressedLogFileName("transactions-2026-11-01.jsonl") == "transactions-2026-11-01.jsonl.gz");
    CHECK(compressedLogFileName("discarded-2026-11-01.jsonl") == "discarded-2026-11-01.jsonl.gz");
    // 已经是 .gz 就原样返回： 重试压缩时不会变成 .gz.gz
    CHECK(compressedLogFileName("transactions-2026-11-01.jsonl.gz") == "transactions-2026-11-01.jsonl.gz");
}

TEST_CASE("流水轮转: 文件名里的日期来自 ts 的前 10 个字符") {
    // 「ts 前 10 位就是日期」这条约定只写在 isoDateOf 一处： 业务流水与销毁留档都靠它推文件名。
    CHECK(std::string{isoDateOf("2026-11-01T13:23:12+08:00")} == "2026-11-01");
    CHECK(std::string{isoDateOf("2026-12-31T00:00:00-05:00")} == "2026-12-31");
    // 短到没有日期原样返回（这种记录会被 tradeRecordProblem 拦住，不该走到写文件）
    CHECK(std::string{isoDateOf("2026")} == "2026");
    CHECK(std::string{isoDateOf("")} == "");
}

TEST_CASE("日志布局: 旧位置的文件能算出新位置 (前缀 → 子目录)") {
    // 旧布局把所有文件平铺在 `logs/` 根下： `transactions-2026-11-01.jsonl`。
    // 新布局每个文件进自己的子目录，文件名里不再带前缀。搬家规则必须幂等且不碰别人的文件。
    auto const trade = relocatedLogFile("transactions-2026-11-01.jsonl");
    REQUIRE(trade.has_value());
    CHECK(trade->subdirectory == "transactions");
    CHECK(trade->fileName == "2026-11-01.jsonl");

    auto const discard = relocatedLogFile("discarded-2026-11-01.jsonl.gz");
    REQUIRE(discard.has_value());
    CHECK(discard->subdirectory == "discarded");
    CHECK(discard->fileName == "2026-11-01.jsonl.gz"); // `.gz` 原样保留

    // 不认识的、日期不合法的、后缀不对的： 一律不碰（那个目录里可能有服主自己放的文件）
    CHECK_FALSE(relocatedLogFile("README.md").has_value());
    CHECK_FALSE(relocatedLogFile("transactions-.jsonl").has_value());
    CHECK_FALSE(relocatedLogFile("transactions-2026-13-01.jsonl").has_value()); // 13 月
    CHECK_FALSE(relocatedLogFile("transactions-2026-11-01.txt").has_value());
    CHECK_FALSE(relocatedLogFile("transactions-2026-11-01.jsonl.part").has_value());
    // 新布局的文件名（已经在子目录里）不该被再搬一次
    CHECK_FALSE(relocatedLogFile("2026-11-01.jsonl").has_value());
}

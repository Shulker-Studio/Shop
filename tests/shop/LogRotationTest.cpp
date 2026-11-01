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

TEST_CASE("流水轮转：日期从文件名解析（不看 mtime）") {
    // 文件被复制、备份还原、`touch` 过之后 mtime 不代表记录是哪天的；文件名是写记录时定下的。
    CHECK(dateIs("transactions-2026-11-01.jsonl", "2026-11-01"));
    CHECK(dateIs("discarded-2026-11-01.jsonl", "2026-11-01"));
    CHECK(dateIs("transactions-2024-02-29.jsonl", "2024-02-29")); // 闰年

    // 已经压过的不再压（否则会变成 .gz.gz）
    CHECK_FALSE(logFileDate("transactions-2026-11-01.jsonl.gz").has_value());
    // 不认识的不要碰：「猜一个日期把它压掉」比留着一个大文件糟得多
    CHECK_FALSE(logFileDate("config.json").has_value());
    CHECK_FALSE(logFileDate("transactions-.jsonl").has_value());
    CHECK_FALSE(logFileDate("transactions-2026-13-01.jsonl").has_value()); // 没有 13 月
    CHECK_FALSE(logFileDate("transactions-2026-02-30.jsonl").has_value()); // 2026 不是闰年
    CHECK_FALSE(logFileDate("transactions-2026-11-01.jsonl.bak").has_value());
    CHECK_FALSE(logFileDate("transactions-2026-11-01").has_value());      // 没有 .jsonl 后缀
    CHECK_FALSE(logFileDate("transactions2026-11-01.jsonl").has_value()); // 少了分隔用的 `-`
}

TEST_CASE("流水轮转：正好 N 天不压，N+1 天才压") {
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

TEST_CASE("流水轮转：days = 0 永不压缩、未来日期不压、解析不出来不碰") {
    CHECK_FALSE(shouldCompress("2020-01-01", "2026-11-08", 0));  // 0 = 永不压缩（正式取值）
    CHECK_FALSE(shouldCompress("2020-01-01", "2026-11-08", -1)); // 配置层拒了负数，这里兜底
    CHECK_FALSE(shouldCompress("2020-01-01", "2026-11-08", -7));
    // 服主把系统时间调回去过：刚写的记录不能被当成「很旧」
    CHECK_FALSE(shouldCompress("2026-11-20", "2026-11-08", 7));
    // 两头的日期烂掉就什么都不做
    CHECK_FALSE(shouldCompress("", "2026-11-08", 7));
    CHECK_FALSE(shouldCompress("2026-11-01", "", 7));
    CHECK_FALSE(shouldCompress("2026-1-1", "2026-11-08", 7));
}

TEST_CASE("流水轮转：压缩后的文件名就是加 .gz，且幂等") {
    CHECK(compressedLogFileName("transactions-2026-11-01.jsonl") == "transactions-2026-11-01.jsonl.gz");
    CHECK(compressedLogFileName("discarded-2026-11-01.jsonl") == "discarded-2026-11-01.jsonl.gz");
    // 已经是 .gz 就原样返回：重试压缩时不会变成 .gz.gz
    CHECK(compressedLogFileName("transactions-2026-11-01.jsonl.gz") == "transactions-2026-11-01.jsonl.gz");
}

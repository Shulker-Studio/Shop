# 03: 压缩与配置（永不删除）

**What to build:** 流水文件攒久了不会把磁盘吃满：超过 N 天（默认 7）的文件自动压成 `.jsonl.gz`，**永不删除**；N 与总开关都写在 `config.json` 里。

**Blocked by:** 02

**Status:** ready-for-agent

- [ ] 配置项：`transactionLog.enabled`（默认 `true`）、`transactionLog.compressAfterDays`（默认 `7`，`0` = 不压缩；**负数非法，加载时拒绝**）
- [ ] 判定：从**文件名**解析日期（不读 mtime），日期早于「今天 − N 天」且没有 `.gz` 版本 → 压缩
- [ ] 压缩：`transactions-2026-11-01.jsonl` → `transactions-2026-11-01.jsonl.gz`，**成功之后**删原文件；失败保留原文件 + `warn`，下一轮重试（幂等）
- [ ] 用 `zlib`（`add_requires("zlib")`，`gzopen`/`gzwrite` 流式写，别把整个文件读进内存）；两个文件（业务流水与 `discarded`）同一套规则
- [ ] 触发时机：每次写完流水后顺手检查一遍，**不额外开线程、不做定时任务**
- [ ] `/shop status` 显示这两项配置与当前"是否开启"状态
- [ ] 单测（纯逻辑部分）：`shouldCompress(fileDate, today, days)` 的边界（正好 N 天 / N+1 天 / `days=0` / 未来日期）、压缩后的文件名推导
- [ ] 真机：造一个 8 天前的 `.jsonl` → 重启或交易一次 → 它变成 `.gz`、原文件没了、`zcat` 能读、`duckdb` 能查；把 `compressAfterDays` 改 0 → 不再压缩；`enabled=false` → 两个文件都不再新增
- [ ] README 补三个用法例子（`jq` / `zcat` / `duckdb`）
- [ ] `xmake build Shop` / `xmake test` 全绿，测试数与断言数写进本票

## 备注

- 不删的理由：删数据没有回头路，要清盘得服主自己 `rm`。插件不替人做这个决定。
- 并发：两个文件都只在服务端线程写，天然串行；将来若改异步写，必须换成单写者队列。

## Answer

（待实现）

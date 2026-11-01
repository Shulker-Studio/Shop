# 03: 压缩与配置（永不删除）

**What to build:** 流水文件攒久了不会把磁盘吃满：超过 N 天（默认 7）的文件自动压成 `.jsonl.gz`，**永不删除**；N 与总开关都写在 `config.json` 里。

**Blocked by:** 02

**Status:** resolved

- [x] 配置项：`transactionLog.enabled`（默认 `true`）、`transactionLog.compressAfterDays`（默认 `7`，`0` = 不压缩；**负数非法，加载时拒绝**）
- [x] 判定：从**文件名**解析日期（不读 mtime），日期早于「今天 − N 天」且没有 `.gz` 版本 → 压缩
- [x] 压缩：`transactions-2026-11-01.jsonl` → `transactions-2026-11-01.jsonl.gz`，**成功之后**删原文件；失败保留原文件 + `warn`，下一轮重试（幂等）
- [x] 用 `zlib`（`add_requires("zlib")`，`gzopen`/`gzwrite` 流式写，别把整个文件读进内存）；两个文件（业务流水与 `discarded`）同一套规则
- [x] 触发时机：每次写完流水后顺手检查一遍，**不额外开线程、不做定时任务**
- [x] `/shop status` 显示这两项配置与当前"是否开启"状态
- [x] 单测（纯逻辑部分）：`shouldCompress(fileDate, today, days)` 的边界（正好 N 天 / N+1 天 / `days=0` / 未来日期）、压缩后的文件名推导
- [ ] 真机（**未验**）：造一个 8 天前的 `.jsonl` → 重启或交易一次 → 它变成 `.gz`、原文件没了、`zcat` 能读、`duckdb` 能查；把 `compressAfterDays` 改 0 → 不再压缩；`enabled=false` → 两个文件都不再新增
- [x] README 补三个用法例子（`jq` / `zcat` / `duckdb`）
- [x] `xmake build Shop` / `xmake test` 全绿，测试数与断言数写进本票

## 备注

- 不删的理由：删数据没有回头路，要清盘得服主自己 `rm`。插件不替人做这个决定。
- 并发：两个文件都只在服务端线程写，天然串行；将来若改异步写，必须换成单写者队列。

## Answer

### 落地了什么

**纯逻辑（`src/shop`，无 `mc/` 头）**

- 新增 `LogRotation.h/.cpp`：
  - `logFileDate(fileName)`：从**文件名**取日期（`transactions-2026-11-01.jsonl` → `2026-11-01`），
    不看 mtime——文件被复制、备份还原、`touch` 过之后 mtime 不代表记录是哪天的。
    已压过的（`.jsonl.gz`）、不像我们文件的（`config.json`）、日期假日期（`2026-13-01`、
    `2026-02-30`）、少了分隔符的（`transactions2026-11-01.jsonl`）一律返回空：**不认识的不要碰**。
  - `shouldCompress(fileDate, today, days)`：判据是「**早于** 今天 − N 天」——正好 N 天不压、
    N+1 天才压。`days <= 0` 永不压（`0` 是正式取值）、日期解析不出来不压、日期在未来不压
    （服主把系统时间调回去过）。跨月跨年用 `std::chrono::year_month_day` 折算成天数再比，
    不用字符串比较，也不自己算闰年。
  - `compressedLogFileName(fileName)`：`x.jsonl` → `x.jsonl.gz`；已经是 `.gz` 的原样返回（幂等，
    重试时不会变成 `.gz.gz`）。

**BDS 适配（`src/mod`）**

- `Config`：新增 `TransactionLogConfig { enabled = true; compressAfterDays = 7; }`，挂在
  `ShopConfig::transactionLog` 上（键名就是 `transactionLog.enabled` / `transactionLog.compressAfterDays`）。
  `validate` 拒负数（`0` 合法 = 永不压缩）：负数会让「今天 − N 天」跑到未来，把每一份都压掉；
  写错了要让服主知道，不静默按 0 处理。老 `config.json` 里没有这个块时按默认值（开、7 天）运行。
- `LogFile`（**所有流水文件的唯一出口**）现在一次判三件事，所以「只关了一个文件」这种状态
  不可能出现：
  1. **总开关**：`config == nullptr || !transactionLog.enabled` 直接返回——业务流水与 `discarded`
     都从这一个函数出去，两个一起停；
  2. **落盘**：建目录 → 开 → 写 → `flush` → 关（不缓存句柄，跨天换文件名）；
  3. **轮转**：写成功之后 `compressOldLogs(dir, 今天, compressAfterDays)`。「今天」直接取刚写的
     那个文件名里的日期，不再读一次时钟。**同一天只扫一次目录**：一天写几千笔流水时，能让文件
     「变旧」的只有日期变化，按天检查足够，也不至于每笔交易都扫一遍目录；不额外开线程、不做定时任务。
- 压缩用 zlib 的 `gzopen`/`gzwrite` **流式**读写（64 KiB 块，不把整天文件读进内存）。顺序是
  「写 `.part` → `gzclose` 成功 → 改名为 `.jsonl.gz` → 删原文件」，每一步都为同一件事：
  **成品名只会在内容完整时出现**。直接写成品名的话，中途崩服会留一个截断的 `.gz`，下一轮
  看到「`.gz` 已存在」就当它压好了再删原文件——那是拿数据换磁盘。失败时删掉 `.part`、保留原文件、
  `warn`（同进程一次），下一轮重试。成品已在但原文件还在时，下一轮只补「删原文件」这一步。
- Windows 上用 `gzopen_w`（`path::c_str()` 就是宽字符串）：`gzopen` 收窄字符串走系统代码页，
  服主目录名里有中文时会打不开。
- `/shop status` 多一行：`交易流水：开/关 compressAfterDays=N（超期压成 .jsonl.gz，永不删除）`+ 目录路径。
  关时写明「业务流水与 discarded 一起停」。
- `xmake.lua`：`add_requires("zlib 1.3.2")` + `add_packages("zlib")`（只有 `Shop` 链接它——
  纯逻辑层只算「该不该压」，`ShopTests` 不需要 zlib）。已确认 levilamina 包**不**传递 zlib。

**文档**：`README` 新增「交易流水（JSONL）」一节——文件位置与格式、三种 `result`、
`discarded` 是什么、永不删除只压缩、`transactionLog` 两项配置，以及票面要求的三个用法例子
（`jq` 查当天成交 / `zcat` 查历史 / `duckdb` 聚合营业额）；「配置项」一节补上这两个键。

### 测试

- `xmake build Shop`：ok（无 warning）
- `xmake build ShopTests`：ok
- `xmake test`：`ShopTests/default passed`
- `./build/windows/x64/release/ShopTests.exe`：**311 cases / 3464 assertions，全绿**
  （上一票 307 / 3436 → **+4 cases / +28 assertions**；本次分支基线 296 / 3382）
- 新增 `tests/shop/LogRotationTest.cpp`（4 个）：文件名解析（含闰年、假日期、`.jsonl.gz`、
  不是我们的文件）、边界（正好 N 天不压 / N+1 天压 / 今天不压 / 跨月跨年）、
  `days = 0` 与负数与未来日期都不压、压缩文件名推导与幂等。
- 文件 IO 与 gzip 按 spec 的 Testing Decisions **不进单测**（依赖文件系统与时间）。
- **既有断言一条未改**。

### 真机项（**未验**）

下面几条没有在 BDS 上跑过。**不许当成已验**：

1. **造一个 8 天前的 `.jsonl` → 交易一次（或重启后第一次写流水）→ 它变成 `.gz`、原文件没了、
   `zcat` 能读、`duckdb` 能查** —— **未验**。
   退路：`compressAfterDays` 默认 7，所以 `transactions-<今天-8>.jsonl` 会被压。
   - 没被压 → 先确认当天第一次写流水之前它就在目录里（触发点是「写成功之后」）；
     再确认文件名严格是 `transactions-YYYY-MM-DD.jsonl`（`logFileDate` 认不出来的不碰）。
   - 出现 `.jsonl.gz` 但原文件还在 → 那是上一轮删原文件失败（会留一条 warn），下一次写流水会补删。
   - `zcat` 读不出来 → 只可能是截断：`.part` 方案保证成品完整，若真出现，删掉那个 `.gz` 重新压缩
     即可（原文件还在）。gzip 流本身的正确性已在本机用一个同构的小程序验过
     （`.jsonl` → `gzopen_w`/`gzwrite`/`gzclose` → Python `gzip` 读回，内容逐字节一致；
     但那是独立程序，不是模组里的这条路径）。
2. **`compressAfterDays` 改 0 → 不再压缩** —— **未验**。退路：`shouldCompress` 在 `days <= 0` 时
   直接返回 false（有单测），且 `compressOldLogs` 连目录都不扫；真机上生效要 `/ll reload Shop`
   或重启（配置只在 `load()` 读一次）。
3. **`transactionLog.enabled = false` → 两个文件都不再新增** —— **未验**。退路：开关只在
   `appendLogLines` 一个入口判，两个文件都走它；若真机发现其中一个还在写，说明它绕过了
   `appendLogLines`（`rg "ofstream" src/mod` 里不该有第二处写流水）。
4. **`/shop status` 显示这两项与开关状态** —— **未验**（只是拼字符串，但字体/换行在游戏里
   可能挤在一起）。

### 顺带闭合的已知缺口

`p1-admin-unlimited-stock` 票 02 记下的两条已知缺口——「按天轮转 + 7 天后 gzip」与
「`transactionLog.enabled = false` 时两个文件一起停」——本票落地。两条都落在
`LogFile::appendLogLines` 这一个入口上，所以 `discarded` 不需要第二份实现。

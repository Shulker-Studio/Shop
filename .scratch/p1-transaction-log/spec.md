# P1 交易流水（JSONL）

Status: ready-for-agent
Type: spec

## Problem Statement

Shop 自己不留任何成交记录。出了争执只能靠 LegacyMoney 的 `mtrans` 表——而那张表：

- 只有钱的流动，**没有物品**：看不出卖了什么、几份；
- 记分板后端根本没有流水；
- 管理员店的买入（销毁）与卖出（凭空记账）在 `mtrans` 里看不出是"哪家店"；
- 失败与冻结（补偿失败）完全没有痕迹。

服主想问"这家店今天卖了多少钻石""这个人一晚上搬空了三家店吗"，现在答不上来。

## Solution

Shop 自己写一份 append-only 的成交流水：

- 位置：`plugins/Shop/logs/transactions-YYYY-MM-DD.jsonl`（按天轮转，服务器本地时间）
- 格式：**NDJSON 规范、`.jsonl` 扩展名**——一行一个 JSON 对象、UTF-8 无 BOM、`\n` 结尾、键序固定（便于人眼读，也便于 `jq`/`duckdb` 直接查）
- 只记**结论**：`ok`（成交）、`fail`（拒绝或未成交）、`frozen`（补偿失败被冻结，同时写 fatal 日志）
- 写失败不能影响交易：日志写不进去就打一条 warn（同一进程只打一次），交易照常完成
- 第二个文件 `discarded-YYYY-MM-DD.jsonl`：管理员店卖出时**被销毁的物品**完整 NBT 留档（见 D5）
- 两个文件都**永不删除**；超过 N 天（默认 7，可配）压成 `.jsonl.gz`（见 D4）

规范名是 NDJSON（ndjson.org），扩展名用 `.jsonl`（生态更通用）；两者是同一格式，改个后缀就能互换。

## User Stories

1. 作为服主，我想查今天某家店卖了多少份，以便看经营情况。
2. 作为服主，我想查某个玩家一晚上的全部交易，以便发现搬空商店的行为。
3. 作为服主，我想知道管理员店今天销毁了多少货、凭空记了多少钱，以便校经济。
4. 作为服主，我想在出争执时看到**失败与被冻结**的记录，以便定位是哪一步没做完。
5. 作为服主，我想日志按天分文件，以便备份与删除。
6. 作为服主，我想日志写失败不影响玩家交易，以便磁盘满时不至于全服商店瘫痪。

## Implementation Decisions

### 记录字段（第一版）

一行一个对象，键按下面顺序写出：

| 键 | 内容 |
| --- | --- |
| `ts` | 服务器本地时间，ISO-8601 带时区偏移，如 `2026-11-01T13:23:12+08:00` |
| `event` | `trade`（本 spec 只有这一种；以后加 `create` / `close` 等） |
| `result` | `ok` / `fail` / `frozen` |
| `reason` | `result != ok` 时的机器可读原因（`no_money` / `no_stock` / `no_space` / `owner_offline` / `shop_closed`…），复用现有拒绝原因枚举，不新造一套文案。**以后每加一个 `TradeRejection` 枚举值都要同步加到这里**（停业会加 `shop_closed`） |
| `dir` | `buy` / `sell`（从**玩家**视角） |
| `shop` | `{dim, x, y, z}` 商店牌坐标 |
| `owner` | `{xuid, name}` 店主；管理员店 `xuid` 为空串、`name` 为 `adminShopName` |
| `admin` | 是否管理员店 |
| `backend` | `legacy_money` / `scoreboard` / `null` |
| `player` | `{xuid, name}` 交易者（当时显示名，方便人看） |
| `item` | `{id, aux, name}`：`id` 用 `ItemStack::getTypeName()`，`aux` 整数，`name` 为当时展示名 |
| `units` / `items` | 份数 / 总件数 |
| `unitPrice` | 单价（牌面上一份的价） |
| `gross` | 主交易金额 |
| `tax` | 税额（管理员店恒为 0） |
| `total` | 玩家实际付出/收到的总额 |

不记录：余额快照（会泄露他人财力、也会让日志随退款重写而失真）、容器坐标（能从牌坐标推导时也不写，保留日后加字段的空间）。

### D1 只在成交成功后写 `ok`

流水必须写在交易**真正完成**之后。钱动了又回滚的中间态一律记 `fail`，不记 `ok`——否则日志会告诉服主“成交了”，而账上没有这笔钱。

**实现入口**：`TradeService.cpp` 里的 `finishTrade(trader, shopSign, direction, result)` ——两个方向都从 `submitTrade` 走到它，且拿得到全部四种 `TradeStatus`（`Done` / `Rejected` / `NeedsConfirm` / `Broken`）与 `result.plan`（份数/件数/金额/税）。

- `Done` → `ok`；`Rejected` → `fail`；`Broken` → `frozen`；
- `NeedsConfirm` **不写**：它不是终态，玩家可能直接关掉确认表单（写了就是噪音，而且会被当成“失败”）；
- 不要在 `buyUnits`/`sellUnits` 里就写——那两处只能看到自己这条路径。

### D2 不做数据库

P1 只要"能查、能对账"。`jq`/`duckdb` 直读 JSONL 就够，还不用处理 SQLite 依赖、并发写、schema 迁移。等真需要"按玩家/店铺聚合的常驻查询"再上 SQLite + `/shop log`（另一条 P1），届时 JSONL 可以原样导入。

### D3 与 LegacyMoney 的 `mtrans` 边界

`mtrans` 是**经济后端的账**，JSONL 是**Shop 的业务流水**。两者刻意分开：

- 换后端（记分板/未来 builtin）时业务流水不变；
- 不读 `mtrans`、不写 `mtrans`，避免与 LegacyMoney 的格式耦合（`pay_tax` 那些坑不外溢）。

### D4 保留与压缩：永不删除，7 天后 gzip

2026-11-01 定：**只压缩，不删除**。

- 配置键：`transactionLog.compressAfterDays`，默认 `7`；写 `0` 表示永不压缩。
- 判定：文件的日期（从文件名解析，不读 mtime）早于"今天 − N 天"且还没有 `.gz` 版本 → 压缩。
- 结果：`transactions-2026-11-01.jsonl` → `transactions-2026-11-01.jsonl.gz`，**原文件删除**（压缩成功之后）。
  压缩失败就留着原文件并 warn，下一轮再试（幂等）。
- 触发时机：每次写新流水之后顺手检查一遍（一天最多几次）；不做定时任务，避免额外线程。
- 压缩用 `zlib`（走 `add_requires("zlib")`，`gzopen`/`gzwrite` 流式写，不把整个文件读进内存）。
  `gzip -dc` / `zcat` / `duckdb` 都能直读 `.jsonl.gz`，查询体验不变。
- 不删的理由：删数据这件事没有回头路，而磁盘便宜。真要清盘，服主自己 `rm` 一次就行——
  那种操作必须是人的决定，不该由插件替他做。

### D5 被销毁的物品单独一个文件

管理员店卖出方向收到的货是**销毁**的（见 `p1-admin-unlimited-stock/spec.md` 的 D3；本文的 D3 讲的是另一件事）。销毁不可逆，所以每一条都留档：

- 文件：`plugins/Shop/logs/discarded-YYYY-MM-DD.jsonl`，与业务流水同一套轮转与压缩规则；
- 一行一条**物品堆**（不是一笔交易一行）：一次卖出 3 组就是 3 行，共用同一个 `ts` 与 `shop`。
  这样 `jq` 里按物品聚合、按附魔筛都不用拆数组；
- 字段（键序固定）：`ts` / `shop{dim,x,y,z}` / `player{xuid,name}` / `item{id,aux,name,nbt}`；
  `nbt` 是 `ItemStack` 的完整 SNBT——`stack.save(SaveContext{SaveContext::SaveUseCase::Clone})->toString()`
  （`ItemStack` 自己没有 `save(CompoundTag&)`），可能很长——所以它**不能**塞进业务流水，
  那会把一行几十字节的记录撑成几十 KB；
- 只在这个方向写：普通店、管理员店**买入**方向都不产生 `discarded` 记录（没有东西被销毁）；
- `transactionLog.enabled = false` 时**两个文件一起停**（业务流水与 `discarded` 都不写），不做"只开一个"的组合；
- 并发：两个文件都只在服务端线程写（交易与表单回调都在那条线程），天然串行，不需要锁。
  将来若引入异步写，要换成"单写者队列"，不许两个线程各开一个 `ofstream` 追加同一个文件。

## Testing Decisions

**缝是序列化函数**（纯逻辑）：记录结构 + 字段组合校验 + 一行 JSON 的生成。文件 IO 不进单测。

- **测什么**：键序固定；`
` 结尾；中文与引号、换行不破坏"一行一对象"；`ok` 不带 `reason`；`fail`/`frozen` 必须带；
  `admin == true` 时 `tax == 0` 且 `owner.xuid` 为空；文件名按 `ts` 生成；`discarded` 记录的字段与 `nbt` 原样透传。
- **不测什么**：不测真实文件写入、轮转、gzip 压缩——这些依赖文件系统与时间，一律真机/manual 验
  （造一个 8 天前的文件看它被压成 `.gz`，`zcat` 能读）。
- **先例**：本项目首次做 JSON 序列化，没有同类先例可抄；用 `nlohmann::ordered_json`（需先 `add_requires`，见票 01 备注）后直接比较 `dump()` 的完整字符串，
  比按字段断言更能锁住键序与转义。

## Out of Scope

- 游戏内查询命令 `/shop log`（需要索引，另开）
- 平均价格/统计聚合（依赖查询层）
- 记录每次点击与表单打开（噪音，且没有业务价值）

## Further Notes

- 与前两份 P1 无硬依赖，可以独立完成；但它读的是 `TradeResult.plan`，所以**放在最后做**能一次拿到全部字段
  （无限库存会给 `plan` 增加"无限"的表示，先做流水再改结构等于重写一遍序列化）。
- 落盘后的实际用法写进 README（`jq` / `zcat` / `duckdb` 三个例子），让服主拿到就能查。
- 不做"流水即审计"的承诺：它记的是**插件以为发生的事**，与后端的 `mtrans` 对账是服主自己的事（见 D3）。

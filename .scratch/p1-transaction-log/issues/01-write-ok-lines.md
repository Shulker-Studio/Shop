# 01: 每笔成交落一行（模型 + 写文件 + 接漏斗）

**What to build:** 买一次东西，`plugins/Shop/logs/transactions-YYYY-MM-DD.jsonl` 里就多一行 `result: "ok"` 的记录，`jq .` 能解析，字段一眼能看出「谁、在哪家店、买了什么、几份、多少钱」。

**Blocked by:** None (can start immediately)

**Status:** resolved

- [x] 纯逻辑 `TradeRecord` 结构 + 序列化：**一行一个 JSON 对象**、键序固定、以换行符结尾、UTF-8 无 BOM、中文不转义；用 `nlohmann::ordered_json` 而不是拼字符串
- [x] 字段按 spec 表格：`ts` / `event` / `result` / `reason` / `dir` / `shop{dim,x,y,z}` / `owner{xuid,name}` / `admin` / `backend` / `player{xuid,name}` / `item{id,aux,name}` / `units` / `items` / `unitPrice` / `gross` / `tax` / `total`
- [x] 组合校验：`ok` 不带 `reason`；`fail`/`frozen` 必须带；`admin == true` 时 `tax == 0` 且 `owner.xuid` 为空串
- [x] 依赖方向：`reason` 复用 `src/shop` 的 `TradeRejection`，**不许**引用 `src/mod` 的 `TradeStatus`；`Done/Rejected/Broken/NeedsConfirm` 到流水三态的映射写在 mod 一侧
- [x] `ts` 由调用方传入（不在纯逻辑里读时钟）；文件名规则是纯函数（给 `ts` 出 `transactions-YYYY-MM-DD.jsonl`）
- [x] 写文件：目录 `plugins/Shop/logs/` 不存在就建；每次 `append` → 写一行 → `flush` → 关；跨天自动换文件（不缓存"当前文件"）
- [x] 写失败（磁盘满/权限）只 `warn`（**同进程一次**），交易照常成功
- [x] 接线点**只有** `finishTrade`（两个方向都经过它，且拿得到 `result.plan`）；本票只处理 `Done → ok`，`Rejected`/`Broken` 留到 02
- [x] 单测：三种记录的快照字符串（锁键序与转义）、含引号与换行的自定义物品名仍是一行、文件名、矛盾组合被拒
- [ ] 真机（**未验**）：买一次 → `jq . transactions/*.jsonl` 看一行 `ok`，金额与件数与牌面对得上；再卖一次看到 `dir: "sell"`
- [x] `xmake build Shop` / `xmake test` 全绿，测试数与断言数写进本票

## 备注

- **`nlohmann_json` 现在不是本项目的依赖**（`levilamina` 包里也不带头文件）。先在 `xmake.lua` 加
  `add_requires("nlohmann_json")` 与 `add_packages("nlohmann_json")`（本机缓存已有 v3.12.0），别假设它现成可用。
- 这是**数据**不是日志：不要写进 LeviLamina 的 logger，也不要依赖它的级别。路径用 `std::filesystem` 拼。
- spec 只要求 P1 记终态：`NeedsConfirm` 不是终态，别写。

## Answer

### 落地了什么

**纯逻辑（`src/shop`，无 `mc/` 头）**

- 新增 `src/shop/TradeRecord.h/.cpp`：`TradeLogResult`（`ok` / `fail` / `frozen` 三态）+
  `TradeRecord`（spec 字段表的全部字段）+ `TradeFacts`（「谁、在哪家店、买了什么、多少钱一份」那一半）。
  两者分开是因为**什么时候知道它们**不同：事实在拿到商店快照时就定了，份数/金额/结论要等交易真的走完。
  份数与金额**不存第二份**，唯一真相是 `TradePlan`，`composeTradeRecord` 从它抄一次。
- `tradeRecordJson`：用 `nlohmann::ordered_json` 装字段，**一行一个对象**、键序按 spec 表格、
  `\n` 结尾、`ensure_ascii = false`（中文原样写，不转 `\u94bb`）。转义、数字格式全交给库，
  不再手拼字符串——`DiscardRecord.cpp` 那份手写转义是上一票的历史，票 02 收敛时一并接管（见下）。
  非法 UTF-8 走 `error_handler_t::replace`：本工程 `set_exceptions("none")`，一个坏名字不值得让进程 abort。
- `composeTradeRecord` 把「`admin == true` ⇒ `owner.xuid` 为空串、`tax = 0`」做成**造不出来的矛盾**：
  管理员店的对手方是系统账户（DESIGN §7.4），一条写着「店主收到钱」的管理员店流水会让人去追一个
  不存在的账户。`ts` 由调用方传入，纯逻辑层不读时钟。
- `transactionLogFileName(date)`：`YYYY-MM-DD.jsonl`，与 `ts` 前 10 位同源，跨天就是另一个文件。
  （**布局后来变了**：文件现在放在 `plugins/Shop/logs/transactions/` 里，文件名因此去掉了
  `transactions-` 前缀——类型由目录区分。规则不变，仍是纯函数；见票 03 的「追加：日志按类型分目录」。）
- `tradeRecordProblem(record)`：落盘前的最后一道闸（`ok` 不带 `reason`、`fail`/`frozen` 必须带、
  管理员店的 `tax` 与 `owner.xuid`、`ts` 不能空）。矛盾记录**不写**——比少一行更误导。
- 依赖方向按票面：`TradeLogResult` 与 mod 的 `TradeStatus` 是两套枚举，
  纯逻辑层不认识 `TradeStatus`；`Done/Rejected/Broken/NeedsConfirm` 到三态的映射与
  「哪种终态值得写」都留在 `finishTrade`。`reason` 复用 `TradeRejection` 的那条依赖是票 02 的事，
  本票 `reason` 只有空串。

**BDS 适配（`src/mod`）**

- 新增 `src/mod/LogFile.h/.cpp`：`logDirectory()`（`getConfigDir()/logs`，不自己写死 `plugins/Shop`）
  与 `appendLogLines()`——每次「建目录 → 开 → 写 → flush → 关」，**不缓存句柄**（跨天换文件名，
  缓存「当前文件」还要多维护两种状态）。失败只 `warn`，并且按失败种类（`create_directories` /
  `open` / `write`）**同一进程只提示一次**：磁盘满了也该让生意成交，但每笔交易刷一条 warn 会把真信息淹掉。
  这一层是**策略**，票 02 的 `discarded` 与票 03 的压缩都复用它，不另写一份。
- 新增 `src/mod/TransactionLog.h/.cpp`：`writeTradeRecord`——先过 `tradeRecordProblem`，再用
  `transactionLogFileName(ts.substr(0, 10))` 定位文件、追加一行。写失败没有任何回滚动作。
- `TradeService`：`TradeResult` 加 `std::optional<TradeRecord> tradeLog`；`buyUnits` / `sellUnits`
  在**成交那一刻**（所有步骤都成功之后、`return` 之前）用 `tradeFactsFor(...)` + `composeTradeRecord(...)`
  造好记录。选这一刻而不是等 `finishTrade` 再读世界：商品模板、店主名、单价都是这一笔的样子，
  交易走完之后箱子、牌子、在线名单都可能已经变了。
- `finishTrade`（唯一终态漏斗）：`if (result.tradeLog) writeTradeRecord(*result.tradeLog);`，
  在发完玩家消息之后、`discarded` 落盘之前。本票只有 `Done` 会填它，所以「只记成交」这条界
  是由**填不填**保证的，票 02 接拒绝/冻结时不用改这一行。
- 管理员店的 `owner.name` 取 `config.adminShopName` 而不是 `info.ownerName`：`readShopOwner` 在
  `PlayerInfo` 里查得到创建者时会返回创建者的名字，而 spec 要求这里写 `adminShopName`。
- `xmake.lua`：顶层 `add_requires("nlohmann_json 3.12.0")`（与 `doctest 2.5.3` 一样显式锁版本），
  `Shop` 与 `ShopTests` 都 `add_packages("nlohmann_json")`——纯逻辑层与测试同源编译，少一处就编不过。
  确认过本机缓存是 `nlohmann_json v3.12.0`（header-only，没有新增链接产物）。

**一处对 spec 字面的选择**（**票 02 复查后改掉了**）：当时把 `reason` 键写成恒存在（`ok` 时为空串），
理由是想让键集合固定。两轴审查指出票面写的是「`ok` 不带 `reason`」，而空串又会让
`jq 'has("reason")'` 分不出成交与失败的行——那正是这个字段存在的理由。现在 `ok` 的行
**不写这个键**；票 01 那条 `ok` 快照断言因此更新（是本分支自己写的断言，不是既有断言，
改动已在票 03 的审查小节点名）。

### 测试

- `xmake build Shop`：ok（无 warning）
- `xmake build ShopTests`：ok
- `xmake test`：`ShopTests/default passed`
- `./build/windows/x64/release/ShopTests.exe`：**304 cases / 3420 assertions，全绿**
  （上一票 `p1-admin-unlimited-stock` 票 02 是 288 / 3356；本次基线 296 / 3382 → **+8 cases / +38 assertions**）
- 新增 `tests/shop/TradeRecordTest.cpp`（8 个）：`ok` 的完整快照字符串（锁键序）、`fail`/`frozen`
  的 `reason` 与「一份没成交数字也是 0」、管理员店强制 `owner.xuid` 空与 `tax = 0`、
  含引号/反斜杠/换行的名字仍是一行（只有一个 `\n` 且在末尾）、中文原样写（不含 `\u94bb`）、
  文件名按日期生成、矛盾组合被拒（6 种）、三态名字。
- **既有断言一条未改**：`git diff` 里 `tests/` 只有新增的这一个文件。

### 真机项（**未验**）

下面这条没有在 BDS 上跑过，只有纯逻辑测试与构建。**不许当成已验**：

1. **买一次 → `jq . transactions-*.jsonl` 看到一行 `ok`，金额与件数与牌面对得上；再卖一次看到 `dir: "sell"`** —— **未验**。
   退路：
   - 文件没生成 → 先看 `plugins/Shop/logs/` 在不在了；不建目录会留一句
     `日志目录建不出来（…）`，打不开会留 `日志文件打不开（…）`，两种都只提示一次（同进程）。
   - 有文件但没这一行 → 说明 `result.tradeLog` 没被填：检查 `finishTrade` 是否走到了（`/shop debug on`
     之后看 `[shop/dbg] trade_result` 的 `outcome` 是不是 `ok`）。
   - 有行但金额不对 → 先看 `units` / `items` 是否与牌面「第 2 行份量」和实际件数一致；
     `unitPrice` 取的是**这一笔**的方向价（买入价 / 卖出价），不是另一侧。
   - `ok` 的行里**没有** `reason` 这个键（票 01 当时写的是空串，两轴审查后按票面「`ok` 不带 reason」
     改成省掉；若真机看到 `ok` 行带 `"reason":""`，说明装的是旧构建）。非 `ok` 的行才带 `reason`，
     且必须是英文短名；若真机上出现中文，说明有人手拼了记录（`tradeRecordProblem` 会拦住并打 `error`）。
2. **管理员店卖出时 `owner.name` 是 `adminShopName`** —— **未验**。退路：本票按配置取值，
   与牌面第 1 行无关；若真机看到的是创建者名字，说明 `config.adminShopName` 与牌面不一致
   （改了配置但没重启），改配置后重启即可。

### 留到后面两票的（明确不做）

- `Rejected → fail`、`Broken → frozen`、`frozen_pair`、免费店仍记一行 `ok`：票 02。
  本票的 `TradeLogResult` / `composeTradeRecord` / `tradeRecordProblem` 已经为它们留好位置。
  （实际落地时票 02 把 `TradeResult` 从「造好的 `TradeRecord`」改成了 `logFacts` + `logReason`、
  由 `finishTrade` 统一组装：原因短名与「三态映射」两件事都得在漏斗那里收口，
  「造好的记录」里没有地方放原因。见票 02 的 Answer。）
- `TradeRejection → 机器短名`（`no_money` / `no_stock` / …）：票 02，放纯逻辑层。
- `discarded` 与业务流水**共用一套**「目录/追加/warn 一次」策略：本票把这条策略落在
  `src/mod/LogFile`，但**没有**改 `DiscardLog`（票面不许在本票动它）。票 02 把
  `writeDiscardRecords` 接到 `appendLogLines` 上即可，同时把 `DiscardRecord.cpp` 的手写 JSON 转义
  收敛到 `nlohmann::ordered_json`——那是票 02 的「收敛」，现在改会同时碰两条线。
- 压缩与 `transactionLog.*` 配置、`transactionLog.enabled = false` 两个文件一起停：票 03。

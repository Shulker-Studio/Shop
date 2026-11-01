# 02: `/shop debug on|off`：把“拦没拦、为什么”打在明处

**What to build:** 一个运行时开关。打开后在**结果点**各打一行 `[shop/dbg]` 事件（只 5 类，见 spec），关掉后彻底安静。用来把 D2（牌面防护 20 条）、D4（保护 22 条）、B 组（交易 22 条）从"看提示猜"变成"照着日志对"。

**Blocked by:** 01

**Status:** resolved

- [ ] `/shop debug on|off`（权限 `GameDirectors`）。**不持久化**：重启归零，避免误开后忘记关
- [ ] 只这 5 类事件，每类**只在结果点**打一行（决策已定之后）：
  - `sign_decision`：`plan=TakeOver|PassThrough|Block|NotAShopSign`、玩家、坐标、手持物、潜行、是否上蜡
  - `protection_block`：类别（开箱/拆箱/拆牌/拆依附方块）、主体、坐标、原因
  - `trade_result`：方向、份数/件数/金额、结果（`ok`/`fail`/`confirm`/`frozen`）、原因（直接用 `TradeResult` 与 `TradeRejection`，不新造原因码）
  - `content_block`：漏斗拉/推、投掷器、铜傀儡 被拦的那次
  - `economy_resolution`：配置值 → 实际值、跳过原因
- [ ] **不做**：按 tick 记录、逐步插 `moveMoney`/`undoAndRefund` 的中间日志（补偿路径会产生误导性中间记录）、打印完整 NBT、事件流全量
- [ ] 关闭时一行都不打；用 `bool` 在调用点短路，别在热路径上先拼字符串再判断
- [ ] 固定前缀 `[shop/dbg]`，字段顺序固定，每行一个事件——目的是能 `rg trade_result` 直接对上测试单条目
- [ ] 与 01 联动：`/shop status` 显示当前是否开着
- [ ] 真机验一次：开着 debug，非店主用红染料点牌，日志应同时出现 `sign_decision plan=Block` 与一条 `protection_block`，且没有多余重复行
- [ ] `xmake build Shop` 通过

## 备注

- 给测试者的话要写进票：**调试开着时一次只测一条**，否则控制台会刷满。
- 不把 debug 做成配置项（配置要重启才生效，而调试的价值就是"现在就想看"）。

## Answer

（待实现）

## Answer

**已实现（`xmake build Shop` 通过）**

- 新增 `src/mod/DebugLog.h/.cpp`：`shopDebugEnabled()` / `setShopDebugEnabled()` / `dbgField()` /
  `dbgPos()` / `shopDebugEvent()` / `shopDebugEventThrottled()`。开关是普通 `bool`（读写都在服务器主线程），
  模组关闭时由 `Shop::disable()` 归零。
- 五个结果点：
  - `sign_decision`（`SignInteractListener`）：**每个最终分支只记一次**（`TakeOver` / `PassThrough` / `Block` /
    `NotAShopSign`，冷却期连点带 `cooldown=true`）。早先的写法在算出 `decision` 后先记一次、停摆分支又记一次，
    同一件事会出现两行，已改掉。非商店牌只在**方块实体是告示牌类**时记一行，普通方块被点不进日志。
  - `protection_block`：拦下的唯一出口，带完整坐标与原因（复用 `decision.message`），两类来源：
    容器/方块保护（`open_chest` / `break_shop` / `break_support`）与牌面改动（`sign_color` / `sign_style` /
    `sign_wax` / `sign_unwax`，由 `SignTouch` 分类，见 `SignInteractListener`）。
  - `trade_result`（`TradeService::finishTrade`）：唯一漏斗，覆盖 `ok` / `fail` / `confirm` / `frozen`。
  - `content_block`（`ContentProtectionHooks`）：漏斗拉、漏斗推、投掷器、铜傀儡四处，全部用**节流版**。
  - `economy_resolution`（`EconomyService::resolveProvider`）：解析完记一行，`selection` 与启动日志同一句。
- 节流：连续相同的行 1 秒内只出一条（红石时钟一秒能走 20 次）；只比"连续相同"，换格换类照样记。
- 热路径（漏斗/投掷器/铜傀儡）在调用点先 `if (!shopDebugEnabled())` 短路，关掉时不留字符串拼接。
- `/shop status` 对后端是**只读**的：不调用 `economyBackend()`，免得一条查询命令顺手在记分板上建目标。
- `EconomyService` 的早退分支（认不出配置 / `builtin` / `null`）统一走 `finishResolution()`：既写那句给
  `/shop status` 看的文本，也发 `economy_resolution`；否则查询会把「解析过了但没后端」错报成「尚未解析」。
- `setShopDebugEnabled()` 会清空节流状态：否则 off→on 之后第一条相同事件会被上一次的节流吞掉。
- `dbgField` 需要单独的 `int` 重载：`int` 同时能转 `long long` 与 `bool`（同为 Conversion 级），少这个重载会报歧义。

**只能真机验收的项**

1. `/shop debug on` → 非店主用红染料点牌：**恰好**一条 `sign_decision plan=Block` 与一条 `protection_block
   kind=sign_color`（不是两条 `sign_decision`）。
2. 红石时钟驱动漏斗对着商店箱 → `content_block kind=hopper_pull` 不会一秒刷 20 行（节流生效）。
3. `/shop debug off` → 同样操作一行都不出现。
4. 重启服务器 → `debug` 回到关闭状态。

### 真机第一轮（2026-11-01 18:26–18:30）暴露的两处

1. `sign_decision` 里 `plan` 打成了 `plan=true`：`dbgField(key, char const*)` 会选 `bool` 重载
   （指针转 `bool` 是标准转换，转 `int`/`long long` 不是）。第一轮只把那一处写成 `std::string_view`，
   第二轮 `protection_block` 的 `kind` 又踩了同一个坑（`x ? "a" : "b"` 也是 `char const*`），
   所以最终修的是根因：给 `dbgField` 补 `char const*` 的**精确匹配重载**（精确匹配优于布尔转换，
   用独立小程序验过重载决议：`char const*` / 三元表达式都走新重载，真 `bool` 仍走 `bool`）。
   同一批修掉的还有 `trade_result` 的 `dir` / `outcome`、`content_block` 的 `kind`。
2. 世界已就绪、玩家已进服时 `/shop status` 仍显示「尚未解析」：`ServerStartedEvent` 在 LL 26.51.9 上
   没有在就绪时刻送达（同时解释了启动日志里为什么从来没见过那句选型行）。修法见 01 票与 `DESIGN §19.3`：
   加 `PlayerJoinEvent` 兜底，状态行把「世界未就绪」与「已就绪但解析没跑」分开写。


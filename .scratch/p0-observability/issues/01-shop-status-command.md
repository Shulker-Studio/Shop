# 01: `/shop status`：随时查状态，不用翻日志

**What to build:** 一条 OP 权限的命令，把现在只能"启动时看日志"或"猜"的东西变成随时可查：配置快照、经济后端选了谁（含跳过原因）、10 条 hook 的**注册尝试结果与返回码**、模组版本、调试开关状态。

**Blocked by:** —

**Status:** resolved

- [ ] 注册 `/shop`（`ll::command::CommandRegistrar::getInstance().getOrCreateCommand`，权限 `GameDirectors`）。`CommandRegistrar.h` 在 26.51.8 头文件里；项目目前**一条命令都没有**，这是第一次接命令层
- [ ] `/shop status` 输出：
  - 模组版本（`getSelf().getManifest().version`）；iListenAttentively 版本能拿到就写，拿不到写「未知」，**不编**
  - 配置快照的关键 8 项：`maxShopAmount`、`allowPartialTransactions`、`ignoreCreativeMode`、`allowOwnerOpenChest`、`requireOwnerOnline`、`shopTaxRate`、`shopInteractionCooldownMs`、`economy.backend`
  - 经济后端：**配置值 → 实际选用值**，复用 `backendSelectionText`（与启动日志同一句，不写第二套文案）
  - hook 表：破坏防护 5 条 + 内容防盗 5 条，每条写 `名字 + 已尝试注册(是/否) + attach 返回码`。**措辞不许写成"挂载成功"**：`HookSlot::attached` 只代表挂过了，`attachHooks` 的注释也明确返回值不拿来判成败，行为是否生效仍以手测为准
  - 调试开关状态（见票 02）
- [ ] 世界没起来 / 后端还没解析时，如实输出「尚未解析（世界未就绪）」，不报错、不编值
- [ ] `config.json` 非法导致模组没启用时，这条命令**不存在**（模组没加载就没有命令）。这句要写进测试单的口径说明，免得被当成命令坏了
- [ ] 全部输出走 `CommandOutput`（游戏内与控制台都能看），不要只写 logger——这条命令存在的意义就是绕开不做实时刷盘的 `latest.log`
- [ ] `xmake build Shop` 通过；中文文案与现有日志口径一致

## 备注

- 为能查返回码，给 `HookSlot` 加一个 `int lastReturn`（默认 `0`/记录 attach 返回），**仍然不**把它当成败判据，只用于对照符号漂移。
- 不加配置项开关这条命令（少即是多），权限就是 `GameDirectors`。

## Answer

（待实现）

## Answer

**已实现（`xmake build Shop` 通过，`xmake test` 全绿，逻辑层未改动因而用例数不变）**

- 新增 `src/mod/ShopCommand.h/.cpp`：`/shop` 首次接命令层（`CommandRegistrar::getInstance(false)`，
  权限 `GameDirectors`）。两个重载：`status`、`debug <on|off>`。命令句柄是全局的，重载模组会再跑
  `enable()`，所以 `registerShopCommand()` 用 `static bool` 只挂一次——否则一次 `/shop status` 会执行两遍。
- `DebugParam` 必须放**具名**命名空间：`overload<T>()` 用 `boost::pfr` 反射 `T`，内部链接的类型会
  报 "used but not defined ... does not have linkage"。
- `/shop status` 输出：版本（`Manifest::version` 是 `optional`，缺失写「未知」）、iListenAttentively 版本
  （`ModManagerRegistry::getMod`，未安装写「未安装」）、配置 8 项、`economySelectionText()`（与启动日志同一句，
  未解析写「尚未解析（世界未就绪）」）、两段 hook 表、调试开关。
- `HookSlot` 增加 `int lastReturn`（`attachHooks` 记录返回值）；新增 `destructionHookSlots()` /
  `contentHookSlots()` 两个只读视图。**措辞守住**：只写「已尝试注册，attach 返回 N」，不写成加载成功。
- 顺手修掉一处过期文档：`EconomyService.h` 里「`/shop` 关开一次才会重探」改成「`/ll reload Shop`，或直接重启」——
  项目此前根本没有 `/shop` 命令。

**只能真机验收的项**

1. `/shop status` 在服务器控制台与游戏内都能出结果（`output.success` 多行文本的显示）。
2. 刚启动、世界未就绪时打 `/shop status` → 后端显示「尚未解析（世界未就绪）」；过几秒再打 → 显示实际选型。
3. `config.json` 写坏重启 → 模组不加载 → `/shop` 命令不存在（不是报错）。

### 真机第一轮（2026-11-01 18:26–18:30）的发现

- 命令本身能用：控制台与游戏内都出结果，10 条 hook 全部「已尝试注册，attach 返回 0」，版本两行正确。
- **`ServerStartedEvent` 不可靠**：18:29:18 玩家已经进服，18:29:32 的 `/shop status` 仍显示「尚未解析」；
  直到 18:30:09 玩家点了一次牌（表单要读余额）才解析出 `配置 auto → 选用 legacy_money`。
  也就是说启动那一刻的解析从来没有跑过，启动日志里那句选型行也因此从来没出现过。
  修法：`PlayerJoinEvent` 也解析一次（主要依靠这一条），并保留懒解析；`DESIGN §19.3` 记录此事。
- 状态行措辞改细：世界已就绪但还没解析时写「尚未解析（世界已就绪，会在玩家进服或第一次交易时解析）」，
  不能与「世界未就绪」混成一句。
- `shopTaxRate` 显示从 `0.000000` 改成 `0`（浮点配置走最短表示）。


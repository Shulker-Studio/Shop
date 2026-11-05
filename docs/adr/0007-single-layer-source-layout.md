# 源码单层，不做镜像层，回归验收走真机命令

`src/` 下按功能分目录，不再分 `shop`（纯逻辑）/ `mod`（引擎）两层；代码可以直接用 `mc/`、`ll/` 类型；回归验收靠服务器里的 `/shoptest` 命令。

```
src/
├── Shop.h / Shop.cpp       插件入口：加载、配置重载、装配监听器
├── MemoryOperators.cpp
├── event/                  6   SignEditListener, SignInteractListener,
│                              ContainerProtectionListener, ContentProtectionHooks,
│                              DestructionProtectionHooks, HookSlots, BlockResync*
├── command/                2   ShopCommand, TestCommand
├── form/                   5   InfoForm, UnitsForm, ConfirmForm, ManageForm, FormModel
├── core/                  28
│   ├── sign/               4   ShopSign, SignGeometry, SignProtection, SignEditPlan
│   ├── shop/               6   ShopInfoData, ShopReader, ShopInspector, SignOwner,
│   │                          BundleRules, ItemBanList
│   ├── container/          4   ContainerFinder, ContainerProtection,
│   │                          ContentProtection, DestructionProtection
│   ├── trade/              7   TradeService, TradePlan, TradePricing, ClickCooldown,
│   │                          Units, TradeRecord, DiscardRecord
│   └── economy/            4   Provider, Service, LegacyMoney, Scoreboard
└── util/                  13
    ├── config/             2   Config, ConfigPatch
    ├── i18n/               4   Text, Locale, Message, Reason
    ├── log/                6   Files, Transaction, Discard, Debug, JsonLine, Rotation
    └── time/               1   Time
```

`* BlockResync` 暂留 `event/`，名字和位置都待定：它被两个监听器和 `ShopForm` 调用，是不是通用世界同步服务还没验证。

`Shop.h` 留在 `src/` 根，与官方模板、LK-Stats 的 `MyMod.cpp`、STweaks 的 `Entry/` 同一习惯。

## 三条硬决定

**1. 不保留可测清单，不保留 `check_pure_logic_headers()`。** 断言存在的唯一目的是让 `ShopTests` 能脱离 BDS 编译；测试删了，断言就没有服务对象。

**2. 不按类型名批量砍镜像。** 只有被证明「只为引擎转换而存在」的结构才删。已经排除的：`ShopInfoData`（表单投影，52 处引用）、`TradePlan` / `StockView`（业务状态）、`TradeRecord`（审计事实），它们不是镜像。候选只有 `Cell`、`SignForm`、`HorizontalFacing`，而它们同时承载几何语义（朝向 → 附着点、双箱定位），必须单独验证后再动，且不能标为「零行为变化」。

**3. 测试换成 `/shoptest`。** 注册在 `command/`，GM 权限，先做两个场景：

```
/shoptest buy         正常路径：钱、货、箱子三方对齐
/shoptest compensate  人为制造给货失败（塞满背包），验证退款退货都还原
```

用 `output.success()` / `output.failure()` 打印断言结果，形态照 LSE 的 `tests/LSETests`（一个 mod + 一条命令，真机跑）。`reject` / `partial` / `admin` 手动交易能测，不写进命令。

## 不做独立测试的理由

生态现实：官方模板、InventoryBackup、STweaks、ChestTrading 都没有测试；LK-Stats 有，但是裸 `main()` + `std::cerr` 手判失败数，没有框架；LSE 的 `tests/LSETests` 是跑在真服务器上的 mod。真正有测试基础设施的只有 LeviLamina 本体（gtest，链引擎，`has_config("tests")`）。

`tests/` 5273 行占仓库代码量 37%，删掉同时带走 `ShopTests` target 与 `check_pure_logic_headers()`。

**已知风险**：`ShopSign` 的四行解析（`B`/`S`、`K`/`M`、小数取整、`int64` 溢出、颜色码、要拒绝 `Q 64 : C 64`）从此只能真机摆牌子试；`ContainerProtection` / `ContentProtection` 的判定同理。

## 子目录是定位方式，不是分层约束

不要承诺 `sign → container → shop → trade` 的单向依赖，实测是双向的：

- `core/container/ContainerFinder` → `core/shop/ShopInfoData`（`BoxCell`）+ `SignOwner`
- `core/shop/ShopInfoData` → `core/trade/TradePlan`（只为 `TradeDirection`）
- `core/shop/ShopReader` → `core/container/ContainerFinder`

第二层语义：顶层回答「谁触发我」（`event/` 游戏回调、`command/` 玩家指令、`form/` 界面、`util/` 无触发方的工具），`core/` 内部回答「我处理哪块业务」。

## 命名约定

单平台项目不留平台抽象：全项目只有一个虚接口 `EconomyProvider`，它抽象的不是平台而是同平台的三种经济来源（LegacyMoney DLL / 记分板 / 内置）。没有 adapter 层。

| 现在 | 改成 | 说明 |
| --- | --- | --- |
| `ContainerFinder`（几何）+ `ContainerFinderBds`（引擎） | `SignGeometry` + `ContainerFinder` | 几何归 `sign/`，引擎遍历归 `container/` |
| `ShopService` 一个文件 | `SignEditPlan` + `ShopReader` + `ShopInspector` | 它实际是三段：编辑决策 / 读商店 / 看交互 |
| `ShopInfoData` | **保留原名** | 与已有的 `ShopSnapshot`（`ShopInfoData` + `ownerXuid`）区分，52 处引用不动 |
| `ShopForm` | `InfoForm` + `UnitsForm` + `ConfirmForm` + `ManageForm` + `FormModel` | 表单布局与领域角色分开 |
| `Clock.h` + `WallClock.h` | `util/time/Time` | 单调钟 + 本地时间戳，18 行 |
| `Economy` / `EconomyService` / `LegacyMoneyEconomy` / `ScoreboardEconomy` | `economy/Provider` / `Service` / `LegacyMoney` / `ScoreboardBackend` | 去掉重复前缀；`Scoreboard` 与引擎同名类型冲突，加 Backend |
| `DebugLog` / `LogFile` / `TransactionLog` / `DiscardLog` / `LogRotation` | `log/Debug` / `Files` / `Transaction` / `Discard` / `Rotation` | 日志统一进 `log/` |
| `mod/Text` + `shop/Text` | `i18n/Text`（树）、`Locale`（装载 + 翻译 + 渲染）、`Message`（发送） | 实际是三件事，不合并 |
| `TradeGate` | `trade/ClickCooldown` | 它只是连点冷却 |
| `StockView` | `trade/UnitCount` | struct，不是视图；`Units` 已被 `ChangeReason::Units` 占用 |
| `ProductRules` | `shop/BundleRules` | 26 行只有一个函数，只判收纳袋 |

规则：不加 `Bds` 后缀；不用 `Info` + `Data` 双后缀；监听与 hook 按注册方式命名（`*Listener` 走事件、`*Hook` 打函数）。

## 为什么不按 MVC

MVC 的三个角色横跨两侧：View 是 `form/`，Controller 是 `event/` 加 `core/trade/TradeService` 的编排，Model 是 `core/` 的数据与判定。按 MVC 分目录会把一次交易拆到三个顶层目录。传统 Bukkit 商店也不是 MVC——ChestShop-3 是事件总线 + 责任链（`PreTransaction` 里 9 个 checker 依次 `if (cancelled) return`），搬不过来，因为 `TradeService` 是显式顺序调用。

## 实施顺序

五步，每一步独立构建、独立验收。

| 步 | 内容 | 风险 |
| --- | --- | --- |
| 1 | 纯移动 + `#include` 路径改写，一行逻辑不改 | 低，编译通过即验证 |
| 2 | 源文件职责拆分：`ShopInfo` 四份、`ShopService` 三份、两份 `Text` 三份、`ShopForm` 五份、`ShopSign` 的 `.cpp` 三份 | 中，纯移动代码但接口要重新组合 |
| 3 | 引擎类型迁移与镜像删除（`Cell` → `BlockPos` 等） | **高，不是零行为变化**：维度表达、对象生命周期、双箱定位都会变 |
| 4 | 删 `tests/`、`ShopTests`、`doctest`、`check_pure_logic_headers()` | 低 |
| 5 | `/shoptest`，作为最后的真机验收命令 | 低 |

第 3 步之前必须先做逐个模块的镜像判定表，不能按类型名批量砍。第 5 步不作为前四步的前置条件。

## 修订（2026-11-05）

本 ADR 改过六稿。前四稿都在「怎么分两层」里打转：两层内部继续平铺 → `mod` 分五区 → 两层都分（`shop` 五组 + `mod` 六目录）→ 单层功能目录但保留可测清单。第五稿删掉了清单与测试。第六稿修正了第五稿的三处硬伤：

- 编造的依赖箭头 `sign ← container ← shop ← trade`。实测 `ContainerFinderBds.h` 已 include `ShopInfo.h`，方向是 `container → shop`，且 `ShopReader → ContainerFinder` 构成循环。目录改为「定位方式」而非「分层约束」。
- 把 `ShopInfoData` 改名 `ShopSnapshot` 会撞上 `ShopService.h` 里已有的 `ShopSnapshot`，且 52 处引用不值当。保留原名。
- 「目录移动 + 改名 + 砍镜像」被当成零行为变化。实际上引擎类型迁移会改变维度表达与对象生命周期，必须独立成高风险步骤。

分区依据也试过按业务域（`sign/ trade/ protection/ chest/`）和按后端分层词（`app/ platform/ integration/`），前者拆散同类回调，后者在插件语境里不成立。

## 实施进度（2026-11-05）

`refact` 分支，六个提交，每次构建通过。

| 步骤 | 状态 | 提交 |
| --- | --- | --- |
| 1 纯移动 + include 改写 + 删测试设施 | 完成 | `000b3c3` |
| 2a 文件名去后缀、ContainerFinder 拆分、Time 合并、economy 改名 | 完成 | `5bc0edc` |
| 2b `ShopOwner`/`TradeGate`/`StockView` 标识符改名 | 完成 | `bb36fc9` |
| 2c `ShopInfo` → `ShopInfoData` / `Reason` / `ShopFormRole` / `FormModel` | 完成 | `5dd5d4e` |
| 2d `ShopService` → `SignEditPlan` / `ShopReader` / `ShopInspector` | 完成 | `90ff1ed` |
| 2e `Text` → `Text` / `Locale` / `Message` | 完成 | `eda8b7d` |
| 2f `ShopForm` 拆成信息 / 管理 / 份数 / 确认四张表单 | 完成（比原计划细：多出 `FormSupport`、`ActionDispatch`、`ManageView`） | `73105ad` |
| 3 引擎类型迁移与镜像删除 | 未开始 | — |
| 4 删测试设施 | **提前到步骤 1**（目录移动后 `src/shop/**` 不存在，`ShopTests` 无法定位） | `000b3c3` |
| 5 `/shoptest`（buy / compensate） | 完成 | `7b35178` |

与原计划的偏差：`StockView` 改名为 `UnitCount` 而不是 `Units`（`ChangeReason::Units` 已占用该名字）；`ScoreboardEconomy` 改为 `ScoreboardBackend` 而不是 `Scoreboard`（后者与引擎的 `Scoreboard` 类型同名）；测试设施在步骤 1 一并删除，否则中间状态无法构建。

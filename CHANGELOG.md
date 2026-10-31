# 更新日志

本项目所有值得记录的变更都会写在这里。

格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 新增

- 基于 LeviLamina 模组模板初始化工程：target `Shop`，命名空间 `ss::shop`，类 `Shop`。
- 依赖 LeviLamina `26.51.8` 与 iListenAttentively `0.17.0`（事件层）。
- `DESIGN.md`：完整设计文档（数据模型、事件与 hook、交易流程、商店箱保护、经济契约、决策记录）。
- `README.md`：中文项目说明与构建/安装指引。
- `tooth.json`：填充仓库地址、运行时依赖与发布产物路径。
- 告示牌四行解析与规范化（`src/shop/ShopSign.*`）、份数计价（`src/shop/TradePricing.*`），以及 doctest 测试设施（`xmake test`）。
- 开店流程：监听告示牌正面编辑，校验并规范化四行，失败只提示不删牌（`src/mod/ShopService.*`、`SignEditListener.*`）。
- 绑定规则：牌子必须附着在容器上（墙牌看背面、站立看下方、吊牌看上方），双箱合并为 54 格，一个容器只被一块牌绑定（`src/shop/ContainerFinder.*`、`src/mod/ContainerFinderBds.*`）。
- `config.json` 加载与校验：`adminShopName`、`shopContainers`、`maxShopAmount`、`blockSellPriceHigherThanBuy`、`shopInteractionCooldownMs`；配置非法时拒绝启用。
- 点牌看店：`PlayerInteractBlockEvent` 只拦"正面是合法商店牌 + 附着容器可用"的牌子，弹 `SimpleForm` 展示店主、商品、每份件数、买入价、卖出价、可成交份数与余额（`src/mod/SignInteractListener.*`、`ShopForm.*`）。
- 店主判定：读正面 `mLastEditXuid` 经 `PlayerInfo` 反查当前名（`src/mod/ShopOwner.*`）；编辑落地后在 `PlayerEditSignAfterEvent` 里补写一次 ownerID（记录的 XUID 与本次编辑者不一致时），补上包路径不写 `mLastEditXuid` 的空洞。
- 可成交份数与商品统计：商品以第一个非空槽位为准，合并双箱统计严格同种（类型 + aux + 用户数据）的总件数（`src/mod/ContainerFinderBds.*`）。
- 表单文案、连点冷却与可成交份数都是纯逻辑（`src/shop/ShopInfo.*`、`TradeGate.*`、`TradePricing::unitsFromItems`），有 doctest 覆盖。
- 经济后端契约与空实现（`src/shop/Economy.*`）：金额一律 `int64`，`transfer` 失败保证双方余额不变，补偿失败返回 `Broken`；`NullEconomy` 读 0、不算可用后端。
- 记分板经济后端（`src/mod/ScoreboardEconomy.*`）：余额 = 玩家在 `economy.scoreboardObjective` 上的分值，写入前过 `toScoreValue` 检查 `int32` 范围，超范围拒绝而不是截断；只支持在线玩家。
- 后端选择与账户查询（`src/mod/EconomyService.*`）：按 `economy.backend` 惰性选定（04 只有记分板与空实现，目标必须已存在），选不出可用后端时打错误日志并降级为「交易全部失败」。
- 买入交易（`src/shop/TradePlan.*`、`src/mod/TradeService.*`）：`planBuy` 纯逻辑算出成交份数、件数与总价（含部分成交与原因判定）；`buyUnits` 按「预校验 → 快照 → 先扣钱 → 先取货再给货」执行，任一步失败按快照回写物品并退回钱款；补偿失败打 `fatal` 并通知在线 OP。
- 买几份的份数表单（`src/shop/BuyForm.*`、`src/mod/ShopForm.*`）：下拉框 `1 / 2 / 4 / 自定义份数`，正文逐档列出总件数与总价；上限 = `maxShopAmount`、可成交份数、余额、背包取最小（DESIGN §10.1 第 3 步 / §10.3）。只能部分成交时先弹确认表单，把重算后的真实份数与金额摆出来，确认后才扣款。
- 容器刷新与冻结：成交后对商店箱两半各 `BlockActor::refresh` 一次（DESIGN §7.3 / §10.2 第 9 步）；补偿失败时把「玩家 + 商店」钉进内存冻结名单，后续提交拒绝并通知在线 OP。
- 部分成交/改价的确认：实际份数或金额与份数表单上看到的不一致时，`buyUnits` 不动任何钱货，先弹确认表单，玩家点头才成交（DESIGN §10.3）。
- 表单「购买」按钮接上真交易：回调不碰世界，捕获 XUID 与牌子坐标后切到服务端线程重新解析玩家与商店（`ShopForm.*`、`TradeService::requestBuyForm`/`submitBuy`），并加 250ms 提交冷却。
- `config.json` 新增 `allowPartialTransactions` 与 `economy`（`backend` / `scoreboardObjective` / `currencySymbol`），以及 `ignoreCreativeMode`（创造模式禁止交易）；表单余额与货币符号改为读真实后端与配置。

### 变更

- `TradePricing::stockUnits` 改名为 `unitsFromItems`：商店箱的货与背包容量共用同一个「件数 → 份数」算式。
- `ShopSnapshot` 拆成「只看世界」的 `readShop` 与「看视角 + 读余额 + 定角色」的 `inspectShop`，交易路径复用前者。
- `ContainerFinderBds` 导出 `AttachedContainer` / `mergedSlots` / `sameShopItem` / `firstProductStack`，「合并槽位的顺序与边界」与「严格同种比较」各只有一处定义。
- `xmake.lua`：移除 server/client 双目标，固定为服务端模组；版本号由 `modpacker` 显式指定为 `0.1.0`。
- `manifest.json`：移除未被替换的 `platform` 占位符。
- CI：`build.yml` / `release.yml` 只构建服务端产物。

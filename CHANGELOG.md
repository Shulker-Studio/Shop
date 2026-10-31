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
- 表单文案、连点冷却与库存份数都是纯逻辑（`src/shop/ShopInfo.*`、`TradeGate.*`、`TradePricing::stockUnits`），有 doctest 覆盖。

### 变更

- `xmake.lua`：移除 server/client 双目标，固定为服务端模组；版本号由 `modpacker` 显式指定为 `0.1.0`。
- `manifest.json`：移除未被替换的 `platform` 占位符。
- CI：`build.yml` / `release.yml` 只构建服务端产物。

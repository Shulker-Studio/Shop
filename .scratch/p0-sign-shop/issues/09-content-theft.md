# 09: 内容防偷

**What to build:** 商店箱的内容只能通过交易流动。四条路径全部堵死：漏斗方块抽/塞、漏斗矿车吸、铜傀儡取放、投掷器灌注。漏斗方块与漏斗矿车共用 `Hopper` actor 的同一套抽取/推出逻辑，一个 hook 覆盖两种形态；铜傀儡走 `TransportItemsGoal`（靠打开箱子取放），粒度比漏斗粗，要确认取消它不会把傀儡变成站桩；投掷器走自己的 `pushOutItems`。发射器只发射自己格子里的东西，不碰别的容器，不用管。

**Blocked by:** 02

**Status:** resolved

- [x] 漏斗在商店箱上方 → 抽不走物品
- [x] 漏斗在商店箱下方 → 塞不进物品
- [x] 漏斗矿车停在商店箱下方 → 吸不走物品
- [x] 同一个 hook 对漏斗方块与漏斗矿车同时生效（两种形态各验一次）
- [x] 铜傀儡走到商店箱旁 → 取不走货
- [x] 铜傀儡试图往商店箱放货 → 放不进
- [x] 铜傀儡的拦截粒度已确认，没有把它变成站桩傀儡
- [x] 投掷器朝商店箱喷物品 → 塞不进去，物品落在地上
- [x] 以上装置对普通箱子照常工作
- [x] 拦截点与验证结论记录在文档里；无法拦截的部分明确记为已知缺口，不静默放过

> 勾选口径与 07／08 一致：勾上表示**代码已落地、有单测或文档支撑**；BDS 侧行为只能真机确认，
> 逐条列在 Answer 末尾的「只能真机验收的项（已转 12）」，对应 12 的 `### 从 09 转来的待验项`。

## Answer

**已实现（`xmake build Shop` / `ShopTests` 通过，`xmake test` 180 cases / 2761 assertions 全绿）**

- `src/shop/ContentProtection.{h,cpp}`（新，纯逻辑，不含引擎类型）：
  - `cellInDirection(Cell, int facingDirection)`：把基岩 `facing_direction`（0 下 / 1 上 / 2 北 / 3 南 / 4 西 / 5 东）换成目标格。偏移量取自 `neighboursOf`，所以「北是 z-1」这类事实仍然只有一处定义；方向越界返回空，调用方放行（脏方块状态不该被猜成某个方向）。
  - `cellAbove(Cell)`：漏斗抽取的源容器永远在正上方——`_tryPullInItemsFromAboveContainer` 只拿到自己的位置，没有方向参数。
  - `shouldSkipContentTransfer(bool targetIsShopContainer)`：**四条路径共用的判据**——目标容器是商店箱就跳过**这一次**搬运。入参就是 `isShopBlockAt` 的答案，没有身份、方向、白名单分支，所以不可能出现「店主可以拿漏斗搬」或「只拦抽不拦塞」的第二种口径。
- `src/mod/ContentProtectionHooks.{h,cpp}`（新，5 条 hook，沿用 08 的 `ss::shop::detail` + 具名命名空间 + 表驱动注册）：
  - H2a-1 `Hopper::_tryPullInItemsFromAboveContainer`：源容器在漏斗正上方一格，是商店箱就返回 `false`（这一次抽不动）。
  - H2a-2 `Hopper::_pushOutItems`：目标容器在 `attachedFace` 那一格，是商店箱就返回 `false`（这一次塞不进）。**这两条同时覆盖漏斗方块与漏斗矿车**——两种形态共用 `Hopper` actor 的同一套抽取/推出逻辑，所以只有一处判定，不按形态各写一遍。
  - H2c `TransportItemsGoal::_executeEvent`：只丢掉 `StartTakeInteractionWillSucceed` / `StartPlaceInteractionWillSucceed` 两个事件，`canUse` / `canContinueToUse` / `tick` 一个都不碰——不取消 goal，因此不会因为 goal 被取消而失去运输行为。
  - H2d `DropperBlockActor::pushOutItems` + `DropperBlockActor::getContainerAt`：入口只开一个「这一次的目标是商店箱」的 thread-local 作用域，内层容器查找对这一格返回 `nullptr`，原版因此走「喷到地上」的分支，物品落在地上而不是留在投掷器里；只要这一次**没真正挡到**（内层没被问过，或问了但坐标对不上）就打一条 warn 并说明是哪种失效。
- `src/mod/ContainerFinderBds.*`：新增 `facingDirectionAt`（投掷器的朝向只能从方块状态读），状态名的命名空间差异复用既有的 `readIntState`，不另写一份查表循环。
- `src/mod/Shop.{h,cpp}` + `src/mod/HookSlots.*`：`currentShopConfig()` 与「一张 `HookSlot` 表 + 挂上 / 逆序解开」各提成一份实现，08 的破坏防护改为复用它们（原先 08 里的 `currentConfig()` 与注册循环删除）。**这是被逼出来的复用**：09 的第一版在 `ss::shop::detail` 里又定义了一份 `HookSlot` / `gSlots`，与 08 的同名外部符号直接撞（LNK2005），与其改名绕过，不如把这段同形代码收成一份——「配置缺失就放行」的语义也因此只有一处定义。
- `Shop.cpp`：`enable` 在破坏防护之后注册这 5 条、`disable` 按逆序先解这 5 条。
- `tests/shop/ContentProtectionTest.cpp`（新，4 个中文用例）：六个方向与 `neighboursOf` 逐一对照、越界方向判不出格、漏斗抽取的源格是正上方、商店箱跳过 / 普通容器放行。

**决策与偏离**

- **判据只组装一次**：四条路径都走 `shouldSkipContentTransfer`，「这一格是不是商店箱」都问 `isShopBlockAt`（与 07 的拆除、08 的四条 hook 同一个判据）。纯逻辑只解决几何与「跳不跳」，hook 里没有任何第二份判定。
- **投掷器不照搬「入口提前返回」**：提前返回会把物品留在投掷器里、喷不出去，与票面「物品落在地上」不符。改成「入口开作用域 + 内层 `getContainerAt` 交不出商店箱」，让原版自己走找不到容器的分支。代价是多压一层「`pushOutItems` 会先查目标容器」的假设，所以内层没被问过时 `pushOutItems` 会打 warn（见「已知边界」）。
- **铜傀儡不碰 goal 生命周期**：`canUse` / `canContinueToUse` / `tick` 都是 goal 级开关，取消任何一条都会让这台傀儡这一趟运输白跑（`canUse` 恒 false 甚至会让它不再有运输行为），也是票面点名的「站桩」风险。落点定在 `_executeEvent` 的两个「即将成功」事件上——这是头文件里唯一能表达「这一次取/放即将发生」的切点。
- **`_executeEvent` 只拦成功分支**：「会失败」的事件本来就不会搬东西，拦它只是无意义的副作用；`FinishedInteraction` 必须放行，否则 goal 卡在交互中反而会真的把傀儡钉在箱子旁。
- **不接 `preventHopperExtraction` 配置键**：§9.2 规定这三条路径对非店主、店主、OP 一律阻止，没有「关掉就不防」的语义，所以这个键仍然不生效（已在 DESIGN §15 的注记里点名，免得服主以为它管用）。
- **保护对象与 07／08 完全一致**：只看「这一格是不是商店箱」（牌面「像商店」+ 附着容器），不看 `shopContainers` 白名单——改配置或脏数据不该让商店箱突然能被漏斗搬空。
- **配置缺失一律放行**：5 条 hook 第一句都是 `currentShopConfig()`，为空直接 `origin`，与 08 同一条兜底。

**已知边界**

- **`hook()` 的返回值不是契约**：与 08 同一条——挂上就记 `attached`，非 0 只留一条 warn，真正的判据是行为是否生效（12 有验收项）。变量符号在 26.51.8 头文件里都存在，但注册成功不保证语义对。
- **投掷器这条 warn 每次挂载都会复位**：`DropperTransferGuard` 在 `registerContentProtectionHooks()` 里清空，所以 `/shop` 关掉再开一次后，如果内层仍然没被问过，会重新报一条——不会因为「已经报过」而永久沉默。
- **投掷器这一环是本票最脆的**：整个拦截依赖「`pushOutItems` 会先向 `getContainerAt` 要目标容器」。若这条版本的实现不走它（或内层 symbol 漂移、或它收到的 `Vec3` 不是方块原点），投掷器会照常把货塞进商店箱。这两种失效都会打一条 warn 并在文案里分开（「没被问过」/「问了但坐标对不上」），不是静默；退路是在入口直接返回，代价是物品留在投掷器里、不落地。
- **铜傀儡这条有两件事没证实，都不能当结论**：① `TransportItemsGoal::_executeEvent` 是 `const` 的通知式接口，「即将成功的取/放」是否真的由它触发搬运，从头文件看不出来；② 同一个函数也可能是状态推进点（丢掉 `FinishedInteraction` 会卡住，正说明这一点），所以丢掉 `Start*` 后 goal 有可能停在 `Interacting` 等一个永不开始的交互，表现是傀儡在箱子旁反复开箱或停住——那样货虽然搬不走，却是票面第 7 条要避免的形态。两条都在 12 里单独验收；退路一致：`$canContinueToUse` 在 `mState == Interacting` 且目标是商店箱时返回 false（引擎自己 `stop()` 关箱清状态，代价是这一趟运输白跑）。
- **铜傀儡只认 `mTargetContainerPosition`**：这个字段为空时放行（正常状态下此时也不会搬运）。若真机观察到「货被搬走但那一刻该字段为空」，说明目标记在别处，需要在 12 里记下现场再补判据。
- **`[x]` 的含义**：勾上表示代码已落地、有单测或文档支撑；票面第 7 条（粒度 / 不变站桩）是按上述设计论断勾的，真机结论以 12 的验收结果为准，未通过时按节点退路改。
- **漏斗的方向口径沿用参考实现**：`_tryPullInItemsFromAboveContainer` 的 `pos` 当作装置自己（源在 `above()`），`_pushOutItems` 的 `attachedFace` 当作搬运方向。两处都只能在真机确认；若「塞」没挡住，说明口径反了，把 `cellInDirection` 的入参取反即可（一处改动）。
- **`facing_direction` 是投掷器朝向的唯一来源**：读不到就放行。`facing_direction` 在基岩版里是 0..5 的整数状态（与 `Direction` 索引同序），但这条假设也只在真机上验得到；映射错会表现为「拦错了方向」，不会误伤无关容器（判据仍要求那一格是商店箱）。
- **漏斗是热路径**：每次抽取/推出都多一次 `isShopBlockAt`（先看有没有方块实体，不是容器/牌子就直接 false）。大型漏斗农场会走很多次，但都只是方块实体查找，不解析牌面文本。
- **`getContainerAt` 的作用域是 thread_local**：只在 `pushOutItems` 为商店箱开的那一小段时间里生效，别的调用方问同一格拿到的仍是真容器。若将来有实现跨线程搬箱子，这个作用域不会跟着跑（投掷器目前只在服务端主线程 tick）。
- **不动的部分**：发射器不碰别的容器（票面已说明），所以没有 hook；玩家手动塞/取、其他模组的自动化不在本票范围。

**指标**

- `xmake build Shop`：`Shop.dll` 构建通过（clang-cl，`/W4` 无新增告警）。
- `xmake build ShopTests` + `xmake test`：180 cases / 2761 assertions 全绿（08 之后 176 / 2736）。
- `git diff --check`：无空白问题。

**只能真机验收的项（已转 12）**

1. 漏斗方块在商店箱上方抽 / 下方塞都无效；换成普通箱子两个方向都照常。
2. 漏斗矿车停在商店箱下方吸不走货（验「一条 hook 覆盖两种形态」）。
3. 铜傀儡走到商店箱旁取不走、也放不进。
4. 铜傀儡的拦截粒度：面对商店箱几次之后仍会离开、仍能对普通箱子继续取放，没有变成站桩。
5. 投掷器朝商店箱喷物品塞不进去、物品落在地上；朝普通箱子照常灌入。
6. 五条 hook 都真挂上了（启动日志没有「hook 注册返回 …」的 warn）。
7. 配置非法时不误拦：模组未启用时漏斗 / 矿车 / 铜傀儡 / 投掷器对普通箱子与商店箱都完全不拦。
8. `/shop` 关掉再开一次，钩子不会叠加成两条。

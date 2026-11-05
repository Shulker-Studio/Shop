# 适配 LeviLamina 26.60：编译期与链接期发现

对照版本：`levilamina 26.51.8` → `26.60.0`（`iListenAttentively` 0.17.0 → 0.18.0）。

## 一、签名变更（编译期报错，已修）

### `Level::destroyBlock` 多了一个参数

```cpp
// 26.51
virtual bool destroyBlock(BlockSource& region, BlockPos const& pos,
                          bool dropResources, BlockChangeContext const& changeSourceContext);
// 26.60
virtual bool destroyBlock(BlockSource& region, BlockPos const& pos,
                          bool dropResources, BlockChangeContext const& changeSourceContext,
                          BlockDestroySound destroySound);   // Play / Silent
```

没有保留 4 参重载，5 个实参必须全给。受影响两处：

| 位置 | 场景 | 处理 |
| --- | --- | --- |
| `src/mod/ContainerProtectionListener.cpp:229` | 拆双箱时主动打掉绑着的商店牌 | 传 `BlockDestroySound::Play`（保持原来「有声音、有掉落」） |
| `src/mod/DestructionProtectionHooks.cpp:139` | `WitherDestroyCellHook` 挂在 `&Level::$destroyBlock` 上 | hook 原型补 `BlockDestroySound destroySound`，原样透传给 `origin` |

## 二、MCNAPI（26.51 是 MCAPI，26.60 标成 `[[deprecated]] MCAPI`，符号不再导出）

> **26.60.1 已全部恢复为 `MCAPI`**（`SignBlockActor::getSideFacingPlayer`、`setMessageForServerScripingOnly(SignTextSide, string, string)`、
> `DropperBlockActor::pushOutItems`、`TransportItemsGoal::_executeEvent`），源码不需要任何绕法。
> 申请已提：https://github.com/LiteLDev/mcapi-requests/issues/285
> `SignBlockActor::setMessage(SignTextSide, ...)` 与 `setMessageForServerScripingOnly` 的 `TextObjectRoot` 重载仍是 `MCNAPI`，Shop 不用。
> 因此 Tooth 依赖下限定为 `>=26.60.1 <26.61.0`，不是 `26.60.0`。

编译只给 `-Wdeprecated-declarations` 警告，**链接期才是真报错**：`LNK2019 无法解析的外部符号`。

| 符号 | 调用点 | 用途 | 26.60 里还有什么可用 |
| --- | --- | --- | --- |
| `SignBlockActor::setMessageForServerScripingOnly(SignTextSide, std::string, std::string)` | `src/mod/ShopForm.cpp:382`、`src/mod/ShopOwner.cpp:92` | 服务端改牌面文本（管理表单改价、纠正店主 XUID） | 无。26.60 把 `setMessage` 的 4 个重载、`Text::setMessage/load/save`、`SignBlockActor` 构造函数**全部** MCNAPI 掉了，官方写牌路径整条断 |
| `SignBlockActor::getSideFacingPlayer(Player const&) const` | `src/mod/ShopService.cpp:259` | 判断玩家点的是正面还是背面 | 无。`_playerCanUpdate` 是 MCAPI 但语义不同 |
| `DropperBlockActor::pushOutItems(BlockSource&)` | `src/mod/ContentProtectionHooks.cpp:162` | 投掷器防偷钩子 | `DropperBlockActor::_tryMoveInItemsAndDepleteStack(Container&, ItemStack&, int stackSizeLimit, int slot, int face)` 在 `LL_PLAT_S` 下仍是 MCAPI，是这条钩子的天然替代点 |
| `TransportItemsGoal::_executeEvent(Event) const` | `src/mod/ContentProtectionHooks.cpp:249` | 铜傀儡防偷钩子 | 无。`_executeEvent/_canSeeTarget/_reset/_startCooldown` 与所有 `$xxx` thunk 都已 MCNAPI |

牌面文本可考虑的绕法（未实施）：`mTextFront` / `mTextBack` 本身是 `ll::TypedStorage<std::unique_ptr<SignBlockActor::Text>>`，字段（`mMessage`、`mLastEditXuid`、`mGlowing`…）都还是普通成员，直接改字段再走现有的 `resyncBlockActorToDimension` 发包，不需要碰任何 MCNAPI 函数。

## 三、确认无变化的部分

`ll/api/event`（`EventBus`、`ListenerBase`、各 player/world 事件）、`ll/api/mod`（`Mod`、`NativeMod`、注册）、`ll/api/command`、`ll/api/form`、`ll/api/service`（`Bedrock`、`PlayerInfo`）、`ll/api/memory/Hook`、`ll/api/data/Version`、`ll/api/Config`：Shop 用到的接口在 26.60 都没有签名变化。

`ll/api/i18n/I18n.h` 有改动，但只是 `tr` 内部实现和 `I18nStringError::message` 的 locale 回退：`I18n::get(key, localeCode)` 不再在空 locale 时替默认语言。`src/mod/Text.cpp` 的调用点已经自己算好 `effectiveLocale`，行为不变。

`ll/api/io/Logger.h` 的 sink API 改成按名字取（`setSink(name, sink)` / `removeSink(name)` / `getSink(name)` / `clearSinks()` / `sinks()` 返回 `pair<name, Sink&>`），Shop 没用 sink，不受影响。

## 四、还不能静态确认的

- `mc/` 结构体布局变化（26.60 换了 BDS 版本）：编译不报错，只能真机验。重点是牌面 NBT、容器槽位、`ItemStack` 复制这几条路径。
- 发布目录从 `plugins/` 改名 `mods/`：`tooth.json` 的 `dest` 已改；代码里的日志目录走 `getModDir()` 运行期解析，不受影响，只有文档/注释里的 `plugins/Shop` 字样需要同步。

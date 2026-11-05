# Shop 的 mcapi-requests 草稿（26.60 MCNAPI）

四个符号 26.51.8 都是 `MCAPI`，26.60.0 变成 `MCNAPI`，链接期 `LNK2019 / LNK2001`。
`Hopper::_pushOutItems` 虽然在 obj 里也是 undefined，但链接器能解析到，不在申请范围。
模板来自 `.github/ISSUE_TEMPLATE/request.yaml`（Mod / Platform / Justification / Classes / Symbols），
`Additional context` 沿用本仓库前几张票（#279、#284）的写法。

---

## title

```text
Request symbols for Shop
```

## body

````markdown
### Mod

https://github.com/Shulker-Studio/Shop

### Platform

Server for Windows

### Justification

Shop 是告示牌箱子商店模组。这四个符号在 26.51 是 `MCAPI`，26.60 变成 `MCNAPI`：前两个用于服务端改写牌面文本和判断告示牌正反面，后两个是拦截投掷器与铜傀儡搬走商店箱物品的钩子。

### Classes

_No response_

### Symbols

```text
__imp_?setMessageForServerScripingOnly@SignBlockActor@@QEAAXW4SignTextSide@@V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@1@Z
__imp_?getSideFacingPlayer@SignBlockActor@@QEBA?AW4SignTextSide@@AEBVPlayer@@@Z
__imp_?pushOutItems@DropperBlockActor@@QEAA_NAEAVBlockSource@@@Z
__imp_?_executeEvent@TransportItemsGoal@@QEBAXW4Event@1@@Z
```

### Additional context

```text
ShopForm.cpp.obj : error LNK2019: 无法解析的外部符号 "__declspec(dllimport) public: void __cdecl SignBlockActor::setMessageForServerScripingOnly(enum SignTextSide,class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> >,class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> >)" (__imp_?setMessageForServerScripingOnly@SignBlockActor@@QEAAXW4SignTextSide@@V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@1@Z)
ShopOwner.cpp.obj : error LNK2001: 无法解析的外部符号 "__declspec(dllimport) public: void __cdecl SignBlockActor::setMessageForServerScripingOnly(enum SignTextSide,class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> >,class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> >)" (__imp_?setMessageForServerScripingOnly@SignBlockActor@@QEAAXW4SignTextSide@@V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@1@Z)
ShopService.cpp.obj : error LNK2019: 无法解析的外部符号 "__declspec(dllimport) public: enum SignTextSide __cdecl SignBlockActor::getSideFacingPlayer(class Player const &)const " (__imp_?getSideFacingPlayer@SignBlockActor@@QEBA?AW4SignTextSide@@AEBVPlayer@@@Z)
ContentProtectionHooks.cpp.obj : error LNK2019: 无法解析的外部符号 "__declspec(dllimport) public: bool __cdecl DropperBlockActor::pushOutItems(class BlockSource &)" (__imp_?pushOutItems@DropperBlockActor@@QEAA_NAEAVBlockSource@@@Z)
ContentProtectionHooks.cpp.obj : error LNK2019: 无法解析的外部符号 "__declspec(dllimport) public: void __cdecl TransportItemsGoal::_executeEvent(enum TransportItemsGoal::Event)const " (__imp_?_executeEvent@TransportItemsGoal@@QEBAXW4Event@1@@Z)
build\windows\x64\release\Shop.dll : fatal error LNK1120: 4 个无法解析的外部命令
```

Tested against BDS 1.26.60.6 (server) with LeviLamina 26.60.0 (`bedrockdata v26.60.6-server.5`).
````

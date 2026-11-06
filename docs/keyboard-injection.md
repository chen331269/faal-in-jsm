# 键盘注入：SendInput 与虚拟 HID 的区别

JSM 把按键送进系统有两条路。它们对游戏来说不是一回事。

## 两条通道

| | `SendInput` | 虚拟 HID（FakerInput） |
|---|---|---|
| 层级 | 用户态 API | 内核级虚拟键盘设备 |
| 系统看来 | 一个"合成的输入事件" | 一个真实的 USB 键盘 |
| 游戏/反作弊能区分吗 | 部分可以 | 基本不能 |
| 覆盖范围 | 任意虚拟键码 | 仅 HID 键盘页（0x07）里的用法码 |
| 同时按键 | 无限制 | 6 个普通键 + 8 个修饰键 |
| 需要驱动 | 否 | 是（FakerInput 驱动 + `FakerInputDll.dll`） |

## 代码里怎么分流

所有键盘输出都经过唯一的一个函数 `pressKey()`：

```cpp
int pressKey(KeyCode vkKey, bool pressed)
{
    if (vkKey.code <= V_WHEEL_DOWN)          // 鼠标键另有路径
        return pressMouse(vkKey, pressed);

    static FakerInputBackend &faker = getFakerBackend();
    if (faker.isKeyboardAvailable() && vkToHidUsage(vkKey.code) != 0)
    {
        faker.pressKey(vkKey.code, pressed);  // 虚拟 HID
        return 1;
    }
    return SendInput(...);                    // 兜底
}
```

上游（`DigitalButton` → `pressKey`）没有任何改动，所以配置写法、组合键、和弦都和原来一致。

**两条判据**决定走哪条路：

1. 虚拟 HID 后端是否可用（驱动装了、DLL 加载成功）
2. 这个键码有没有对应的 HID 用法（`vkToHidUsage` 返回 0 表示没有）

任一不满足就回退 `SendInput`——**功能不会丢，只是这一下又被看见了**。

## 哪些键走哪条

走虚拟 HID：

- 字母 A–Z、数字 0–9
- F1–F24
- 方向键、Home / End / PageUp / PageDown / Insert / Delete
- 空格、回车、Esc、Tab、Backspace、CapsLock
- 标点 `- = [ ] \ ; ' ` , . /`
- 左右 Ctrl / Shift / Alt / Win
- 小键盘 0–9 与 `/ * - + .`
- PrintScreen、ScrollLock、Pause

仍走 `SendInput`：

- 媒体键（音量、静音、播放/暂停、上下曲）——它们属于 HID 的 Consumer Page，普通键盘报文放不下
- `VK_APPS`（菜单键）、`OEM_102`（部分欧洲键盘的 `<>` 键）
- 极少数特殊键（输入法相关）

## 6 键上限

HID 键盘报文只有 6 个普通按键位。同时按超过 6 个，第 7 个会被忽略；修饰键单独用位掩码表示，不占这 6 个位置。

正常映射碰不到，但"一键连招"这类宏可能会。需要突破时得换 `fakerinput_update_keyboard_enhanced`，那是另一套报文语义。

## 什么时候会退回 SendInput

| 情况 | 结果 |
|---|---|
| 驱动没装 / DLL 加载失败 | 全部按键走 `SendInput`（和改造前一致） |
| DLL 可用但驱动拒绝连接 | 同上 |
| 单个键没有 HID 映射 | 只有这个键走 `SendInput` |

启动时控制台会打印一行状态：

```
FakerInput backend initialized successfully! keyboard=yes
```

`keyboard=no` 表示键盘通道没接上，此时行为等同旧版。

## 为什么改这个

在多款带反作弊的射击游戏里，`SendInput` 产生的键盘事件会被识别并触发额外的完整性扫描，表现为**一按键盘就掉帧**。鼠标早就走了虚拟 HID，键盘一直是缺口。

改完之后两条输入都从同一个虚拟 HID 设备出去，反作弊看到的是"玩家在用键盘和鼠标"，而不是"有人在用 API 注入"。

## 已知限制

- **没有线程锁**：键盘状态（修饰位 + 6 个键位）是普通变量。目前只从输入回调线程写，若以后有控制台命令也按键，需要加锁。
- **媒体键未接入**：需要 Consumer Page 报文，尚未实现。

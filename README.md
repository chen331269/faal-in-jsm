# JSM × FAAL — 把防抖滤波器接到陀螺仪瞄准的两条路径上

本仓库是 [JoyShockMapper](https://github.com/Electronicks/JoyShockMapper) 的一个分支，
把 [FAAL Filter](https://github.com/chen331269/FAAL-Filter) 接进了它的两条陀螺仪输出路径，
并补上了逐帧记录与离线分析工具。

> **FAAL 算法本身不在这里。** 设计、参数语义、滤波器核心都在
> [chen331269/FAAL-Filter](https://github.com/chen331269/FAAL-Filter)。
> 本仓库只负责"把它放在哪、怎么和宿主程序对接"。

---

## 1. 两条输出路径

```
A 鼠标模式：陀螺仪 → 标定 → 空间转换 → FAAL → 灵敏度/曲线 → 鼠标
B PS_MOTION：陀螺仪 → 标定 → [玩家空间映射 → FAAL → 折回3D] → 虚拟手柄 → 游戏内部转换 → 瞄准
```

**A** 拿到的已经是"横向/纵向"两个意图量，直接二维滤波即可。

**B** 只能站在游戏的空间转换**之前**（游戏自己做转换），所以要先把三维角速度映射成二维意图，
滤波后再折回三维。折回之所以精确，是因为该映射对 `(Y,Z)` 与 `X` 都是**齐次**的：

$$T(s\cdot V) = s\cdot T(V)$$

在三维上乘一个标量，游戏内部的二维输出会精确乘同一个标量。实测齐次性残差 ≤ 0.02。

细节见 [`docs/player-space.md`](docs/player-space.md) 与 [`docs/branches.md`](docs/branches.md)。

---

## 2. FakerInput 是什么，为什么需要它

**FakerInput** 是一个 Windows 下的**虚拟 HID 驱动 + 用户态 DLL**
（上游项目：Ryochan7/FakerInput）。它注册一个虚拟的键盘/鼠标设备，
应用通过 `FakerInputDll.dll` 导出的接口把输入事件写进去，系统会把它当成真实硬件设备处理。

JSM 用它而不是普通的 `SendInput`，原因是**注入层级不同**：

| 方式 | 注入层级 | 游戏能分辨吗 |
|---|---|---|
| `SendInput` | 用户态 API，注入到会话的输入队列 | 部分游戏/反作弊可以识别为合成事件 |
| `FakerInput` | 内核态虚拟 HID 设备 | 对游戏而言与物理设备无差别 |

所以本仓库运行时需要 **`FakerInputDll.dll` 与驱动同时就位**：

1. 安装 FakerInput 驱动（需要管理员权限，会创建虚拟 HID 设备）；
2. 把 `FakerInputDll.dll` 放在 `JoyShockMapper.exe` 同目录；
3. 程序启动时会 `LoadLibraryW("FakerInputDll.dll")` 并调用
   `fakerinput_alloc / connect / release / disconnect`（见 `src/win32/FakerInputBackend.cpp`）。
   加载失败会回退到系统默认输入方式，并打印 `FakerInput: LoadLibrary failed`。

**注意**：这属于内核级输入注入，部分反作弊系统会将其视为可疑行为。
只在允许虚拟输入的游戏中使用，风险自负。

---

## 3. 本仓库相对上游 JSM 的改动

| 文件 | 改动 |
|---|---|
| `include/faal_filter.h` / `src/faal_filter.cpp` | FAAL 接入 + 逐帧记录 + 线程安全的延迟重置 |
| `src/main.cpp` | 12 个 FAAL 设置的创建与命令注册、两条路径的调用点、PS_MOTION 预处理、三条命令 |
| `src/SettingsManager.cpp` | 移除 FAAL 全局对象与静态注册器（改为堆分配，与其它设置一致） |
| `include/FAALSettings.h` | 清空为说明性文件 |

完整 diff：[`patch/changes.diff`](patch/changes.diff)。核心文件 `SettingsManager.h`、
`JSMVariable.hpp` 一行未动。

修掉的问题：退出时堆损坏、`ResetFaalFilter()` 与输入线程竞争、记录功能的竞争、
回调内异常导致死锁、PS_MOTION 转发未标定陀螺仪。

---

## 4. 命令

| 命令 | 作用 |
|---|---|
| `FAAL_TRACE_START` / `FAAL_TRACE_STOP` | 逐帧记录开始／停止（写出 `faal_trace.csv`） |
| `FAAL_PSMOTION` | PS_MOTION 预处理开关，用于 A/B 盲测手感 |

记录内容（每帧 21 列）：滤波前二维值、当帧生效的 4 个参数、**空间转换前的原始三维陀螺仪**、
**重力向量**、**四元数**、**实际输出的向量**。有了这些，同一段动作可以离线重放到任意陀螺仪空间。

---

## 5. 实测数据

| 场景 | 行程保留 | 被抑制 |
|---|---|---|
| 慢速跟靶 | 95.7% | 4.3% |
| 反复甩枪 | 85.6% | 14.4% |
| 抖动 + 画圈 | 57.9% | 42.1% |

同一速度档（5–20 deg/s）：跟靶压 4.6%、抖动压 51.5%——**差 11 倍**。
它区分的不是速度，是方向反复的剧烈程度。

PS_MOTION 预处理（实测，含直通对照组）：行程保留 84.7%，方向偏折中位 1.42°。

> 前三行为离线重放推导值，最后两行为直接由记录的输出列测得。来源与前提见 `docs/`。

---

## 6. 已知边界

1. **只对玩家空间成立**——换成世界空间映射会破坏齐次性，折回三维出现预料之外的旋转；
2. 世界空间游戏会偏折（二维方向差 p50 3.7° / p99 20.8°）；
3. 抑制能否保持取决于旋转轴与重力的关系：绕重力轴保留，垂直重力轴被 accel 部分撤销；
4. roll 与 yaw 共用水平面通道，没有独立增益；
5. 玩家空间垂直轴无互补滤波，比水平更吃手抖。

完整说明见 [`docs/player-space.md`](docs/player-space.md)。

---

## 7. 构建

上游 JSM 的构建方式不变（CMake + SDL3 / ViGEmClient / magic_enum / pocket_fsm /
GamepadMotionHelpers）。本仓库额外需要 `FakerInputDll.dll` 放在可执行文件旁。

---

## 8. 许可

JSM 部分遵循其原始许可；FAAL 算法遵循 [FAAL-Filter](https://github.com/chen331269/FAAL-Filter)
的 MIT 许可。

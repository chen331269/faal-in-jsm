# JSM Studio 0.2.4 · FAAL 相关修复与记录功能

这份包是 2026-11-01 对 `JSM_Studio-0.2.4` 源码所做全部改动的存档，供日后查阅、复用或学习。

**原则：只修 FAAL 自己引入的问题。** 属于上游 JSM 原有的设计（和弦设置的并发访问等）
一律不动，保持原样。

- `changes.diff` —— 相对原版源码的完整改动（unified diff，可直接读）
- `files/` —— 改动后的文件（保持原目录结构，可直接覆盖）
- `docs/audit-threading.md` —— 线程 / 性能 / 运行时配置审计
- `docs/FAAL-review.md` —— 基于三段真实手柄数据的滤波器行为分析
- `tools/faal_replay.cpp` —— 离线回放工具

编译环境：MSVC 19.51 / C++17 / CMake 4.3 / Ninja，x64 Release。

---

## 改动范围

| 文件 | 性质 | 改了什么 |
|---|---|---|
| `include/faal_filter.h` | FAAL | 新增记录接口声明 |
| `src/faal_filter.cpp` | FAAL | 延迟重置、记录缓冲、原子标志 |
| `src/main.cpp` | 接线 | 记录调用、两条命令、异常兜底、12 个设置改为在此创建并注册 |
| `src/SettingsManager.cpp` | 清理 | **删除** FAAL 全局对象与静态注册器；其余与原版逐字相同 |
| `include/FAALSettings.h` | 墓碑 | 原内容清空，只留一段说明指向 `initJsmSettings()` |
| `include/SettingsManager.h` | — | **未改动** |
| `include/JSMVariable.hpp` | — | **未改动** |

---

## 一、修复：退出时堆损坏

**现象**：正常退出时进程可能以 `0xC0000374`（`STATUS_HEAP_CORRUPTION`）结束。

**根因**：`SettingsManager` 的容器是 `unordered_map<SettingID, shared_ptr<JSMVariableBase>>`，
`add(SettingID, JSMVariableBase*)` 会把裸指针直接交给 `shared_ptr`，即**取得所有权**。
工程里其它 124 个设置都是 `new` 出来的，没问题；但 12 个 FAAL 设置原本是文件作用域的全局对象，
把**静态存储地址**交给 `shared_ptr` 后，静态析构时会对它执行 `operator delete`——未定义行为。

**复现**（10 行同构代码，同样模式）：

```cpp
struct B { virtual ~B() {} };
static B g;
int main() { std::shared_ptr<B> sp(&g); }   // 退出时 rc = 0xC0000374
```

**改法**：把这 12 个设置搬进 `initJsmSettings()`，和其它 124 个设置完全同样的写法——

```cpp
auto faalHystRiseTime = new JSMSetting<float>(SettingID::FAAL_HYST_RISE_TIME, 0.0029f);
SettingsManager::add(faalHystRiseTime);
commandRegistry->add((new JSMAssignment<float>("FAAL_HYST_RISE_TIME", *faalHystRiseTime))
    ->setHelp("抑制快进时间（秒）。抑制从 0 到满的基准时长。"));
```

同时删掉 `SettingsManager.cpp` 里的全局对象、静态注册器和那一行 `#include "FAALSettings.h"`。
`initJsmSettings()` 在 `connectDevices()` 之前执行，所以设置在手柄线程启动前就已经注册完毕。

**两个附带收益**：

1. **不再有静态注册**——原来 FAAL 设置是在 `main()` 之前由静态初始化器注册的，
   依赖静态初始化顺序；现在和其它设置一样在 `main()` 里创建。
2. **核心文件零改动**——上一版曾想加 `addNonOwning()`（三行消除 UB），但那会给“设置归
   SettingsManager 独占”这条原版规矩开例外，后来者会看到两套所有权写法。现在只有一套。

**关于 `FAALSettings.h` / `FAALAssignments.h`**：两者都已无引用，但**没有删除**——
`CMakeLists.txt` 仍把它们列在 `target_sources` 里，删文件必须同时改 CMake，而改 CMake 会触发
重新 configure，本机当前离线、CPM 拉不到依赖，会把可用的构建树弄坏。收益（少两个头文件）
远小于风险，所以 `FAALSettings.h` 改成一段说明性的“墓碑”，`FAALAssignments.h` 原样保留（本就无人使用）。

---

## 二、未修复：和弦设置的并发访问（上游设计，有意保留）

**问题**：`ChordedVariable::_chordedVariables` 是 `std::map`。命令线程解析
`ZL,某设置 = 值` 时会往里面**插入**，输入轮询线程每帧通过 `JoyShock::getSetting()`
→ `chordedValue()` 在同一个 map 上**查找**。标准容器不支持并发读写。

**为什么不动**：这不是 FAAL 引入的——任何和弦设置（`ZL,GYRO_SENS`、`ZL,MIN_GYRO_THRESHOLD`…）
都会走同一条路径，启动顺序（先连手柄、后解析配置）也是原版就有的。
本次改动的原则是“只修 FAAL 引入的问题”，所以**保持原样**。

**当前的实际风险**（如实记录）：

- 插入只改指针、不搬动节点，通常只是某几帧读到旧值；
- 如果命令执行的是“删除和弦”（`X,设置 = NONE`），会释放节点，此时并发查找可能访问已释放内存；
- 概率很低，典型表现是“几十次启动偶尔崩一次”。

如果哪天要修，两种做法：给和弦表加锁（上一版试过，能编过但要动核心模板），
或者把 `connectDevices()` 挪到配置解析之后（要求 AutoConnect 线程不会抢先连设备，未验证）。

---

## 三、修复：`ResetFaalFilter()` 与滤波线程竞争

**根因**：`ResetFaalFilter()` 在命令线程被调用，会直接清零正在被轮询线程读写的 `AxisState`。

**改法**：改为“请求重置”，由下一次 `faal_filter()` 在轮询线程上执行。

```cpp
static std::atomic<bool> s_faal_resetRequested{ false };

void ResetFaalFilter() { s_faal_resetRequested.store(true, std::memory_order_release); }

// faal_filter() 开头
if (s_faal_firstFrame || s_faal_resetRequested.exchange(false)) { /* init states */ }
```

重置最多延迟一帧（≤1 ms），语义等价；`exchange` 保证只执行一次。

---

## 四、修复：逐帧记录的线程安全

**根因**：`FAAL_TRACE_START/STOP` 在命令线程改标志和计数，`FaalTraceRecord` 在轮询线程写缓冲区。

**改法**：标志与计数改成 `std::atomic`；缓冲区**只分配、不释放**（不存在 use-after-free）；
`Stop` 先关标志（release）再读计数；`Record` 先查标志、写数据、最后 release 递增计数。

**代价**：每帧一次 relaxed load，未记录时等于一次可预测分支。

---

## 五、修复：回调里抛异常会导致死锁 + 进程终止

**根因**：`joyShockPollCallback` 一进来就 `callback_lock.lock()`，退出才 `unlock()`。
中间的 `jc->getSetting()` 在设置缺失时会抛 `out_of_range`，异常逃出回调后锁不释放，
并穿透到 SDL 线程的 C 栈 → `std::terminate`。

**改法**：把 12 次 FAAL 设置读取包进 `try/catch`，失败就沿用上一帧参数：

```cpp
FaalParams p;
try { /* 12 × getSetting */ }
catch (...) { p = GetFaalParams(); }
```

**局限**：只兜住 FAAL 这一段（目前唯一可能抛的地方），不是全函数异常安全。
彻底解决需要 RAII 锁 + 全函数 try，改动面太大，有意保留。

---

## 六、新增：PS_MOTION 预处理（原生游戏）

**背景**：`GYRO_OUTPUT = PS_MOTION` 时 JSM 把物理陀螺仪转发给虚拟手柄，游戏自己做空间转换。
FAAL 只能站在**游戏转换的上游**，而它需要的是"2D 意图"。

**做法**（`main.cpp` 的 `isDirectOutput` 分支）：

1. 用 JSM 自己的重力，把标定后的陀螺仪按 **PLAYER_TURN** 映射成两个标量
   （yaw = 重力参考的水平分量，pitch = 手柄本地 X）；
2. 在这两个标量上跑 FAAL；
3. 取比例 `s = filtered / original`，按 `X *= s_pitch`、`(Y,Z) *= s_yaw` 折回 3D；
4. 转发的是**标定后**的陀螺仪（原来发的是未标定的 `imu.gyro*`，属于既有缺陷）。

**为什么在 3D 里乘一个标量就能精确传递到游戏的 2D**：两个方向的映射都是**齐次**的
（输入乘 s，输出乘 s）。实测验证：对真实录制按 0.5 缩放 (Y,Z)、0.3 缩放 X，
重算后的相对误差为 **0.00e+00 / 0.00e+00**。

**代价**：两轴增益不同会改变 3D 角速度向量的方向——但在 2D 输出上是精确的，
方向只在"游戏用融合姿态而非原始角速度瞄准"时才有影响。

**A/B 开关**：`FAAL_PSMOTION` 命令在"预处理"与"标定后直通"之间切换，用于盲测手感。

---

## 七、新增：FAAL 逐帧记录 + 离线回放

**目的**：参数调优不能只靠感觉。记录滤波器的**输入**和当帧生效的参数，事后用同一份
`faal_filter.cpp` 精确重放。

```cpp
struct FaalTraceSample
{
    // 前 8 列：FAAL 的输入 + 当帧生效的参数（v1 就有的，顺序不变）
    float dt, gx, gy, gz;
    float signChangeThr, freezeDurationSec, enableVectorReversal, smoothTimeSec;
    // v2 追加：原始 3D 信号与它所依据的参考系
    float rawX, rawY, rawZ;       // 标定后的陀螺仪，空间转换之前
    float gravX, gravY, gravZ;    // 同一帧的重力估计
    float quatW, quatX, quatY, quatZ;
};                                  // 68 字节
```

预分配 15 万帧（≈10.2 MB，1000 Hz 下约 150 秒），只在开启记录时分配；
实时路径不发盘、不加锁、不分配；`FAAL_TRACE_STOP` 时一次性写 CSV 并清零。

**为什么追加在最后**：前 8 列的语义和顺序保持不变，所以 v1 的回放工具
（只读前 8 列）能直接读 v2 的 CSV，不需要改一行。

**为什么记原始 3D 而不是只记 2D**：滤波器对输入是确定性纯函数，记下 `(dt, gx, gy)`
就能重建内部状态；再记下 `raw*`、`grav*`、`quat*`，就能在离线状态下**把同一段动作重新投影到
任意陀螺仪空间**（LOCAL / PLAYER_TURN / WORLD_TURN …），比较不同空间的抑制行为，
而不需要让玩家反复重录。

**回放工具** `tools/faal_replay.cpp`：行程保留率、分速度档抑制率、逐通道消融、
`--whatif` 参数对比、`--dump` 波形导出、`output fingerprint` 输出指纹。

---

## 七、验证：改动没有改变行为

同一份真实录制（反复甩枪，62,488 帧）分别跑原版 / 改动后的 `faal_filter.cpp`：

| 数据 | 原版指纹 | 改动后指纹 |
|---|---|---|
| 真实甩枪录制 | `6fffcbbbf079b0ec` | `6fffcbbbf079b0ec` |
| 合成自测轨迹 | `6d889d7169acefdf` | `6d889d7169acefdf` |

指纹是逐帧输出浮点数的 FNV 哈希，相同即输出序列逐位一致——**手感不变**。

编译验证：删除全部目标文件后全量重编，`[33/33]` 链接，0 error。

---

## 八、有意保持原状的

| 项 | 原因 |
|---|---|
| 和弦设置的 map 并发访问 | 上游设计，非 FAAL 引入 |
| `main.cpp` 硬编码的 `dropSensitivity/dropCurve/peakMinTrigger/suppressThr/freezeOutMul` | 改成可配置会改变现有手感 |
| 矢量检测比较时 EWMA 已被当前样本更新 | 同上；修了会改变抖动响应 |
| `AxisState` 未使用字段、未使用的 `FAALAssignments.h` | 纯清理，无功能影响 |
| 设置值（`float`）层面的并发读写 | x86 上单字读写不会撕裂 |

---

## 九、如何重新构建

构建树 `out/build/x64` 里的路径指向 `X:` 盘（用 `subst X: <工作区>` 建立），
以便复用已有的 SDL3 / ViGEm 产物。重新构建前先确认 `subst X:` 仍有效，然后：

```
cmake --build out\build\x64 --target JoyShockMapper
```

若要让构建树回到普通盘符路径，删除 `out/build/x64` 重新 configure（需联网拉依赖）。

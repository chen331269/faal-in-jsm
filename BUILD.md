# 构建说明

`dist/` 里的二进制由以下步骤产生，可自行复现。

## 1. 取源码

```bash
git clone -b jsm-studio https://github.com/hotuns/JoyShockMapper.git
cd JoyShockMapper
git apply ../patch/changes.diff      # 本仓库的补丁
```

补丁基于 `jsm-studio` 分支的 `50cd082`（2026-11）验证可用；
它新增 `FAALSettings.h` / `faal_filter.h` / `faal_filter.cpp`，修改 `main.cpp` 与 `SettingsManager.cpp`。

## 2. 编译

上游用 CMake + CPM 拉取依赖，需要联网（首次）。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target JoyShockMapper
```

产物在 `build/JoyShockMapper/JoyShockMapper.exe`，构建后会自动把 `SDL3.dll` 复制到同目录。

本次发布所用的工具链：

| 项 | 版本 |
|---|---|
| MSVC | 14.51（Visual Studio 18） |
| CMake | 4.3 |
| Ninja | 随 VS 附带 |
| 平台 | Windows x64, Release |

## 3. FakerInputDll.dll

上游 [Ryochan7/FakerInput](https://github.com/Ryochan7/FakerInput) 是归档仓库，**没有提供预编译产物**，
需要自己用它的解决方案编译。注意：

- `FakerInputDll.dll` 必须与已安装的 **FakerInput 驱动**版本匹配；
- 驱动需要管理员权限安装，创建虚拟 HID 设备；
- 驱动与 DLL 都来自同一个上游仓库。

## 4. 校验

`dist/SHA256SUMS` 是本仓库发布文件的哈希。若你自己编译，结果通常不会逐字节相同
（时间戳、链接顺序），但可以用它确认下载到的文件未被替换。

## 5. 关于误报

本程序会：

- 创建虚拟游戏手柄（ViGEm）；
- 通过 FakerInput 向系统注入键鼠事件（内核级虚拟 HID）。

这两件事**本身就是杀软和反作弊重点关注的行为**，被误报属于预期，不代表文件被篡改。
验证方式只有三种：比对哈希、自己编译、或在隔离环境里运行观察。

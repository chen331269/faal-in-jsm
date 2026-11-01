# 可运行版本

这是把 patch 应用到 JSM Studio 所用的 JSM 分支后编译出来的可执行文件（Windows x64, Release）。

## 直接运行

1. 安装 **FakerInput 驱动**（需要管理员权限，会创建虚拟 HID 设备）：
   https://github.com/Ryochan7/FakerInput
2. 本目录下运行：

   JoyShockMapper.exe <你的配置文件.txt>

   没有配置文件时按提示操作；JSM 的配置语法见上游文档。

## 文件

| 文件 | 说明 |
|---|---|
| JoyShockMapper.exe | 主程序（含 FAAL 与 PS_MOTION 预处理） |
| SDL3.dll | 手柄输入后端 |
| FakerInputDll.dll | 虚拟输入后端（需配合驱动使用） |

## 注意

- 未签名。首次运行 Windows SmartScreen 会拦截，需手动放行。
- 不含 FakerInput **驱动**，只有用户态 DLL；驱动必须单独安装。
- 二进制与 `patch/changes.diff` 对应，可自行重新编译验证。

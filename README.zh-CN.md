# flutter_wwise

> **非官方**的 **Audiokinetic Wwise** × **Flutter** 集成 —— 覆盖 Windows / Linux / Android。
> 基于 `dart:ffi`，不经 Unity，不经 Unreal。

[English](README.md) · **简体中文**

---

## 它为什么存在

Audiokinetic 为 **Unity** 与 **Unreal** 提供了官方 Wwise 集成。
**Flutter 什么都没有**——一个口碑扎实的音频中间件，从 Flutter 应用里够不着。

这个包就是那缺失的一层：把 Wwise 声音引擎链进 Flutter 应用，
在上面暴露一套小巧、强类型的 Dart API。

```
Flutter (Dart)  ──dart:ffi──▶  C 垫片  ──▶  Wwise 声音引擎
```

**为什么要 C 垫片**：Wwise 的 API 活在 C++ 命名空间里
（`AK::SoundEngine::PostEvent`），而 `dart:ffi` 只能调用平坦的 C 符号。
垫片把名字修饰、命名空间、版本差异一次收在这边，
给 Dart 一层稳定、不会随 SDK 版本漂移的接口。

## 状态

**早期，pre-alpha，尚不可用于生产。**

| 部件 | 状态 |
| --- | --- |
| 仓库骨架、文档、CI | ✅ |
| C 垫片接口（已声明） | 🚧 进行中 |
| Dart FFI 绑定 | 🚧 进行中 |
| Windows 构建（CMake） | 🚧 进行中 |
| Linux 构建 | ⏳ 计划中 |
| Android 构建（NDK/CMake） | ⏳ 计划中 |
| 示例应用 | ⏳ 计划中 |

## 平台支持

| 平台 | 引擎核心 | 形态 |
| --- | --- | --- |
| **Windows** x64 / ARM64 | `AkSoundEngineDLL.dll` + `AkSoundEngine.lib` | 动态 + 静态 |
| **Linux** x64 / aarch64 | `libAkSoundEngine.a` | 静态 |
| **Android** arm64-v8a / armeabi-v7a / x86_64 | `libAkSoundEngine.a` | 静态 |

> ⚠️ **Wwise SDK 需您自备。** 本仓库**不含**任何 Wwise SDK 文件——
> 没有头文件、没有库、没有工具。

## 安装

尚未发布至 pub.dev。目前请从 Git 依赖：

```yaml
dependencies:
  flutter_wwise:
    git:
      url: https://github.com/Nesarf/flutter-wwise.git
      ref: main
```

## 计划中的 API

接口刻意保持窄小。核心稳下来之前，未列出的能力一律不在范围内。

```dart
import 'package:flutter_wwise/flutter_wwise.dart';

// 用你的 Init.bnk 初始化引擎
await Wwise.init(initBankPath: 'assets/wwise/Init.bnk', sampleRate: 48000);

// 加载 Wwise 授权工具产出的 SoundBank
await Wwise.loadBank('Main.bnk');

// Game Object 标识「是谁在发声」
final player = await Wwise.registerGameObject('player');

// 触发 Wwise 里定义的 Event
await Wwise.postEvent('Play_IceDrop', player);

// 实时驱动 Game Parameter —— 例如摇壶摇得多用力
await Wwise.setRtpc('ShakeIntensity', 0.82, player);

await Wwise.renderAudio();          // 每个音频回调 / 帧预算调用一次
await Wwise.unregisterGameObject(player);
await Wwise.term();
```

## 构建

把 `WWISE_SDK_ROOT` 指向你的 Wwise SDK 目录（内含
`include/`、`x64_vc170/`、`Linux_x64/`、`Android_arm64-v8a/` 等的那一层）：

```powershell
$env:WWISE_SDK_ROOT = "C:\Program Files (x86)\Audiokinetic\Wwise 2024.1.14.9084\SDK"
```

```bash
export WWISE_SDK_ROOT=/opt/wwise/2024.1.14.9084/SDK
```

随后构建示例应用：

```bash
cd example
flutter run -d windows
```

`src/cmake/` 里的构建胶水会按平台从 `WWISE_SDK_ROOT` 解析出正确的库并链入插件。

## 范围与非目标

**范围内**

- 初始化 / 关闭声音引擎
- 加载与卸载 SoundBank
- 注册与注销 Game Object
- 触发 Event
- 设置 Game Parameter（RTPC）与 Switch / State
- 由宿主驱动 `RenderAudio`
- 强类型的 Dart 错误，而非裸整数返回码

**暂不在范围内**

- Wwise 授权工具本身（那是 `WwiseConsole` 的事）
- 超出引擎默认行为的空间音频
- 打包或下载 Wwise SDK 内容

## 关于 Wwise 授权

本项目**独立、非官方**，与 Audiokinetic Inc. 无隶属、背书或赞助关系。

Wwise 是商业软件。您需要自己的授权，并自行遵守其条款——
**包括 Wwise 运行库随应用分发时的各项条件。**
以 Audiokinetic 官方授权页面为准；本文件不构成法律意见。

## 相关项目

- [`pywwise`](https://pypi.org/project/pywwise/) — Python 版 WAAPI 封装，
  驱动的是**授权工具**而非运行时，与本包互补
- [`ww2ogg`](https://github.com/hcs64/ww2ogg) — 把 Wwise 的 RIFF/RIFX Vorbis
  转成标准 Ogg Vorbis，用于从 SoundBank 中提取音频
- Wwise SDK 文档，随您的安装包位于 `SDK/Help/`

## 贡献

见 [CONTRIBUTING.md](CONTRIBUTING.md)。尤其欢迎**平台构建报告**——
CI 覆盖不到 Wwise 版本 × 平台的每一种组合。

## 许可

MIT —— 见 [LICENSE](LICENSE)。
Wwise SDK 本身**不在**本许可范围内，且**不**随本仓库分发。

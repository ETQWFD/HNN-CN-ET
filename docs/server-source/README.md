# HNN CN ETC Server v1.3.4 C 服务端源码存档

Copyright (c) 2026 ETC · MIT License

本目录存档 HNN CN ETC Server v1.3.4 版本的跨平台纯 C 服务端全部源码与编译脚本。
服务端为单文件主体（`ai.c`）+ Windows 平台适配薄壳，不依赖第三方运行库，可直接在
Termux / Linux / Windows 下编译为可执行文件或 DLL。

## 文件用途

| 文件 | 用途 |
| --- | --- |
| `ai.c` | 服务端主体：HTTP/OpenAI 兼容接口、模型名四级兼容解析（完整 ID / 市场名 / 目录名 / 归一化名 / llm.mnn 权重文件名）、鉴权开关、etc- 混合密钥、0.0.0.0 局域网直连与 CORS、`GET /v1/models`、模型目录缓存、动态输出缓冲、会话复用、端口持久化 |
| `launcher_win.c` | Windows 控制台入口薄壳，负责拉起 `hnn_core.dll` 并转发控制台 IO |
| `win_dirent.h` | Windows 下 POSIX `dirent.h` 缺失时的目录枚举兼容头文件 |
| `versioninfo_dll.rc` | Windows DLL/EXE 资源脚本：版权署名 ETC、版本号 1.3.4、产品信息 |
| `build.sh` | 一键编译脚本，覆盖：Termux arm64 (`aarch64-linux-android`)、Termux armv7 (`arm-linux-androideabi`)、Linux amd64 (`x86_64-linux-gnu`)、Windows DLL/EXE (`x86_64-w64-mingw32`) |

## 编译

```sh
# Termux (在 Termux 内)
bash build.sh termux-arm64      # 或 termux-arm
# Linux
bash build.sh linux-amd64
# Windows (在 MSYS2 / 交叉编译环境)
bash build.sh windows
```

产物：

- Termux / Linux：单文件可执行 `ai`
- Windows：`hnn_core.dll` + `ai.exe`

## 许可

MIT License，详见仓库根目录 `LICENSE`。

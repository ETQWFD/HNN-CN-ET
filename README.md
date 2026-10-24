<p align="center">
  <img src="docs/assets/icons/app-icon-192.png" width="120" alt="HNN CN ET" />
</p>

<h1 align="center">HNN CN ET</h1>

<p align="center">
  <b>在安卓手机上本地跑大模型聊天</b><br/>
  不用联网、不用付费、聊天记录不上传 · 32 位老设备也能用
</p>

<p align="center">
  <a href="https://github.com/ETQWFD/HNN-CN-ET/releases/latest"><img alt="Release" src="https://img.shields.io/badge/下载-最新APK-00B38A" /></a>
  <a href="https://etqwfd.github.io/HNN-CN-ET/"><img alt="Website" src="https://img.shields.io/badge/官网-etqwfd.github.io/HNN--CN--ET-22D3EE" /></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/License-MIT-green" /></a>
</p>

---

## 这是什么

HNN CN ET 是基于 [longluo/MnnLlmChat](https://github.com/longluo/MnnLlmChat)（MIT，Copyright © 2025 Frank Luo）和 [alibaba/MNN](https://github.com/alibaba/MNN) 二次开发的安卓端侧大模型聊天应用。

- 官网：<https://etqwfd.github.io/HNN-CN-ET/>
- 下载统计：<https://etqwfd.github.io/HNN-CN-ET/dan.html>
- Release：<https://github.com/ETQWFD/HNN-CN-ET/releases>
- 联系作者：<mailto:2416444244@qq.com> · QQ 群：917902000

## 功能

- **端侧离线聊天**：模型在本地运行，聊天记录只存在手机里，不上传任何服务器。
- **真实 32 位支持**：APK 同时包含 `armeabi-v7a`（32 位）与 `arm64-v8a`（64 位）原生库，核心库（libMNN / libmnnllmapp / libmnn_tts）与语音识别库（libsherpa-mnn-jni，ASR）双架构齐备。
- **应用内自动更新**：启动时检查 GitHub Releases，支持断点续传、失败重试、进度与速度显示。
- **日志页**：记录启动、下载、加载、推理和错误，可一键复制，方便排查问题。
- **模型下载器**：内置模型下载，显示进度/速度，支持断点续传与文件校验。
- **公告系统**：官方公告发布后，App 启动时弹窗展示一次。
- **AI 模型审核开关**：默认关闭，直接使用所选模型原生对话；开启后可使用安全审核（Guard）模型。
- **联系作者**：设置页「联系作者」一键发邮件，也可加入 QQ 群。

## 更新日志

### v1.1.6 (2026-10-24)
- API: CORS enabled by default (any origin) — website/网页/LAN clients can call the local API without browser CORS blocks; every download failure is now surfaced (no more silent clicks).
- Windows installer fully fixed: NSIS now packs the complete runtime explicitly (GCC 8.1 runtime + Qt DLLs + plugins, 14 items) — no more missing libgcc_s_seh-1.dll after install.
### v1.1.5 (2026-10-24)
- Import model: pick a folder/file via SAF; it auto-detects model basics (llm.mnn/model.mnn/gguf/safetensors/bin), imports into /storage/emulated/0/hnn, or imports nothing with a clear message when no model is found.
- Downloads fixed: already-downloaded models are skipped (real-file check); failure reasons now pop up (permission/404…); HuggingFace source routes via hf-mirror.com mirror.
- Migration: legacy models auto-migrate into /storage/emulated/0/hnn right after storage permission is granted; the folder is auto-created and used.
- Deletion is thorough: sweeps all naming leftovers and partial-transfer remnants.
- API reworked: auth disabled by default so LAN clients/website can call the local model directly (still bound 0.0.0.0).
- Windows v1.0.8: "cannot find entry point" fixed — rebuilt with Qt-official MinGW GCC 8.1 toolchain + matching GCC 8.1 runtime; verified with a 30s clean wine run.
### v1.1.4 (2026-10-23)
- Fix downloads: HuggingFace source now routes via hf-mirror.com (China mirror); download root /storage/emulated/0/hnn is created before transfers with clear errors.
- API server binds 0.0.0.0 (was 127.0.0.1) — LAN devices and the website can really call your local model. Legacy 127.0.0.1 config auto-migrated.
- Model market pins recommendations on top: image generation (SD1.5, Sana) + small thinking models (DeepSeek-R1-1.5B, Qwen3.5-0.8B, LFM2-350M, Qwen3-1.7B).
- Settings add "Direct Model Run" (one-tap real inference for any downloaded model) and "Show LAN API Address".
- Floating window drag throttled (16ms) to reduce jank on low-end devices.
- Windows v1.0.7: market adds DeepSeek-R1-1.5B and Qwen3.5-0.8B small models (verified modelscope direct links).
### v1.1.3 (2026-10-21)
- Model storage migrated to public directory /storage/emulated/0/hnn: at startup, downloaded models are auto-migrated from the app-private folder; download, scan, delete and cache all use the new directory.
- Before downloading, the app checks the "All files access" permission and guides you to grant it in one tap — no more silent failures.
- During migration both old and new directories are scanned, so models never disappear.
### v1.1.2 (2026-10-19)
- Fix "调用mcp在那个目录中创建一个vs.txt" stuck loading: broad create-file intent auto-detects the filename and calls the MCP file tool (server picker when several).
- Unnamed files ("文本文档") get an auto-generated default name.
- Filter blank streaming deltas; when the model produces no visible text, show a notice instead of spinning forever. API stream closes cleanly.
- Windows v1.0.6: broadened create-file intent + listing tools when only "调用mcp" is said.
### v1.1.1 (2026-10-19)
- Fix AI not calling MCP tools: broad intent recognition for "先调用一下mcp", "帮我用用mcp", etc. — no strict word order required.
- MCP calls no longer depend on the Agent mode toggle; any MCP intent is intercepted. Without a tool name, the app lists all available tools.
- Windows v1.0.5: broad MCP intent matching + isAgentCommand fix + empty tool name lists tools.
### v1.1.0 (2026-10-19)
- MCP config redesigned: inline expandable panel in Settings — toggle on, then add/test/delete servers right below, no more separate page.
- New MCP master switch: when off, the AI clearly tells you to enable it in Settings instead of silently failing.
- MCP server cards show token status (no token / token configured); additions take effect immediately.
- Agent mode and MCP are decoupled: opening apps uses accessibility, file/tool calls use MCP.
- Windows v1.0.4: MCP page gains an "Enable MCP" checkbox, matching the mobile behavior.
### v1.0.9 (2026-10-19)
- MCP tools are now really callable: the AI recognizes "call MCP / call tool / create file" instructions and executes them; when multiple servers exist, a chooser appears and you may pin one server for the whole session.
- Agent mode enhanced: "open Douyin/WeChat/QQ…" launches the installed app directly (built-in map of common apps); if the accessibility permission is missing, the app asks for it and then continues the pending task.
- A "Deep think" toggle now always sits above the input box: on, the model reasons deeply before answering (DeepSeek style); off, it answers directly.
- Settings → new "Appearance" section: theme mode (system/dark/light), theme color (blue/green/red/yellow/purple/cyan/custom), chat wallpaper (50% translucent frosted glass).
- Agent floating ball is now a draggable round gradient orb with a popup menu; benchmark can be stopped; all dialogs upgraded to Material 3 (Android 15) style.
- Windows v1.0.3: the built-in OpenAI-compatible API service is now callable from the LAN/browser (binds all interfaces, auto LAN address, CORS + OPTIONS preflight, full HTTP body buffering); API key format etc- + 62 digits.
### v1.0.8 (2026-10-19)
- Chat: Agent shows "Calling MCP tool…" when invoking MCP tools; MCP token is optional (leave blank for token-free servers).
- Fix: raw technical errors (DB allocation failure / benchmark string) no longer leak into chat — friendly message shown, details logged; DB writes serialized + background repair to stop CursorWindow -12 on low-memory devices; generation failure no longer stuck at "Thinking…".
### v1.0.7 (2026-10-18)
- MCP config supports an optional access token (Bearer); non-JSON responses (e.g. Unauthorized) now show a clear reason instead of a parse error.
### v1.0.6（2026-10-18）
- v1.0.6: Fix MCP connection (allow cleartext HTTP for LAN http://IP:port servers); Agent now asks for the accessibility permission and automatically continues the pending task after it is granted; real MCP tool calls via "call tool <name> k=v".
- MCP：测试显示行内状态与失败原因（超时/无法连接），新增读取/列出/删除/修改文件，支持直接调用 MCP 工具（如：调用工具 工具名 参数=值）。
- 聊天：发送消息后显示「正在思考…」加载提示，回复不再空白。
- 新增「高速直答」开关：AI 直接回答、不拒绝问题，并跳过深度思考，回复更快。
- Agent 工作模式执行更稳定，无障碍缺失时明确引导开启。

### v1.0.4（2026-10-18）
- 优化模型初始化与加载流程：进入聊天不再被全屏遮罩卡住，底部显示「正在初始化模型」进度提示，首次加载稍慢属正常。
- 修复聊天历史记录排序混乱的问题。
- 官网全面改版为明亮简洁风格，网站图标改用应用图标。

### v1.0.3（2026-10-18）
- 「API 服务」测试页改为 HNN CN ET 中文青绿版。
- 聊天新增「深度思考」开关：开 = 先深度分析再回答，关 = 直接原生回答。
- 设置新增「MCP 连接配置」：添加/删除/测试 MCP 服务器，AI 可通过 MCP 调用工具创建文件等。
- 设置新增「Agent 工作模式」：对话中可执行设备操作——打开应用、发消息、看视频、听音乐、创建文件、返回桌面；缺无障碍权限时自动引导开启。
- Agent 运行自动申请存储与悬浮窗权限；退出聊天界面后悬浮窗常驻，点击可回到应用。
- AI 生成的代码块带「复制代码」按钮，长按消息可选择复制代码或复制整条消息。
- 修复聊天记录最后一条消息（纯思考输出/重载后）消失的问题。
- AI 对话默认原生、无附加审核（「AI 模型审核」开关默认关闭）。

### v1.0.2（2026-10-18）
- 反馈改为直接发邮件给运营团队（2416444244@qq.com），不再跳转 GitHub Issue。
- Star 弹窗指向 HNN CN ET 仓库。
- 补齐标签翻译：安全审核 / 语音识别 / 语音合成 / 英语 / 中英双语。
- 安全审核模型单独归类，不再误当聊天模型。
- 新增「AI 模型审核」开关（默认关闭）。
- 新增公告系统。
- 生成卡住超时提示，不再一直转圈。
- 引擎显示：HNN 1.0.2 · 基于 MNN 3.6.0。
- 界面统一为青绿色品牌色。

### v1.0.1（2026-10-18）
- 补齐 32 位语音识别原生库。

### v1.0.0（2026-10-17）
- 首个 ET 版：补齐 32 位 armeabi-v7a 原生库；新增应用内自动更新、日志页、模型下载断点续传；品牌统一为 HNN CN ET。

## 构建

- 环境：Android Studio / JDK 17+ / NDK `27.2.12479018` / Gradle 8.9
- `local.properties` 指向 Android SDK 目录
- 签名（release）：环境变量 `KEYSTORE_FILE` / `KEYSTORE_PASSWORD` / `KEY_ALIAS` / `KEY_PASSWORD`
- 构建：`./gradlew :app:assembleStandardRelease`

## 许可

- 上游：longluo/MnnLlmChat，MIT，Copyright © 2025 Frank Luo
- 推理引擎：alibaba/MNN，版权归原作者
- 本次修改：Copyright © 2026 ET，详见 [NOTICE](NOTICE) 与 [LICENSE](LICENSE)

简体中文 [README_CN.md](README_CN.md) · 繁體中文 [README_TW.md](README_TW.md)

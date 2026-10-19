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

### v1.1.1（2026-10-19）
- 修复 AI 不调用 MCP 工具：宽松识别「先调用一下mcp」「帮我用用mcp」等各种说法，不再要求严格语序。
- MCP 调用不再依赖 Agent 模式开关：只要消息含 MCP 意图就拦截执行；未指定工具名时自动列出该服务器所有可用工具。
- Windows 电脑版 v1.0.5：MCP 意图 broad 识别 + isAgentCommand 匹配 + 空工具名列出可用工具。
### v1.1.0（2026-10-19）
- MCP 连接配置重做：设置页内联展开式，开关启用后直接在下方列表添加/测试/删除服务器，不再跳转独立页面。
- 新增 MCP 总开关：未开启时 AI 调用 MCP 明确提示去设置开启，避免「无法启动」困惑。
- MCP 服务器卡片显示令牌状态（无令牌/已配置令牌），添加成功即时生效。
- Agent 工作模式与 MCP 解耦：打开软件走无障碍，文件/工具走 MCP。
- Windows 电脑版 v1.0.4：MCP 页面新增「启用 MCP 功能」开关，与手机版行为一致。
### v1.0.9（2026-10-19）
- MCP 工具真实可调用：AI 识别「调用 MCP / 调用工具 / 创建文件」等指令并执行；多个服务器时弹出选择框，可勾选本次对话固定使用某个服务器。
- Agent 工作模式增强：「帮我打开XX」直接拉起已安装应用（抖音/微信/QQ 等常用应用内置映射）；无障碍权限未开启时主动弹窗申请，授权后继续完成任务。
- 聊天输入框上方新增「深度思考」按钮：开启后模型先深度推理再回答（DeepSeek 风格），关闭则直接回答。
- 设置新增「主题与外观」：主题模式（跟随系统/深色/明亮）、主题颜色（蓝/绿/红/黄/紫/青/自定义）、聊天背景图片（50% 透明度琉璃质感）。
- Agent 悬浮球改为圆形渐变 + 弹出菜单，可拖动；性能评测支持终止；所有弹窗升级为 Android 15（Material3）风格。
- Windows 电脑版 v1.0.3：内置 OpenAI 兼容 API 服务可在局域网/浏览器正式调用（监听全部网卡、自动生成地址、CORS 与 OPTIONS 预检、修复大请求体截断）；API Key 格式 etc- + 62 位数字。
### v1.0.8（2026-10-19）
- 聊天：Agent 调用 MCP 工具时显示「正在调用 MCP 工具…」，MCP 令牌可填可不填（无令牌工具留空）。
- 修复：聊天不再冒出原始技术报错（数据库分配失败 / 基准字符串错误），改为友好提示并写入日志；数据库读写串行化 + 后台修复，低内存设备不再报 CursorWindow 分配失败；生成失败不再卡在「正在思考…」。
### v1.0.7（2026-10-18）
- MCP 配置新增「访问令牌」（Bearer 认证），MT 等要求令牌的服务器填入令牌即可连接；服务器返回非 JSON（如 Unauthorized）时精准提示「未授权：服务器要求访问令牌」。
### v1.0.6（2026-10-18）
- v1.0.6：修复 MCP 服务器无法连接（放开局域网明文 HTTP）；Agent 无无障碍权限时自动申请，授权后自动继续执行未完成任务；「调用工具 名称 参数=值」可真实调用 MCP 工具。
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

English [README.md](README.md) · 繁體中文 [README_TW.md](README_TW.md)

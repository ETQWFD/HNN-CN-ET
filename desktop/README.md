# HNN CN ET — 电脑版（Windows）

基于 MNN 生态的端侧离线大模型聊天 **Windows 桌面版**，Win11 风格整洁界面，与手机版功能对齐：深度思考、高速直答、MCP 连接配置、Agent 工作模式（可操作电脑）、内置 API 服务（OpenAI 兼容）等。

- 框架：C++ / Qt 5.15（Widgets + Network）
- 协议：MIT · 版权：ET
- 架构：x86_64（Win10/11 64 位）

## 功能

| 功能 | 说明 |
| --- | --- |
| 主题 | 明亮 / 暗黑 / 跟随设备，主题颜色（蓝色/青色/绿色/紫色/红色/橙色），任意高清背景图 + 50% 透明度可调，一键重置关闭 |
| 模型 | 设置内下载 AI 模型到程序数据目录 `models/`（与手机版同源模型），本地目录内模型可直接选用 |
| Agent 工作模式 | AI 可打开记事本/计算器/画图/浏览器/资源管理器/控制面板/任务管理器/命令行、创建/读取/列出/删除/修改文件、回到桌面；操作前弹出确认窗口，可设密码校验；可限制只能在指定工作文件夹内操作 |
| MCP 连接配置 | 添加/测试/删除 MCP 服务器；对话输入「调用工具 工具名 参数=值」调用工具；文件类指令优先走 MCP |
| API 服务 | 设置内开启后显示连接地址与 Key（66 位，`etc-` 开头 + 62 位数字）与模型名称，OpenAI 兼容，可流式调用；可启用/关闭模型限制 |
| 历史记录 | 侧栏会话列表，可新建/切换/回到旧对话 |
| 自动更新 | 启动后静默检查 GitHub 最新 Release |

## 构建

依赖：CMake ≥ 3.16、MinGW-w64（x86_64）、Qt 5.15.2 win64_mingw81。

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=../toolchain-mingw.cmake -DQT_ROOT=/opt/qt/5.15.2/mingw81_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
# 部署 Qt 运行库后，用 NSIS 打包：
makensis packaging/installer.nsi
```

打包产物：`HNN-CN-ET-Setup-1.0.0.exe`（安装器，含卸载程序，版权 ET）。

## 数据目录

- 模型：`%APPDATA%\HNN CN ET\models\`
- 设置：`%APPDATA%\HNN CN ET\settings.json`
- 历史记录：`%APPDATA%\HNN CN ET\history.json`
- Agent 工作文件夹默认：`%USERPROFILE%\HNN-CN-ET`

## 免责声明

Agent 操作电脑属于高风险功能，请仅在信任的本地模型/服务下使用；对模型生成内容与自动操作后果概不负责。

# HNN CN ET Server（Go 版）

用 Go 重写的单文件 MNN 推理服务端，替代原 C 版 `ai.c`。
接口与目录约定保持不变，推理仍由 `llm_demo` 执行，因此模型目录、`system.ini`、`~/.hnn` 布局完全兼容。

## 相对 C 版修掉的问题

- **真流式**：不再整段等推理结束，`llm_demo` 每吐一行就下发一个 SSE 增量；首包先给 `role`，带 5 秒心跳，结尾 `finish_reason` + `usage` + `[DONE]`。
- **完整多轮**：按顺序拼接 system / user / assistant 全部消息，不再只取最后一条用户消息。
- **深度思考**：请求带 `"thinking": true`（也兼容 `enable_thinking`、`reasoning_effort`、`chat_template_kwargs.enable_thinking`）时：
  - 原生支持思考的模型（Qwen3 / R1 / GLM-4.5 等，或 config.json 里 `enable_thinking: true`）直接单遍输出，服务端把 `<think>…</think>` 拆成 `reasoning_content` 流；
  - 不支持的原生模型，先用一轮“只分析、不下结论”的推理产出思考过程，再跑一轮给出答案，两段都实时流式输出。
- **鉴权**：`Authorization: Bearer` 头大小写不敏感，另支持 `x-api-key` 与 `?key=`。
- **请求体**：按 Content-Length 完整读取，chunked 由 net/http 处理，上限 16 MB。
- **模型完整性自检**：缺 `config.json / llm_config.json / tokenizer.txt / llm.mnn / llm.mnn.weight / embeddings_bf16.bin` 时直接列出缺哪个，拒绝启动；模型目录嵌套一层会自动下沉。
- **不再依赖 hnn_core.dll**：Go 版只负责 HTTP 与进程调度，二进制约 6 MB，Windows 单 exe 即可运行（需 `llm_demo.exe`）。

## 运行

```bash
# 菜单模式（默认）
./ai-go-linux-amd64

# 直接启动
./ai-go-linux-amd64 --serve --model Qwen2.5-0.5B-Instruct-MNN --port 8080

# 临时关闭鉴权
./ai-go-linux-amd64 --serve --model <模型名> --no-auth
```

`llm_demo` 探测顺序：`~/.hnn/bin/llm_demo` → 程序同目录 `llm_demo` → 程序同目录 `bin/llm_demo`。
模型目录：`~/.hnn/models`。配置：程序同目录 `ai.conf`（port/auth/force/key/model/remote）。

## 接口

- `GET /health`、`GET /v1/models`、`GET /v1/models/<id>`
- `POST /v1/chat/completions`（stream / 非 stream）
- `POST /v1/completions`（stream / 非 stream）
- 远程模式：菜单 3 填上游地址后，整个服务变为反向代理。

深度思考示例：

```bash
curl -N http://127.0.0.1:8080/v1/chat/completions \
  -H "Authorization: Bearer $KEY" -H 'Content-Type: application/json' \
  -d '{"stream":true,"thinking":true,"messages":[{"role":"user","content":"9.11 和 9.9 哪个大？"}]}'
```

返回的 `delta.reasoning_content` 是思考过程，`delta.content` 是最终答案。

## 构建

```bash
./build.sh          # 生成 linux amd64/arm64/armv7 与 windows amd64
```

纯 Go、零第三方依赖，`CGO_ENABLED=0` 静态链接。

MIT · ETC

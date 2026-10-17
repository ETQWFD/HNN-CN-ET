# 后台统计页（dan.html）管理员说明

> 本文件仅给项目管理员 / 主代理看，不加入站点导航，也不要对外公开。

## 地址

- 站点域名（GitHub Pages 就绪后）：`https://etqwfd.github.io/HNN-CN-ET/dan.html`
- 本仓库路径：`docs/dan.html`

## 访问口令

- 口令（前端遮蔽用）：`et-admin-2026`
- 口令硬编码在 `docs/dan.html` 源码的 `<script>` 中（变量 `ADMIN_PASS`）。

## 重要安全说明（务必读）

GitHub Pages 是**纯静态托管，没有服务端**。本页面的口令只是一段前端 JS 判断：

1. 任何人只要能打开这个网址，就能在浏览器「查看源码」里直接搜到 `ADMIN_PASS = '...'`，口令形同虚设。
2. 页面展示的下载数据本身就来自 GitHub **公开** REST API：
   `https://api.github.com/repos/ETQWFD/HNN-CN-ET/releases`
   这些数据不需要任何鉴权就能被任何人拉到。
3. 因此「口令页」的真实作用只是：
   - 让普通访客在浏览器里**不直接看到**统计数字（防随手点开）；
   - 挡住简单的爬虫/搜索引擎（`noindex, nofollow` + 口令遮罩）。
4. 它**不是**真正的鉴权，也**不保护**数据。如果以后要真正私有的后台，必须：
   - 自建后端（自己服务器 / Cloudflare Workers / Vercel Serverless 等）；
   - 后端持有 GitHub token，向 GitHub API 取数；
   - 后端做真正的会话/账号鉴权，前端只跟自家后端通信。

## 展示内容

- 总下载量（所有 release、所有 asset 求和）
- 最新版本号
- Release 数量、Asset 数量
- 表格：每个 release、每个 asset 的下载次数与发布时间
- 空状态（尚未发布 release）、加载中、网络失败三种状态都已处理

## 首页是否要链接到本页

**不要**。按要求，首页和任何公开页面都不要放 `dan.html` 的链接，避免被搜索引擎收录。

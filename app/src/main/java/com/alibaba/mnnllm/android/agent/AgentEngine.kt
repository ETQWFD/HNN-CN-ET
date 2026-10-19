// HNN CN ET v1.0.3: Agent engine.
// When "Agent 工作模式" is enabled, the chat intercepts device-action requests:
//   - 打开 X / 启动 X / 开启 X        → launch an app by label
//   - 创建/新建/生成 文件 [path，内容：content] → create a file via MCP (or locally)
//   - 给 X 发消息[:内容] / 发消息给 X[:内容] → needs accessibility: open X, type, send
//   - 看视频/播放视频/听音乐/播放音乐 [X]    → needs accessibility: open the app
//   - 返回/回到桌面                     → needs accessibility: go back / home
// Requests that do not match are left to the normal chat model.
package com.alibaba.mnnllm.android.agent

import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.alibaba.mnnllm.android.R
import java.io.File

data class AgentResult(
    val reply: String,
    val needsAccessibility: Boolean = false,
    // HNN CN ET v1.0.9: when multiple MCP servers are configured and no
    // per-session choice was made, the UI shows a picker with this list.
    val mcpServerSelection: List<McpServer>? = null
)

object AgentEngine {

    private const val TAG = "AgentEngine"

    /**
     * HNN CN ET v1.0.6: the last device-action command that was blocked by a
     * missing accessibility permission. Kept until the user grants the
     * permission, then re-executed automatically by ChatActivity.
     */
    @Volatile
    var pendingCommand: String? = null

    /**
     * Try to handle [input] as a device-action request.
     * Returns null when the request is not an Agent command (normal chat).
     */
    fun tryHandle(context: Context, input: String): AgentResult? {
        val trimmed = input.trim()
        if (trimmed.length < 2) return null

        // 创建/新建文件
        if (trimmed.matches(Regex("^(创建|新建|生成)\\s*文件.*"))) {
            return handleCreateFile(context, trimmed)
        }

        // 读取/查看文件
        val readMatch = Regex("^(读取|查看|打开一下文件|读一下)\\s*文件\\s*[:：]?\\s*(.+)").find(trimmed)
        if (readMatch != null) {
            return handleReadFile(context, readMatch.groupValues[2].trim())
        }

        // 列出/查看目录
        val listMatch = Regex("^(列出|查看|浏览)\\s*(文件|目录|文件夹)\\s*[:：]?\\s*(.+)").find(trimmed)
        if (listMatch != null) {
            return handleListDir(context, listMatch.groupValues[3].trim())
        }

        // 删除文件
        val delMatch = Regex("^(删除|移除)\\s*文件\\s*[:：]?\\s*(.+)").find(trimmed)
        if (delMatch != null) {
            return handleDeleteFile(context, delMatch.groupValues[2].trim())
        }

        // 修改/追加文件
        val editMatch = Regex("^(修改|编辑|追加|覆盖)\\s*文件\\s*[:：]?\\s*(.+)").find(trimmed)
        if (editMatch != null) {
            return handleEditFile(context, editMatch.groupValues[2].trim())
        }

        // 直接调用 MCP 工具：调用工具 <name> [k=v ...] 或 调用MCP <name> ...
        val toolMatch = Regex("^(调用工具|调用mcp|调用MCP|mcp)\\s+([A-Za-z0-9_.\\-]+)\\s*(.*)").find(trimmed)
        if (toolMatch != null) {
            return handleToolCall(context, null, toolMatch.groupValues[2], toolMatch.groupValues[3].trim())
        }
        // HNN CN ET v1.0.9: "尝试调用mcp" / "帮我调用工具" style input is also an MCP intent.
        if (trimmed.contains("mcp", true) || trimmed.contains("工具")) {
            val loose = Regex("^(?:尝试|帮我|请|麻烦)?\\s*(?:调用|使用|运行|执行|试试)?\\s*(?:mcp|MCP|工具)\\s*(?:调用)?\\s*(.*)").find(trimmed)
            if (loose != null) {
                val rest = loose.groupValues[1].trim()
                val explicit = Regex("^([A-Za-z0-9_.\\-]+)\\s*(.*)").find(rest)?.groupValues?.get(1) ?: ""
                return handleToolCall(context, null, explicit, rest)
            }
        }

        // 打开/启动/开启 应用 — HNN CN ET v1.0.6: 无无障碍权限时先申请，
        // 用户授权后自动继续执行该任务（见 ChatActivity.onResume 的重试逻辑）。
        val openMatch = Regex("^(请|帮我)?(打开|启动|开启|打开一下)\\s*([\\u4e00-\\u9fa5A-Za-z0-9·+_\\-]{1,12})")
            .find(trimmed)
        if (openMatch != null && !trimmed.contains("发消息")) {
            val label = openMatch.groupValues[3]
            if (!HnnAccessibilityService.isEnabled(context)) {
                return AgentResult(
                    context.getString(R.string.agent_needs_accessibility),
                    needsAccessibility = true
                )
            }
            if (launchAppByLabel(context, label)) {
                return AgentResult(context.getString(R.string.agent_opened, label))
            }
            return AgentResult(context.getString(R.string.agent_open_failed, label))
        }

        // 发消息：给 X 发消息[:内容] 或 发消息给 X[:内容] 或 打开 X 发消息[:内容]
        val sendMatch = Regex("(?:给|向)\\s*([\\u4e00-\\u9fa5A-Za-z0-9·+_\\-]{1,12})\\s*(?:发|发送)\\s*(?:消息|信息|短信)\\s*(?:[:：]|说)?\\s*(.*)")
            .find(trimmed)
        if (sendMatch != null) {
            return handleSendMessage(context, sendMatch.groupValues[1], sendMatch.groupValues[2])
        }
        val sendMatch2 = Regex("(?:发|发送)\\s*(?:消息|信息|短信)\\s*(?:给|到)\\s*([\\u4e00-\\u9fa5A-Za-z0-9·+_\\-]{1,12})\\s*(?:[:：]|说)?\\s*(.*)")
            .find(trimmed)
        if (sendMatch2 != null) {
            return handleSendMessage(context, sendMatch2.groupValues[1], sendMatch2.groupValues[2])
        }

        // 看视频 / 播放视频 / 听音乐 / 播放音乐
        val mediaMatch = Regex("^(看视频|播放视频|看个视频|听音乐|播放音乐|放音乐|听歌)\\s*([\\u4e00-\\u9fa5A-Za-z0-9·+_\\-]{1,12})?")
            .find(trimmed)
        if (mediaMatch != null) {
            val kind = mediaMatch.groupValues[1]
            val appHint = mediaMatch.groupValues[2]
            return handleMedia(context, kind, appHint)
        }

        // 返回 / 回到桌面
        if (trimmed == "返回" || trimmed == "回去" || trimmed.contains("回到桌面") || trimmed.contains("回桌面")) {
            return handleBack(context)
        }

        return null
    }

    // ---------- MCP file & tool operations ----------

    private fun mcpServers(context: Context): List<McpServer> = McpConfigStore.loadServers(context)

    /** Try to run [op] against the first MCP server exposing a matching tool; returns (result, usedServer). */
    private fun withMcpTool(
        context: Context,
        candidates: List<String>,
        args: Map<String, String>
    ): Pair<String?, McpServer?> {
        for (server in mcpServers(context)) {
            val (tools, _) = McpClient.listToolsWithError(server)
            val tool = tools.firstOrNull { t ->
                candidates.any { c -> t.contains(c, ignoreCase = true) }
            }
            if (tool != null) {
                val (out, err) = McpClient.callToolWithError(server, tool, args)
                if (out != null) return out to server
                if (err != null && err != "timeout" && err != "connect") {
                    // tool exists but the call failed with a server-side error
                    return null to server
                }
            }
        }
        return null to null
    }

    private fun handleReadFile(context: Context, path: String): AgentResult {
        val (out, _) = withMcpTool(context, listOf("read_file", "read_text_file", "read"), mapOf("path" to path))
        if (out != null) {
            return AgentResult(context.getString(R.string.agent_read_file, path, out.take(4000)))
        }
        val file = localFile(path)
        if (file == null || !file.exists()) {
            return AgentResult(context.getString(R.string.agent_open_failed, "文件 $path"))
        }
        return try {
            AgentResult(context.getString(R.string.agent_read_file, path, file.readText().take(4000)))
        } catch (e: Exception) {
            AgentResult("读取文件失败：${e.message}")
        }
    }

    private fun handleListDir(context: Context, path: String): AgentResult {
        val (out, _) = withMcpTool(context, listOf("list_files", "list_directory", "list_dir"), mapOf("path" to path))
        if (out != null) {
            return AgentResult(context.getString(R.string.agent_list_dir, path, out.take(3000)))
        }
        val dir = File(context.filesDir, "agent")
        val target = File(dir, path)
        val base = if (target.exists() && target.isDirectory) target else dir
        val names = (base.list() ?: emptyArray()).sorted().take(60).joinToString("\n")
        return AgentResult(context.getString(R.string.agent_list_dir, base.absolutePath, names.ifBlank { "（空目录）" }))
    }

    private fun handleDeleteFile(context: Context, path: String): AgentResult {
        val (out, _) = withMcpTool(context, listOf("delete_file", "remove_file"), mapOf("path" to path))
        if (out != null) {
            return AgentResult(context.getString(R.string.agent_deleted, path))
        }
        val file = localFile(path)
        if (file != null && file.exists() && file.delete()) {
            return AgentResult(context.getString(R.string.agent_deleted, path))
        }
        return AgentResult("删除文件失败：$path")
    }

    private fun handleEditFile(context: Context, trimmed: String): AgentResult {
        var body = trimmed
        var content = ""
        val marker = Regex("(?:，|,|\\s)(?:内容|内容为|内容是|内容：)[:：]?\\s*(.*)").find(body)
        if (marker != null) {
            body = body.substring(0, marker.range.first).trim()
            content = marker.groupValues[1]
        }
        val path = body
        if (path.isBlank()) return AgentResult("请提供要修改的文件路径")
        if (content.isBlank()) content = ""
        val (out, _) = withMcpTool(context, listOf("write_file", "edit_file", "update_file"), mapOf("path" to path, "content" to content))
        if (out != null) {
            return AgentResult(context.getString(R.string.agent_wrote, path) + "\n" + out.take(800))
        }
        val file = localFile(path)
        if (file != null) {
            return try {
                file.writeText(content, Charsets.UTF_8)
                AgentResult(context.getString(R.string.agent_wrote, file.absolutePath))
            } catch (e: Exception) {
                AgentResult("修改文件失败：${e.message}")
            }
        }
        return AgentResult("修改文件失败：$path")
    }

    private fun handleToolCall(
        context: Context,
        pinnedServerName: String?,
        toolName: String,
        argsText: String
    ): AgentResult {
        val servers = mcpServers(context)
        if (servers.isEmpty()) {
            return AgentResult(context.getString(R.string.agent_mcp_fail, "未配置 MCP 服务器"))
        }
        // Resolve which server to use: pinned > per-session choice > only server > user picks.
        var server: McpServer? = null
        if (pinnedServerName != null) {
            server = servers.firstOrNull { it.name == pinnedServerName }
        }
        if (server == null) {
            val sessionId = context.getSharedPreferences("session", 0)
                .getString("current_session_id", "") ?: ""
            val chosen = com.alibaba.mnnllm.android.mainsettings.MainSettings
                .getMcpServerChoice(context, sessionId)
            server = servers.firstOrNull { it.name == chosen }
        }
        if (server == null && servers.size == 1) {
            server = servers[0]
        }
        if (server == null) {
            // Ask the user to pick a server (UI shows a chooser dialog).
            return AgentResult("", mcpServerSelection = servers)
        }
        val args = HashMap<String, String>()
        if (argsText.isNotBlank()) {
            Regex("([A-Za-z0-9_.\\-]+)\\s*=\\s*([^\\s]+|\"[^\"]*\")").findAll(argsText).forEach { m ->
                args[m.groupValues[1]] = m.groupValues[2].trim('"')
            }
            if (args.isEmpty()) args["text"] = argsText
        }
        // No explicit tool name → list available tools.
        if (toolName.isBlank()) {
            val (tools, _) = McpClient.listToolsWithError(server)
            if (tools.isEmpty()) {
                return AgentResult(context.getString(R.string.agent_mcp_fail, "服务器无可调用工具"))
            }
            return AgentResult(
                context.getString(R.string.agent_mcp_tools_hint, server.name) + "\n" +
                        tools.take(40).joinToString("\n")
            )
        }
        val (tools, _) = McpClient.listToolsWithError(server)
        val tool = tools.firstOrNull { it.equals(toolName, true) || it.contains(toolName, true) }
        if (tool == null) {
            return AgentResult(context.getString(R.string.agent_mcp_fail, "未找到工具：$toolName"))
        }
        val (out, err) = McpClient.callToolWithError(server, tool, args)
        if (out != null) {
            return AgentResult(context.getString(R.string.agent_mcp_tool_done, tool, out.take(3000)))
        }
        val reason = when (err) {
            "timeout" -> context.getString(R.string.mcp_err_timeout)
            "connect" -> context.getString(R.string.mcp_err_connect)
            "unauthorized" -> "服务器要求访问令牌（Unauthorized），请在配置中填写令牌"
            else -> "调用失败：${err ?: "未知错误"}"
        }
        return AgentResult(context.getString(R.string.agent_mcp_fail, reason))
    }

    /**
     * HNN CN ET v1.0.9: re-run a tool-call command against a user-picked server.
     */
    fun runToolCallWithServer(context: Context, input: String, serverName: String): AgentResult? {
        val trimmed = input.trim()
        val toolMatch = Regex("^(?:尝试|帮我|请|麻烦)?\\s*(?:调用|使用|运行|执行|试试)?\\s*(?:mcp|MCP|工具)\\s*(?:调用)?\\s*([A-Za-z0-9_.\\-]+)\\s*(.*)").find(trimmed)
        val toolName = toolMatch?.groupValues?.get(1) ?: ""
        val argsText = toolMatch?.groupValues?.get(2)?.trim() ?: ""
        if (trimmed.contains("创建文件") || trimmed.contains("新建文件") || trimmed.contains("生成文件")) {
            return handleCreateFileWithServer(context, trimmed, serverName)
        }
        return handleToolCall(context, serverName, toolName, argsText)
    }

    private fun localFile(path: String): File? {
        val dir = File(android.os.Environment.getExternalStorageDirectory(), "HNN-CN-ET/agent")
        val f1 = File(dir, path)
        if (f1.parentFile != null) f1.parentFile.mkdirs()
        return f1
    }

    // ---------- open app ----------

    // HNN CN ET v1.0.9: known package shortcuts for common Chinese apps.
    private val KNOWN_PACKAGES = mapOf(
        "抖音" to "com.ss.android.ugc.aweme",
        "微信" to "com.tencent.mm",
        "QQ" to "com.tencent.mobileqq",
        "淘宝" to "com.taobao.taobao",
        "京东" to "com.jingdong.app.mall",
        "支付宝" to "com.eg.android.AlipayGphone",
        "哔哩哔哩" to "tv.danmaku.bili",
        "bilibili" to "tv.danmaku.bili",
        "快手" to "com.smile.gifmaker",
        "微博" to "com.sina.weibo",
        "小红书" to "com.xingin.xhs",
        "美团" to "com.sankuai.meituan",
        "百度" to "com.baidu.searchbox",
        "高德地图" to "com.autonavi.minimap",
        "网易云音乐" to "com.netease.cloudmusic",
        "QQ音乐" to "com.tencent.qqmusic",
        "酷狗音乐" to "com.kugou.android",
        "爱奇艺" to "com.qiyi.video",
        "优酷" to "com.youku.phone",
        "腾讯视频" to "com.tencent.qqlive",
        "钉钉" to "com.alibaba.android.rimet",
        "WPS" to "cn.wps.moffice_eng",
        "浏览器" to "com.android.browser",
        "相机" to "com.android.camera",
        "设置" to "com.android.settings",
        "文件管理" to "com.android.documentsui"
    )

    /**
     * Launch an app by matching its label or package name against [label].
     * Works without the accessibility permission.
     */
    fun launchAppByLabel(context: Context, label: String): Boolean {
        val pm = context.packageManager
        // 1) known package shortcuts
        KNOWN_PACKAGES.entries.firstOrNull { (key, pkg) ->
            label.contains(key, true) || key.contains(label, true)
        }?.let { (_, pkg) ->
            try {
                pm.getPackageInfo(pkg, 0)
                val launch = pm.getLaunchIntentForPackage(pkg)
                if (launch != null) {
                    launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    context.startActivity(launch)
                    return true
                }
            } catch (e: Exception) {
                Log.w(TAG, "Known package $pkg not installed: ${e.message}")
            }
        }
        val intent = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
        val apps = pm.queryIntentActivities(intent, 0)
        val candidates = mutableListOf<android.content.pm.ResolveInfo>()
        for (app in apps) {
            val appLabel = app.loadLabel(pm)?.toString() ?: ""
            val pkg = app.activityInfo.packageName
            if (appLabel.contains(label, ignoreCase = true) ||
                pkg.contains(label.lowercase(), ignoreCase = true) ||
                label.contains(appLabel, ignoreCase = true)
            ) {
                candidates.add(app)
            }
        }
        if (candidates.isEmpty()) {
            Log.w(TAG, "No app found for label: $label")
            return false
        }
        // Prefer an exact label match; otherwise the first candidate.
        val exact = candidates.firstOrNull {
            it.loadLabel(pm)?.toString()?.equals(label, true) == true
        } ?: candidates.first()
        try {
            val launch = Intent(Intent.ACTION_MAIN).apply {
                addCategory(Intent.CATEGORY_LAUNCHER)
                setPackage(exact.activityInfo.packageName)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(launch)
            return true
        } catch (e: Exception) {
            Log.w(TAG, "Failed to launch ${exact.activityInfo.packageName}: ${e.message}")
            return false
        }
    }

    private fun handleCreateFileWithServer(context: Context, trimmed: String, serverName: String): AgentResult {
        val pathContent = trimmed.replace(Regex("^(创建|新建|生成)\\s*文件\\s*[:：]?"), "")
        var path = pathContent
        var content = ""
        val marker = Regex("(?:，|,|\\s)(?:内容|内容为|内容是|内容：)[:：]?\\s*(.*)").find(pathContent)
        if (marker != null) {
            path = pathContent.substring(0, marker.range.first).trim()
            content = marker.groupValues[1]
        }
        if (path.isBlank()) path = "agent_${System.currentTimeMillis()}.txt"
        if (content.isBlank()) content = "由 HNN CN ET Agent 创建"
        val server = mcpServers(context).firstOrNull { it.name == serverName }
            ?: return AgentResult("未找到 MCP 服务器：$serverName")
        val tools = McpClient.listTools(server)
        val createTool = tools.firstOrNull { t -> t.contains("create_file", true) || t.contains("write_file", true) }
        if (createTool != null) {
            val (result, err) = McpClient.callToolWithError(
                server, createTool, mapOf("path" to path, "content" to content)
            )
            if (result != null) {
                return AgentResult(context.getString(R.string.agent_created_file, path) + "\n" + result.take(800))
            }
            return AgentResult(context.getString(R.string.agent_mcp_fail, err ?: "未知错误"))
        }
        return AgentResult(context.getString(R.string.agent_mcp_fail, "服务器无创建文件工具"))
    }

    // ---------- create file ----------

    private fun handleCreateFile(context: Context, trimmed: String): AgentResult {
        // 创建文件 <path>，内容：<content> / 创建文件 <path> 内容为 <content>
        val pathContent = trimmed.replace(Regex("^(创建|新建|生成)\\s*文件\\s*[:：]?"), "")
        var path = pathContent
        var content = ""
        val marker = Regex("(?:，|,|\\s)(?:内容|内容为|内容是|内容：)[:：]?\\s*(.*)").find(pathContent)
        if (marker != null) {
            path = pathContent.substring(0, marker.range.first).trim()
            content = marker.groupValues[1]
        }
        if (path.isBlank()) {
            path = "agent_${System.currentTimeMillis()}.txt"
        }
        if (content.isBlank()) {
            content = "由 HNN CN ET Agent 创建"
        }

        // Prefer MCP create_file / write_file tools
        val servers = try { McpConfigStore.loadServers(context) } catch (e: Exception) { emptyList() }
        if (servers.isNotEmpty()) {
            for (server in servers) {
                val tools = McpClient.listTools(server)
                val createTool = tools.firstOrNull { t ->
                    t.contains("create_file", true) || t.contains("write_file", true)
                }
                if (createTool != null) {
                    val (result, err) = McpClient.callToolWithError(
                        server, createTool,
                        mapOf("path" to path, "content" to content)
                    )
                    if (result != null) {
                        return AgentResult(context.getString(R.string.agent_created_file, path) + "\n" + result.take(800))
                    }
                    if (err != null && err != "timeout" && err != "connect") {
                        return AgentResult(context.getString(R.string.agent_mcp_fail, err))
                    }
                }
            }
            // MCP configured but no usable tool: fall back to local + note
        }

        // Local fallback: FilesDir/agent/
        val dir = File(context.filesDir, "agent").apply { mkdirs() }
        val safeName = path.substringAfterLast('/').ifBlank { "agent_${System.currentTimeMillis()}.txt" }
        val file = File(dir, safeName)
        return try {
            file.writeText(content, Charsets.UTF_8)
            AgentResult(context.getString(R.string.agent_mcp_none, file.absolutePath))
        } catch (e: Exception) {
            AgentResult("创建文件失败：${e.message}")
        }
    }

    // ---------- send message (needs accessibility) ----------

    private fun handleSendMessage(context: Context, targetApp: String, content: String): AgentResult {
        if (!HnnAccessibilityService.isEnabled(context)) {
            return AgentResult(context.getString(R.string.agent_needs_accessibility), needsAccessibility = true)
        }
        val service = HnnAccessibilityService.instance
        if (service == null) {
            return AgentResult(context.getString(R.string.agent_a11y_not_ready))
        }
        if (!launchAppByLabel(context, targetApp)) {
            return AgentResult(context.getString(R.string.agent_open_failed, targetApp))
        }
        Handler(Looper.getMainLooper()).postDelayed({
            service.typeText(content)
            Handler(Looper.getMainLooper()).postDelayed({ service.clickSend() }, 400)
        }, 1400)
        return AgentResult(context.getString(R.string.agent_sent, targetApp))
    }

    // ---------- media (needs accessibility) ----------

    private fun handleMedia(context: Context, kind: String, appHint: String): AgentResult {
        if (!HnnAccessibilityService.isEnabled(context)) {
            return AgentResult(context.getString(R.string.agent_needs_accessibility), needsAccessibility = true)
        }
        val service = HnnAccessibilityService.instance
        if (service == null) {
            return AgentResult(context.getString(R.string.agent_a11y_not_ready))
        }
        val target = if (appHint.isNotBlank()) appHint else {
            if (kind.contains("音乐") || kind.contains("歌")) "音乐" else "视频"
        }
        if (!launchAppByLabel(context, target)) {
            return AgentResult(context.getString(R.string.agent_open_failed, target))
        }
        // Best effort: tap the first clickable item in the opened app.
        Handler(Looper.getMainLooper()).postDelayed({ service.tapFirstClickable() }, 1600)
        return AgentResult(context.getString(R.string.agent_played, target))
    }

    // ---------- back / home (needs accessibility) ----------

    private fun handleBack(context: Context): AgentResult {
        if (!HnnAccessibilityService.isEnabled(context)) {
            return AgentResult(context.getString(R.string.agent_needs_accessibility), needsAccessibility = true)
        }
        val service = HnnAccessibilityService.instance
        if (service == null) {
            return AgentResult(context.getString(R.string.agent_a11y_not_ready))
        }
        val ok = if (context.getString(R.string.agent_back) == "done") service.goHome() else service.goBack()
        return AgentResult(context.getString(R.string.agent_back))
    }
}

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

data class AgentResult(val reply: String, val needsAccessibility: Boolean = false)

object AgentEngine {

    private const val TAG = "AgentEngine"

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

        // 打开/启动/开启 应用
        val openMatch = Regex("^(请|帮我)?(打开|启动|开启|打开一下)\\s*([\\u4e00-\\u9fa5A-Za-z0-9·+_\\-]{1,12})")
            .find(trimmed)
        if (openMatch != null && !trimmed.contains("发消息")) {
            val label = openMatch.groupValues[3]
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

    // ---------- open app ----------

    /**
     * Launch an app by matching its label or package name against [label].
     * Works without the accessibility permission.
     */
    fun launchAppByLabel(context: Context, label: String): Boolean {
        val pm = context.packageManager
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
        val servers = McpConfigStore.loadServers(context)
        if (servers.isNotEmpty()) {
            for (server in servers) {
                val tools = McpClient.listTools(server)
                val createTool = tools.firstOrNull { t ->
                    t.contains("create_file", true) || t.contains("write_file", true)
                }
                if (createTool != null) {
                    val result = McpClient.callTool(
                        server, createTool,
                        mapOf("path" to path, "content" to content)
                    )
                    if (result != null) {
                        return AgentResult(context.getString(R.string.agent_created_file, path))
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

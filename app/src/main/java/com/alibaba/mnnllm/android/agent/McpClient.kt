// HNN CN ET v1.0.3: MCP (Model Context Protocol) client and config store.
// Servers are stored in SharedPreferences as a JSON array; tools are called
// over JSON-RPC (HTTP POST) with the standard MCP "tools/list" / "tools/call" methods.
package com.alibaba.mnnllm.android.agent

import android.content.Context
import android.util.Log
import com.alibaba.mnnllm.android.mainsettings.MainSettings
import org.json.JSONArray
import org.json.JSONObject
import java.io.OutputStream
import java.net.HttpURLConnection
import java.net.URL

data class McpServer(val name: String, val url: String)

object McpConfigStore {

    private const val TAG = "McpConfigStore"

    fun loadServers(context: Context): List<McpServer> {
        val raw = MainSettings.getMcpServersJson(context)
        if (raw.isBlank()) return emptyList()
        return try {
            val arr = JSONArray(raw)
            val list = mutableListOf<McpServer>()
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                list.add(McpServer(o.optString("name", ""), o.optString("url", "")))
            }
            list
        } catch (e: Exception) {
            Log.w(TAG, "Failed to parse MCP servers: ${e.message}")
            emptyList()
        }
    }

    fun saveServers(context: Context, servers: List<McpServer>) {
        val arr = JSONArray()
        servers.forEach { s ->
            arr.put(JSONObject().put("name", s.name).put("url", s.url))
        }
        MainSettings.setMcpServersJson(context, arr.toString())
    }
}

object McpClient {

    private const val TAG = "McpClient"

    /** POST a JSON-RPC request and return the result object (or null on error). */
    private fun rpc(urlStr: String, method: String, params: JSONObject?, timeoutMs: Int = 8000): JSONObject? {
        return try {
            val url = URL(urlStr)
            val conn = url.openConnection() as HttpURLConnection
            conn.requestMethod = "POST"
            conn.connectTimeout = timeoutMs
            conn.readTimeout = timeoutMs
            conn.doOutput = true
            conn.setRequestProperty("Content-Type", "application/json")
            conn.setRequestProperty("Accept", "application/json, text/event-stream")
            val body = JSONObject()
                .put("jsonrpc", "2.0")
                .put("id", System.currentTimeMillis())
                .put("method", method)
                .apply { if (params != null) put("params", params) }
            val out: OutputStream = conn.outputStream
            out.write(body.toString().toByteArray(Charsets.UTF_8))
            out.flush()
            out.close()
            val code = conn.responseCode
            val text = (if (code in 200..299) conn.inputStream else conn.errorStream)
                ?.bufferedReader()?.use { it.readText() } ?: ""
            conn.disconnect()
            if (text.isBlank()) return null
            val json = JSONObject(text)
            if (json.has("result")) json.getJSONObject("result") else json
        } catch (e: Exception) {
            Log.w(TAG, "MCP rpc failed ($method @ $urlStr): ${e.message}")
            null
        }
    }

    /** List tools exposed by the server. Returns tool names or empty list. */
    fun listTools(server: McpServer): List<String> {
        val result = rpc(server.url, "tools/list", null) ?: return emptyList()
        val tools = result.optJSONArray("tools") ?: return emptyList()
        val names = mutableListOf<String>()
        for (i in 0 until tools.length()) {
            val t = tools.getJSONObject(i)
            t.optString("name").takeIf { it.isNotBlank() }?.let { names.add(it) }
        }
        return names
    }

    /**
     * Call a tool. `arguments` is a plain JSON object map.
     * Returns the tool result text, or null on failure.
     */
    fun callTool(server: McpServer, toolName: String, arguments: Map<String, String>): String? {
        val params = JSONObject().put("name", toolName)
        val args = JSONObject()
        arguments.forEach { (k, v) -> args.put(k, v) }
        params.put("arguments", args)
        val result = rpc(server.url, "tools/call", params) ?: return null
        val content = result.optJSONArray("content")
        if (content != null) {
            val sb = StringBuilder()
            for (i in 0 until content.length()) {
                val c = content.getJSONObject(i)
                val t = c.optString("text")
                if (t.isNotBlank()) {
                    if (sb.isNotEmpty()) sb.append('\n')
                    sb.append(t)
                }
            }
            if (sb.isNotEmpty()) return sb.toString()
        }
        return result.optString("result", result.toString())
    }
}

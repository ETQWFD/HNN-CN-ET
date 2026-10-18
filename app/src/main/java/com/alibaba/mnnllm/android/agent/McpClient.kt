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

data class McpServer(val name: String, val url: String, val token: String = "")

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
                list.add(McpServer(o.optString("name", ""), o.optString("url", ""), o.optString("token", "")))
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
            arr.put(JSONObject().put("name", s.name).put("url", s.url).put("token", s.token))
        }
        MainSettings.setMcpServersJson(context, arr.toString())
    }
}

object McpClient {

    private const val TAG = "McpClient"

    data class RpcOutcome(val ok: Boolean, val json: JSONObject?, val error: String?)

    /** POST a JSON-RPC request; returns a structured outcome with an error reason. */
    private fun rpcEx(server: McpServer, method: String, params: JSONObject?, timeoutMs: Int = 8000): RpcOutcome {
        return try {
            val url = URL(server.url)
            val conn = url.openConnection() as HttpURLConnection
            conn.requestMethod = "POST"
            conn.connectTimeout = timeoutMs
            conn.readTimeout = timeoutMs
            conn.doOutput = true
            conn.setRequestProperty("Content-Type", "application/json")
            conn.setRequestProperty("Accept", "application/json, text/event-stream")
            if (server.token.isNotBlank()) {
                conn.setRequestProperty("Authorization", "Bearer ${server.token}")
            }
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
            if (text.isBlank()) return RpcOutcome(false, null, if (code == 401 || code == 403) "unauthorized" else "empty")
            val json = try {
                JSONObject(text)
            } catch (e: Exception) {
                // Non-JSON response (e.g. "Unauthorized" plain text): keep the raw
                // body so the UI can show the real reason instead of a parse crash.
                val reason = if (code == 401 || code == 403 || text.contains("Unauthorized", true)) {
                    "unauthorized"
                } else {
                    "bad_response:" + text.take(200)
                }
                return RpcOutcome(false, null, reason)
            }
            val result = if (json.has("result")) json.getJSONObject("result") else json
            RpcOutcome(true, result, null)
        } catch (e: java.net.SocketTimeoutException) {
            Log.w(TAG, "MCP rpc timeout ($method @ ${server.url})")
            RpcOutcome(false, null, "timeout")
        } catch (e: java.net.ConnectException) {
            Log.w(TAG, "MCP rpc connect failed ($method @ ${server.url}): ${e.message}")
            RpcOutcome(false, null, "connect")
        } catch (e: Exception) {
            Log.w(TAG, "MCP rpc failed ($method @ ${server.url}): ${e.message}")
            RpcOutcome(false, null, e.message ?: "error")
        }
    }

    /** POST a JSON-RPC request and return the result object (or null on error). */
    private fun rpc(server: McpServer, method: String, params: JSONObject?, timeoutMs: Int = 8000): JSONObject? {
        val outcome = rpcEx(server, method, params, timeoutMs)
        return if (outcome.ok) outcome.json else null
    }

    /** List tools exposed by the server. Returns tool names or empty list. */
    fun listTools(server: McpServer): List<String> = listToolsWithError(server).first

    /** List tools with an error reason string (null when successful). */
    fun listToolsWithError(server: McpServer): Pair<List<String>, String?> {
        val outcome = rpcEx(server, "tools/list", null)
        if (!outcome.ok) return emptyList<String>() to describeError(outcome.error)
        val result = outcome.json ?: return emptyList<String>() to "empty"
        val tools = result.optJSONArray("tools") ?: return emptyList<String>() to "no_tools"
        val names = mutableListOf<String>()
        for (i in 0 until tools.length()) {
            val t = tools.getJSONObject(i)
            t.optString("name").takeIf { it.isNotBlank() }?.let { names.add(it) }
        }
        return names to null
    }

    private fun describeError(code: String?): String? {
        return when (code) {
            "timeout" -> "timeout"
            "connect" -> "connect"
            "empty" -> "empty"
            "unauthorized" -> "unauthorized"
            else -> code
        }
    }

    /**
     * Call a tool. `arguments` is a plain JSON object map.
     * Returns the tool result text, or null on failure.
     */
    fun callTool(server: McpServer, toolName: String, arguments: Map<String, String>): String? =
        callToolWithError(server, toolName, arguments).first

    /** Call a tool; returns (result text, error reason). */
    fun callToolWithError(server: McpServer, toolName: String, arguments: Map<String, String>): Pair<String?, String?> {
        val params = JSONObject().put("name", toolName)
        val args = JSONObject()
        arguments.forEach { (k, v) -> args.put(k, v) }
        params.put("arguments", args)
        val outcome = rpcEx(server, "tools/call", params)
        if (!outcome.ok) return null to describeError(outcome.error)
        val result = outcome.json ?: return null to "empty"
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
            if (sb.isNotEmpty()) return sb.toString() to null
        }
        return result.optString("result", result.toString()) to null
    }
}

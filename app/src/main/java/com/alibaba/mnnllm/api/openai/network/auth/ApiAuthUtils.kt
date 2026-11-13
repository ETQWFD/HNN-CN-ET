package com.alibaba.mnnllm.api.openai.network.auth

import android.content.Context
import com.alibaba.mnnllm.android.MnnLlmApplication
import com.alibaba.mnnllm.api.openai.service.ApiServerConfig
import io.ktor.http.ContentType
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpStatusCode
import io.ktor.server.application.ApplicationCall
import io.ktor.server.response.header
import io.ktor.server.response.respondText

/**
 * HNN CN ET v1.3.5：鉴权判定集中在这里，规则与电脑版 C 服务端保持一致。
 *  - 开关关：一律放行（skip 由 isAuthEnabled 决定）
 *  - 开关开：Authorization 里 Bearer 前缀不区分大小写、允许多个空格，
 *    也兼容 x-api-key: <密钥>，还兼容只塞裸密钥的客户端
 */
object ApiAuthUtils {

    fun isAuthorized(call: ApplicationCall, context: Context): Boolean {
        if (!ApiServerConfig.isAuthEnabled(context)) return true
        val configuredApiKey = ApiServerConfig.getApiKey(context)?.trim()
        if (configuredApiKey.isNullOrEmpty()) return false
        return matches(call, configuredApiKey)
    }

    private fun matches(call: ApplicationCall, key: String): Boolean {
        val auth = call.request.headers[HttpHeaders.Authorization]?.trim()
        if (!auth.isNullOrEmpty()) {
            val parts = auth.split(Regex("\\s+"))
            when {
                parts.size >= 2 && parts[0].equals("bearer", ignoreCase = true) ->
                    if (parts.drop(1).joinToString(" ").trim() == key) return true
                parts.size == 1 && parts[0] == key -> return true
            }
        }
        val xApiKey = call.request.headers["x-api-key"]?.trim()
        if (!xApiKey.isNullOrEmpty() && xApiKey == key) return true
        return false
    }

    /** 路由里调用：未通过则直接回 401 JSON，返回 false 让调用方 return */
    suspend fun ensureAuthorized(call: ApplicationCall): Boolean {
        val context = runCatching { MnnLlmApplication.getAppContext() }.getOrNull() ?: return true
        if (isAuthorized(call, context)) return true
        respondUnauthorized(call)
        return false
    }

    suspend fun respondUnauthorized(call: ApplicationCall) {
        call.response.header(HttpHeaders.WWWAuthenticate, "Bearer")
        call.respondText(
            text = """{"error":{"type":"authentication_error","message":"Invalid API key"}}""",
            contentType = ContentType.Application.Json,
            status = HttpStatusCode.Unauthorized
        )
    }
}

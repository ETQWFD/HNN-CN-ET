package com.alibaba.mnnllm.api.openai.network.routes

import OpenAIChatRequest
import com.alibaba.mnnllm.api.openai.network.logging.ChatLogger
import com.alibaba.mnnllm.api.openai.network.services.MNNChatService
import io.ktor.http.ContentType
import io.ktor.http.HttpStatusCode
import io.ktor.server.request.receive
import io.ktor.server.response.respondText
import io.ktor.server.response.respondText
import io.ktor.server.routing.Route
import io.ktor.server.routing.post
import java.util.UUID
import timber.log.Timber

/** * chatroutedefine * responsible fordefineAPIrouteandrequestdispatch，followsingleresponsibilityprinciple * allbusinesslogicalreadysplittodedicatedserviceclassin*/

/** * registerchatrelatedroute*/
fun Route.chatRoutes() {
    val MNNChatService = MNNChatService()
    val logger = ChatLogger()

    post("/v1/chat/completions") {
        val traceId = UUID.randomUUID().toString()
        logger.logRequestStart(traceId, call)
        try {
            val chatRequest = call.receive<OpenAIChatRequest>()
            MNNChatService.processChatCompletion(call, chatRequest, traceId)
        } catch (e: Exception) {
            // HNN CN ET v1.1.8: never let an unexpected error escape to an empty 500
            // (clients then show a useless "调用错误"). Return a parseable JSON error.
            Timber.tag("ChatRoutes").e(e, "chat completions failed")
            try {
                val body = "{\"error\":{\"message\":\"API 调用失败: ${e.message}\",\"type\":\"internal_error\"}}"
                call.respondText(body, ContentType.Application.Json, HttpStatusCode.InternalServerError)
            } catch (t: Throwable) {
                // response already started (streaming) — nothing more to do
            }
        }
    }
}
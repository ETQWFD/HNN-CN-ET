package com.alibaba.mnnllm.api.openai.network.utils

import com.alibaba.mnnllm.api.openai.network.models.ChatCompletionResponse
import com.alibaba.mnnllm.api.openai.network.models.Choice
import com.alibaba.mnnllm.api.openai.network.models.CompletionChoice
import com.alibaba.mnnllm.api.openai.network.models.Delta
import com.alibaba.mnnllm.api.openai.network.models.DeltaResponse
import com.alibaba.mnnllm.api.openai.network.models.Message
import com.alibaba.mnnllm.api.openai.network.models.Usage
import kotlinx.serialization.json.Json

/** 响应格式化工具：v1.3.5 起模型名由请求方决定，不再写死 "mnn-local"。 */
class ChatResponseFormatter {

    // explicitNulls=false：不要把 "role":null / "usage":null 塞进每个块，
    // 严格的客户端（DSH / LangChain）对多余字段的容错没想象中好。
    private val json = Json {
        prettyPrint = false
        explicitNulls = false
        encodeDefaults = true
    }

    /** 首包：只带 role，OpenAI 流式响应的标准开场 */
    fun createRoleChunk(
        responseId: String,
        created: Long,
        model: String
    ): String = json.encodeToString(
        DeltaResponse.serializer(), DeltaResponse(
            id = responseId,
            `object` = "chat.completion.chunk",
            created = created,
            model = model,
            choices = listOf(
                Choice(delta = Delta(role = "assistant", content = ""), finish_reason = null, index = 0)
            ),
            usage = null
        )
    )

    /** 内容增量 / 结束块 */
    fun createDeltaResponse(
        responseId: String,
        created: Long,
        content: String,
        isLast: Boolean = false,
        usage: Usage? = null,
        model: String = "mnn-local"
    ): String = json.encodeToString(
        DeltaResponse.serializer(), DeltaResponse(
            id = responseId,
            `object` = "chat.completion.chunk",
            created = created,
            model = model,
            choices = listOf(
                Choice(
                    delta = Delta(content = content),
                    finish_reason = if (isLast) "stop" else null,
                    index = 0
                )
            ),
            usage = if (isLast) usage else null
        )
    )

    /** stream_options.include_usage=true 时，OpenAI 会额外发一个 choices 为空的 usage 块 */
    fun createUsageChunk(
        responseId: String,
        created: Long,
        model: String,
        usage: Usage
    ): String = json.encodeToString(
        DeltaResponse.serializer(), DeltaResponse(
            id = responseId,
            `object` = "chat.completion.chunk",
            created = created,
            model = model,
            choices = emptyList(),
            usage = usage
        )
    )

    fun createChatCompletionResponse(
        responseId: String,
        created: Long,
        content: String,
        usage: Usage,
        model: String = "mnn-local"
    ): String = json.encodeToString(
        ChatCompletionResponse.serializer(), ChatCompletionResponse(
            id = responseId,
            `object` = "chat.completion",
            created = created,
            model = model,
            choices = listOf(
                CompletionChoice(
                    message = Message(role = "assistant", content = content),
                    finish_reason = "stop",
                    index = 0
                )
            ),
            usage = usage
        )
    )

    fun createUsageFromMetrics(metrics: HashMap<String, Any>): Usage {
        val promptTokens = (metrics["prompt_len"] as? Long)?.toInt() ?: 0
        val completionTokens = (metrics["decode_len"] as? Long)?.toInt() ?: 0
        return Usage(
            prompt_tokens = promptTokens,
            completion_tokens = completionTokens,
            total_tokens = promptTokens + completionTokens
        )
    }
}

package com.alibaba.mnnllm.api.openai.network.validators

import OpenAIChatRequest

/** * request verification * responsible for verificationrequestdatavalidity*/
class ChatRequestValidator {

    /** * verificationchatrequest * @param request chat request object * @return verification result,containingwhether validanderrorinfo*/
    fun validateChatRequest(request: OpenAIChatRequest): ValidationResult {
        //checkmessagelistwhether empty
        if (request.messages.isNullOrEmpty()) {
            return ValidationResult(
                isValid = false,
                errorMessage = "messages 不能为空：请传入至少一条消息"
            )
        }

        // HNN CN ET v1.2.1: also reject requests whose messages contain no usable
        // content (all content parts were filtered out / content absent).
        val hasUsable = request.messages.any { it.content != null }
        if (!hasUsable) {
            return ValidationResult(
                isValid = false,
                errorMessage = "消息内容无法识别：请检查 messages[].content 格式（纯文本字符串或 [{type:text,text:...}]）"
            )
        }

        //canaddmoreverificationrules
        //e.g.：checkmessageformat、modelnameetc.
        
        return ValidationResult(isValid = true)
    }

    /**
     * verificationresultdataclass*/
    data class ValidationResult(
        val isValid: Boolean,
        val errorMessage: String? = null
    )
}
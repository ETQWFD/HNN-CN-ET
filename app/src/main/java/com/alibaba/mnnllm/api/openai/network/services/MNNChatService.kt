package com.alibaba.mnnllm.api.openai.network.services

import OpenAIChatRequest
import com.alibaba.mnnllm.android.MnnLlmApplication
import com.alibaba.mnnllm.android.llm.LlmSession
import com.alibaba.mnnllm.api.openai.di.ServiceLocator
import com.alibaba.mnnllm.api.openai.network.handlers.ResponseHandler
import com.alibaba.mnnllm.api.openai.network.logging.ChatLogger
import com.alibaba.mnnllm.api.openai.network.processors.MnnImageProcessor
import com.alibaba.mnnllm.api.openai.network.queue.RequestQueueManager
import com.alibaba.mnnllm.api.openai.network.utils.MessageTransformer
import com.alibaba.mnnllm.api.openai.network.validators.ChatRequestValidator
import io.ktor.http.HttpStatusCode
import io.ktor.server.application.ApplicationCall
import io.ktor.server.response.respond
import kotlinx.coroutines.runBlocking
import java.util.UUID
import timber.log.Timber

/** * chat service * responsible for coordinatingvarious components,processchatrequestcorebusinesslogic * nowallrequests throughqueuemanagerperformqueueprocess,ensuresame timeonlyoneLLM generationtaskrunning*/
class MNNChatService {

    private val messageTransformer = MessageTransformer()
    private val responseHandler = ResponseHandler()
    private val chatRequestValidator = ChatRequestValidator()
    private val logger = ChatLogger()
    private val requestQueueManager = RequestQueueManager.getInstance()

    //getdependency
    private val chatSessionProvider = ServiceLocator.getChatSessionProvider()
    private val imageProcessor = MnnImageProcessor.getInstance(MnnLlmApplication.getAppContext())
    
    /** * getLLMsessioninstance * @return LlmSessioninstance，if not availablethenthrowexception * HNN CN ET v1.2.1: when no session is active yet (user never loaded a model in * the app), auto-activate the first downloaded model so third-party AI clients * can call the API without any manual step. */
    private fun getLlmSession(): LlmSession {
        val active = chatSessionProvider.getLlmSession()
        if (active != null) return active

        // HNN CN ET v1.2.1: auto-init — pick the first downloaded model and load it.
        try {
            val models = com.alibaba.mnnllm.android.modelist.ModelListManager.getCurrentModels()
            val firstId = models?.firstOrNull { !it.modelItem.modelId.isNullOrBlank() }?.modelItem?.modelId
            if (!firstId.isNullOrBlank()) {
                val result = ServiceLocator.getLlmRuntimeController().ensureSession(firstId)
                if (result.success && result.session != null) {
                    Timber.tag("MNNChatService").i("API auto-activated model: $firstId")
                    return result.session
                }
            }
        } catch (t: Throwable) {
            Timber.tag("MNNChatService").w(t, "auto session init failed")
        }
        throw IllegalStateException("请先在 HNN CN ET 中加载模型：打开「我的模型」选择一个已下载模型后再调用 API")
    }

    /**
     * HNN CN ET v1.3.4: if the third-party client explicitly selected a model,
     * resolve that name against the downloaded model list and switch the runtime
     * session when it differs from the active one. Blank / "default" / unresolved
     * names are ignored so the existing fallback path stays intact.
     */
    private fun switchSessionForRequestedModel(rawModelName: String?, traceId: String) {
        val query = rawModelName?.trim()
        if (query.isNullOrBlank() || query.equals("default", ignoreCase = true)) return

        val resolved = com.alibaba.mnnllm.android.modelist.ModelListManager
            .resolveModelByAnyName(query)
        if (resolved == null) {
            Timber.tag("MNNChatService").i(
                "Requested model '$query' not found in local list, keeping current session"
            )
            return
        }

        val runtime = ServiceLocator.getLlmRuntimeController()
        if (runtime.getActiveModelId() == resolved.modelId) return

        runCatching {
            val result = runtime.ensureSession(resolved.modelId)
            if (result.success) {
                Timber.tag("MNNChatService").i(
                    "API switched model for client request: '$query' -> ${resolved.modelId}"
                )
            } else {
                Timber.tag("MNNChatService").w(
                    "Model switch to ${resolved.modelId} failed (${result.reason}), keeping current session"
                )
            }
        }.onFailure { t ->
            Timber.tag("MNNChatService").w(t, "Model switch threw, keeping current session")
        }
    }

    /** * processchatcompleterequest * nowallrequests willbeen added toqueue,ensureaccording to orderprocess * @param call Ktorapplicationcallcontext * @param chatRequest chat request object * @param traceId traceID*/
    suspend fun processChatCompletion(
        call: ApplicationCall,
        chatRequest: OpenAIChatRequest,
        traceId: String
    ) {
        //generate uniquerequest ID
        val requestId = UUID.randomUUID().toString()
        
        //first performbasic verification,avoidinvalidrequest enteringqueue
        val validationResult = chatRequestValidator.validateChatRequest(chatRequest)
        if (!validationResult.isValid) {
            call.respond(
                HttpStatusCode.BadRequest,
                mapOf("error" to mapOf("message" to validationResult.errorMessage))
            )
            return
        }
        
        logger.logRequestBody(traceId, chatRequest)
        logger.logInfo(traceId, "请求已提交到队列，requestId=$requestId")
        
        //willrequesttoqueueperformqueueprocess,andwaitcomplete
        try {
            requestQueueManager.submitRequest(
                requestId = requestId,
                traceId = traceId,
                task = {
                    //here isactualLLM processinglogic
                    processLlmGeneration(call, chatRequest, traceId)
                },
                onComplete = {
                    logger.logInfo(traceId, "队列任务完成，requestId=$requestId")
                },
                onError = { exception ->
                    logger.logError(traceId, exception, "队列任务失败，requestId=$requestId")
                }
            )
        } catch (e: Exception) {
            logger.logError(traceId, e, "队列处理失败，requestId=$requestId")
            call.respond(
                HttpStatusCode.InternalServerError,
                mapOf(
                    "error" to mapOf(
                        "message" to "Queue processing failed",
                        "trace_id" to traceId
                    )
                )
            )
        }
    }
    
    /** * actualLLMgenerateprocesslogic * thismethodwillatqueueinaccording toorderexecute*/
    private suspend fun processLlmGeneration(
        call: ApplicationCall,
        chatRequest: OpenAIChatRequest,
        traceId: String
    ) {
        try {
            // HNN CN ET v1.3.4: honor the model name the third-party client actually
            // selected (previously chatRequest.model was ignored and the active session
            // was always used). Switch the runtime session when the requested name
            // resolves to a different downloaded model; on any miss, fall through to
            // the existing active-session / first-downloaded-model path — never 500.
            switchSessionForRequestedModel(chatRequest.model, traceId)

            //willas MNNformatunifiedhistory message (containingsystem prompt)
            val unifiedHistory = messageTransformer.convertToUnifiedMnnHistory(
                chatRequest.messages,
                imageProcessor,
                getLlmSession()
            )

            //recordconvertafterhistorymessage
            logger.logTransformedHistory(traceId, unifiedHistory)

            //useunifiedcompletehistorymessageperforminference（APIservicemode）
            if (unifiedHistory.isNotEmpty()) {
                logger.logInferenceStart(traceId, unifiedHistory.size)

                if (chatRequest.stream == true) {
                    responseHandler.handleStreamResponseWithFullHistory(
                        call,
                        unifiedHistory,
                        traceId
                    )
                } else {
                    responseHandler.handleNonStreamResponseWithFullHistory(call, unifiedHistory)
                }

                logger.logInferenceComplete(traceId)
            }
        } catch (e: Exception) {
            logger.logError(traceId, e, "LLM生成失败")
            // HNN CN ET v1.2.1: surface the real reason (e.g. "no model loaded") so
            // third-party clients can show a meaningful message instead of a generic
            // "Internal server error".
            call.respond(
                HttpStatusCode.InternalServerError,
                mapOf(
                    "error" to mapOf(
                        "message" to (e.message ?: "模型生成失败，请检查模型是否已下载并加载"),
                        "type" to "generation_error",
                        "trace_id" to traceId
                    )
                )
            )
        }
    }
    
    /** * getqueuestatisticsinfo*/
    fun getQueueStats() = requestQueueManager.getQueueStats()
}
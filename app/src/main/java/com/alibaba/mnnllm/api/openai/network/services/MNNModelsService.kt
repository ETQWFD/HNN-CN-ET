package com.alibaba.mnnllm.api.openai.network.services

import com.alibaba.mnnllm.api.openai.network.logging.ChatLogger
import com.alibaba.mnnllm.api.openai.network.models.ModelData
import com.alibaba.mnnllm.api.openai.network.models.ModelPermission
import com.alibaba.mnnllm.api.openai.network.models.ModelsResponse
import com.alibaba.mnnllm.android.modelist.ModelListManager
import io.ktor.http.HttpStatusCode
import io.ktor.server.application.call
import io.ktor.server.response.respond
import timber.log.Timber

/** * MNN modelservice * responsible forprocessmodelrelatedbusinesslogic*/
class MNNModelsService {
    private val logger = ChatLogger()

    suspend fun getAvailableModels(call: io.ktor.server.application.ApplicationCall, traceId: String) {
        try {
            logger.logRequestStart(traceId, call)

            // HNN CN ET v1.2.1: /v1/models must NEVER return 500. Third-party AI
            // clients (Chatbox / NextChat / LobeChat ...) probe this endpoint first and
            // abort with "API error" if it fails. Return every downloaded model; if none
            // is installed yet, return a placeholder so the client can still proceed.
            val availableModels = ModelListManager.getCurrentModels()
            val modelDataList = if (!availableModels.isNullOrEmpty()) {
                availableModels
                    .mapNotNull { it.modelItem.modelId }
                    .filter { it.isNotBlank() }
                    .distinct()
                    .map { id ->
                        ModelData(
                            id = id,
                            created = System.currentTimeMillis() / 1000,
                            permission = listOf(
                                ModelPermission(
                                    id = "modelperm-$id",
                                    created = System.currentTimeMillis() / 1000
                                )
                            )
                        )
                    }
                    .ifEmpty {
                        listOf(ModelData(
                            id = "default",
                            created = System.currentTimeMillis() / 1000,
                            permission = listOf(
                                ModelPermission(
                                    id = "modelperm-default",
                                    created = System.currentTimeMillis() / 1000
                                )
                            )
                        ))
                    }
            } else {
                listOf(ModelData(
                    id = "default",
                    created = System.currentTimeMillis() / 1000,
                    permission = listOf(
                        ModelPermission(
                            id = "modelperm-default",
                            created = System.currentTimeMillis() / 1000
                        )
                    )
                ))
            }

            val response = ModelsResponse(data = modelDataList)

            call.respond(response)
            logger.logInfo(traceId, "Models returned: ${modelDataList.map { it.id }}")

        } catch (e: Exception) {
            Timber.tag("MNNModelsService").e(e, "Error getting current model")
            logger.logError(traceId, e, "Failed to get current model")
            // HNN CN ET v1.2.1: even on unexpected errors return a placeholder (200),
            // never a 500 that makes third-party clients show a generic failure.
            try {
                call.respond(
                    HttpStatusCode.OK,
                    ModelsResponse(data = listOf(ModelData(
                        id = "default",
                        created = System.currentTimeMillis() / 1000,
                        permission = listOf(
                            ModelPermission(
                                id = "modelperm-default",
                                created = System.currentTimeMillis() / 1000
                            )
                        )
                    )))
                )
            } catch (t: Throwable) {
                Timber.tag("MNNModelsService").e(t, "Fallback models response also failed")
            }
        }
    }
}

package com.alibaba.mnnllm.api.openai.network.routes

import com.alibaba.mnnllm.api.openai.network.auth.ApiAuthUtils
import com.alibaba.mnnllm.api.openai.network.models.ModelData
import com.alibaba.mnnllm.api.openai.network.models.ModelPermission
import com.alibaba.mnnllm.api.openai.network.models.ModelsResponse
import com.alibaba.mnnllm.api.openai.network.services.MNNModelsService
import io.ktor.server.routing.Route
import io.ktor.server.routing.get
import java.util.UUID

/** * modelroutedefine * responsible fordefine /v1/models APIroute*/

/** * registermodelrelatedroute*/
fun Route.modelsRoutes() {
    val modelsService = MNNModelsService()

    get("/v1/models") {
        // v1.3.5：模型列表与对话接口使用同一套密钥规则。
        // 之前这里在认证块之外，开密钥后 models 仍然免检，和电脑版行为不一致。
        if (!ApiAuthUtils.ensureAuthorized(call)) return@get
        val traceId = UUID.randomUUID().toString()
        modelsService.getAvailableModels(call, traceId)
    }
}

// Created by ruoyi.sjd on 2025/2/28.
// Copyright (c) 2024 Alibaba Group Holding Limited All rights reserved.
package com.alibaba.mnnllm.android.mainsettings

import android.content.Context
import androidx.preference.PreferenceManager
import com.alibaba.mls.api.source.ModelSources
import com.alibaba.mnnllm.android.utils.DeviceUtils


object MainSettings {

    fun getDownloadProvider(context: Context): ModelSources.ModelSourceType {
        val result = getDownloadProviderString(context)
        return when (result) {
            ModelSources.sourceHuffingFace -> ModelSources.ModelSourceType.HUGGING_FACE
            ModelSources.sourceModelScope -> ModelSources.ModelSourceType.MODEL_SCOPE
            else -> ModelSources.ModelSourceType.MODELERS
         }
    }

    fun setDownloadProvider(context: Context, source:String) {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        sharedPreferences.edit().putString("download_provider", source)
        .apply()
    }

    fun getDownloadProviderString(context: Context):String {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        return sharedPreferences.getString("download_provider", getDefaultDownloadProvider())!!
    }

    private fun getDefaultDownloadProvider():String {
        return if (DeviceUtils.isChinese) ModelSources.sourceModelScope else ModelSources.sourceHuffingFace
    }

    fun isStopDownloadOnChatEnabled(context: Context): Boolean {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        return sharedPreferences.getBoolean("stop_download_on_chat", true)
    }

    fun isApiServiceEnabled(context: Context): Boolean {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        return sharedPreferences.getBoolean("enable_api_service", false)
    }

    /**
     * HNN CN ET v1.0.2: AI 模型审核开关。
     * 开启后可使用安全审核(Guard)模型对对话进行过滤；
     * 关闭（默认）时直接使用所选模型原生对话，不经过任何附加审核。
     */
    fun isAiSafetyFilterEnabled(context: Context): Boolean {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        return sharedPreferences.getBoolean("ai_safety_filter", false)
    }

    fun setAiSafetyFilterEnabled(context: Context, enabled: Boolean) {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        sharedPreferences.edit().putBoolean("ai_safety_filter", enabled).apply()
    }

    /**
     * Get the default TTS model ID
     */
    fun getDefaultTtsModel(context: Context): String? {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        return sharedPreferences.getString("default_tts_model", null)
    }

    /**
     * Set the default TTS model ID
     */
    fun setDefaultTtsModel(context: Context, modelId: String) {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        sharedPreferences.edit()
            .putString("default_tts_model", modelId)
            .apply()
    }

    /**
     * Check if the given model is the current default TTS model
     */
    fun isDefaultTtsModel(context: Context, modelId: String): Boolean {
        return getDefaultTtsModel(context) == modelId
    }

    /**
     * Get the default ASR model ID
     */
    fun getDefaultAsrModel(context: Context): String? {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        return sharedPreferences.getString("default_asr_model", null)
    }

    /**
     * Set the default ASR model ID
     */
    fun setDefaultAsrModel(context: Context, modelId: String) {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        sharedPreferences.edit()
            .putString("default_asr_model", modelId)
            .apply()
    }

    /**
     * Check if the given model is the current default ASR model
     */
    fun isDefaultAsrModel(context: Context, modelId: String): Boolean {
        return getDefaultAsrModel(context) == modelId
    }

    /**
     * HNN CN ET v1.0.3: Agent 工作模式开关。
     * 开启后 AI 可在聊天中帮你打开应用、发消息、看视频、听音乐、创建文件。
     */
    fun isAgentModeEnabled(context: Context): Boolean {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        return sharedPreferences.getBoolean("agent_mode", false)
    }

    fun setAgentModeEnabled(context: Context, enabled: Boolean) {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        sharedPreferences.edit().putBoolean("agent_mode", enabled).apply()
    }

    /**
     * MCP 服务器列表（JSON 数组字符串）。
     */
    fun getMcpServersJson(context: Context): String {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        return sharedPreferences.getString("mcp_servers", "") ?: ""
    }


    /**
     * HNN CN ET v1.0.5: 高速直答模式开关。
     * 开启后 AI 直接回答、不拒绝问题，并跳过深度思考，回复更快。
     */
    fun isDirectReplyEnabled(context: Context): Boolean {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        return sharedPreferences.getBoolean("direct_reply", false)
    }

    fun setDirectReplyEnabled(context: Context, enabled: Boolean) {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        sharedPreferences.edit().putBoolean("direct_reply", enabled).apply()
    }

    fun setMcpServersJson(context: Context, json: String) {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
        sharedPreferences.edit().putString("mcp_servers", json).apply()
    }
}

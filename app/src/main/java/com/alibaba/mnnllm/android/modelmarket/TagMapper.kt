package com.alibaba.mnnllm.android.modelmarket

import android.content.Context
import com.alibaba.mnnllm.android.utils.DeviceUtils
import timber.log.Timber

object TagMapper {

    /**
     * Built-in fallback translations so every tag used by the model market
     * (including Safety / TTS / ASR / English / English and Chinese) always
     * resolves, even when the remote market config ships without them.
     * Config-provided translations override these defaults.
     */
    private val builtinTranslations: Map<String, String> = mapOf(
        "Vision" to "图像理解",
        "Video" to "视频理解",
        "Audio" to "音频理解",
        "Code" to "代码",
        "Math" to "数学",
        "ImageGen" to "文生图",
        "AudioGen" to "音频生成",
        "Think" to "深度思考",
        "Chat" to "对话",
        "Safety" to "安全审核",
        "TTS" to "语音合成",
        "ASR" to "语音识别",
        "English" to "英语",
        "English and Chinese" to "中英双语",
        "QNN" to "QNN",
        "Omni" to "全模态",
        "Local" to "本地"
    )

    private var tagMap: Map<String, Tag> = emptyMap()

    fun initializeFromConfig(config: ModelMarketConfig) {
        val mappings = mutableMapOf<String, Tag>()
        // Built-in defaults first, then config overrides.
        builtinTranslations.forEach { (key, chineseTranslation) ->
            mappings[chineseTranslation] = Tag(chineseTranslation, key)
            mappings[key] = Tag(chineseTranslation, key)
        }
        config.tagTranslations.forEach { (key, chineseTranslation) ->
            mappings[chineseTranslation] = Tag(chineseTranslation, key)
            mappings[key] = Tag(chineseTranslation, key)
        }
        tagMap = mappings
        Timber.d("initializeFromConfig: loaded ${tagMap.size} tag mappings")
    }
    
    fun getTag(stringTag: String): Tag {
        if (stringTag.equals("local", ignoreCase = true)) {
            return Tag("本地", "local")
        }
        if (stringTag.equals("builtin", ignoreCase = true)) {
            return Tag("内置", "builtin")
        }
        val tag = tagMap[stringTag] ?: Tag(stringTag, stringTag) // Fallback for unmapped tags
        if (tagMap[stringTag] == null && tagMap.isNotEmpty()) {
            Timber.w("getTag: '$stringTag' not found in tagMap (size=${tagMap.size}), using fallback")
        }
        return tag
    }
    
    fun getAllTags(): List<Tag> {
        return tagMap.values.toList().distinctBy { it.key }.sortedBy { it.getDisplayText() }
    }
    
    fun getQuickFilterTags(quickFilterTagNames: List<String>): List<Tag> {
        return quickFilterTagNames.mapNotNull { tagName ->
            tagMap[tagName]
        }
    }

    fun getDisplayTagList(tagKeys: List<String>): List<String> {
        return tagKeys.map { getTag(it).getDisplayText() }
    }

    /**
     * Context-aware version for ViewHolder bind. Uses the View's context for locale
     * so tags display correctly on Chinese devices without requiring scroll.
     */
    fun getDisplayTagList(tagKeys: List<String>, context: Context): List<String> {
        val useChinese = DeviceUtils.isChinese(context)
        return tagKeys.map { getTag(it).getDisplayText(useChinese) }
    }
} 
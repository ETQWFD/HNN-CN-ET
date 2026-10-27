package com.alibaba.mnnllm.android.modelist

import android.content.Context
import android.util.Log
import com.alibaba.mls.api.ModelItem
import com.alibaba.mls.api.download.ModelDownloadManager
import java.io.File

object LocalModelsProvider {
    /**
     * HNN CN ET v1.2.5: scan the REAL model root (/storage/emulated/0/hnn) instead of
     * the stale dev path /data/local/tmp/mnn_models which made the "我的模型" page
     * always show "没有找到模型" even after a successful download.
     *
     * A folder counts as a local model when it contains a loadable file
     * (llm.mnn / *.mnn / *.gguf / *.safetensors) or a config.json.
     */
    fun getLocalModels(context: Context): MutableList<ModelItem> {
        val result = mutableListOf<ModelItem>()
        try {
            val root = File(ModelDownloadManager.resolveModelRoot(context))
            scanDir(root, result)
            // Downloads may land under <root>/modelers/<repo> (blobs + pointers).
            val modelers = File(root, "modelers")
            if (modelers.exists() && modelers.isDirectory) {
                scanDir(modelers, result)
            }
            // Keep old behavior for legacy dev roots.
            val legacy = File("/data/local/tmp/mnn_models/")
            if (legacy.exists() && legacy.isDirectory) {
                scanDir(legacy, result)
            }
        } catch (e: Exception) {
            Log.e("LocalModelsProvider", "Failed to scan local models", e)
        }
        // Deduplicate by absolute path.
        val seen = mutableSetOf<String>()
        return result.filter { seen.add(it.localPath.orEmpty()) }.toMutableList()
    }

    private fun scanDir(dir: File, out: MutableList<ModelItem>) {
        if (!dir.exists() || !dir.isDirectory) return
        dir.listFiles()?.forEach { modelDir ->
            if (!modelDir.isDirectory) return@forEach
            if (modelDir.name.startsWith("blobs")) return@forEach
            val hasModelFile = modelDir.listFiles()?.any { f ->
                val n = f.name.lowercase()
                !f.isDirectory && (n.endsWith(".mnn") || n.endsWith(".gguf") ||
                    n.endsWith(".safetensors") || n == "config.json")
            } ?: false
            if (hasModelFile) {
                val modelPath = modelDir.absolutePath
                val modelId = "local/${modelPath}"
                out.add(ModelItem.fromLocalModel(modelId, modelPath))
            }
        }
    }
}

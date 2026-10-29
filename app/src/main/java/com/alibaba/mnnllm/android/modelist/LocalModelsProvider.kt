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
        scanDirRecursive(dir, out, 0)
    }

    /**
     * HNN CN ET v1.3.4: recursive descent. The old one-level scan only saw
     * <root>/<model> and <root>/modelers/<model>; downloads that land nested
     * (<root>/modelscope/<org>/<model>, <root>/builtin/<model>, or any container
     * with an extra intermediary directory) were never listed in "我的模型".
     * We keep descending into plain directories until a model marker appears.
     */
    private fun scanDirRecursive(dir: File, out: MutableList<ModelItem>, depth: Int) {
        if (!dir.exists() || !dir.isDirectory || depth > 6) return
        dir.listFiles()?.forEach { entry ->
            if (!entry.isDirectory) return@forEach
            if (entry.name.startsWith("models--")) return@forEach // HF cache container
            if (entry.name == "blobs") return@forEach

            val children = entry.listFiles() ?: return@forEach
            // HNN CN ET v1.2.6: downloaded MNN repos keep weights as unnamed blobs
            // under <repo>/blobs/ — accept a non-empty blobs/ subfolder as a marker.
            val hasBlobs = children.any { f ->
                f.isDirectory && f.name == "blobs" && (f.listFiles()?.isNotEmpty() == true)
            }
            val hasModelFile = children.any { f ->
                val n = f.name.lowercase()
                !f.isDirectory && (n.endsWith(".mnn") || n.endsWith(".gguf") ||
                    n.endsWith(".safetensors") || n == "config.json")
            }
            if (hasBlobs || hasModelFile) {
                val modelPath = entry.absolutePath
                val modelId = "local/${modelPath}"
                out.add(ModelItem.fromLocalModel(modelId, modelPath))
            } else if (depth < 6) {
                // Container / intermediary directory (modelscope/<org>, builtin, ...)
                scanDirRecursive(entry, out, depth + 1)
            }
        }
    }
}

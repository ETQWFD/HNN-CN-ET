// Created by ruoyi.sjd on 2025/5/8.
// Copyright (c) 2024 Alibaba Group Holding Limited All rights reserved.

package com.alibaba.mls.api.download

import android.util.Log
import com.alibaba.mls.api.download.DownloadFileUtils.repoFolderName
import com.alibaba.mls.api.download.ModelIdUtils
import java.io.File
import java.util.Collections

abstract class ModelRepoDownloader {
    abstract var callback: ModelRepoDownloadCallback?
    abstract var cacheRootPath:String
    protected val pausedSet: MutableSet<String?> = Collections.synchronizedSet(HashSet())
    abstract suspend fun getRepoSize(modelId: String):Long
    abstract fun setListener(callback: ModelRepoDownloadCallback?)
    abstract fun download(modelId: String)

    fun repoModelRealFile(modelId: String):File {
        return File(cacheRootPath, "${repoFolderName(ModelIdUtils.getRepositoryPath(modelId), "model")}/blobs")
    }

    abstract suspend fun checkUpdate(modelId: String)

    fun pause(modelId: String) {
        pausedSet.add(modelId)
    }

    fun clearPause(modelId: String) {
        pausedSet.remove(modelId)
    }

    fun isDownloaded(modelId: String): Boolean {
        // HNN CN ET v1.1.5: only count as downloaded when real model files exist.
        // A stale link directory with no model files must NOT block re-download.
        // v1.1.7: any path/IO failure here must never break startDownload -> return false.
        return try {
            val path = getDownloadPath(modelId)
            if (!path.exists()) return false
            val children = path.listFiles() ?: return false
            if (children.isEmpty()) return false
            fun hasModelFile(f: java.io.File): Boolean = f.isFile && (
                f.name.endsWith(".mnn") || f.name.endsWith(".gguf") ||
                f.name.endsWith(".safetensors") || f.name.endsWith(".bin")
            )
            if (children.any { hasModelFile(it) }) return true
            // Link folder may point into the storage folder (models--org--name)
            val storage = children.firstOrNull { it.isDirectory } ?: return false
            (storage.listFiles() ?: emptyArray()).any { hasModelFile(it) }
        } catch (t: Throwable) {
            Log.w("ModelRepoDownloader", "isDownloaded failed for $modelId", t)
            false
        }
    }

    abstract fun getDownloadPath(modelId: String): File
    abstract fun deleteRepo(modelId: String)

    interface ModelRepoDownloadCallback {
        fun onDownloadFailed(modelId: String, e: Exception)
        fun onDownloadTaskAdded()
        fun onDownloadTaskRemoved()
        fun onDownloadPaused(modelId: String)
        fun onDownloadFileFinished(modelId: String, absolutePath: String)
        fun onDownloadingProgress(
            modelId: String,
            stage: String,
            currentFile: String?,
            saved: Long,
            total: Long)
        fun onRepoInfo(modelId: String, lastModified: Long, repoSize: Long)
        fun onDownloadPending(modelId: String)
    }
}

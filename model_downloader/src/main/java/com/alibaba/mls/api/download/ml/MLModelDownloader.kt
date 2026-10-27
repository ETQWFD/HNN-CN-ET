// Created by ruoyi.sjd on 2025/5/8.
// Copyright (c) 2024 Alibaba Group Holding Limited All rights reserved.

package com.alibaba.mls.api.download.ml
import android.util.Log
import com.alibaba.mls.api.ApplicationProvider
import com.alibaba.mls.api.FileDownloadException
import com.alibaba.mls.api.download.DownloadExecutor
import com.alibaba.mls.api.HfFileMetadata
import com.alibaba.mls.api.download.DownloadFileUtils.createSymlink
import com.alibaba.mls.api.download.DownloadFileUtils.deleteDirectoryRecursively
import com.alibaba.mls.api.download.DownloadFileUtils.getLastFileName
import com.alibaba.mls.api.download.DownloadFileUtils.getPointerPathParent
import com.alibaba.mls.api.download.DownloadFileUtils.repoFolderName
import com.alibaba.mls.api.download.DownloadPausedException
import com.alibaba.mls.api.download.FileDownloadTask
import com.alibaba.mls.api.download.ModelFileDownloader
import com.alibaba.mls.api.download.ModelFileDownloader.FileDownloadListener
import com.alibaba.mls.api.download.ModelRepoDownloader
import com.alibaba.mls.api.ml.FileInfo
import com.alibaba.mls.api.ml.MlApiClient
import com.alibaba.mls.api.ml.MlRepoInfo
import com.alibaba.mls.api.download.ModelIdUtils
import com.alibaba.mls.api.download.DownloadCoroutineManager
import kotlinx.coroutines.withContext
import java.io.File
import com.alibaba.mls.api.ml.MlRepoData
import com.alibaba.mls.api.download.TimeUtils


class MLModelDownloader(override var callback: ModelRepoDownloadCallback?,
                        cacheRootPath: String
) : ModelRepoDownloader() {
    companion object  {
        private const val TAG = "MLModelDownloader"

        fun getCachePathRoot(modelDownloadPathRoot:String): String {
            return "$modelDownloadPathRoot/modelers"
        }

        fun getModelPath(modelsDownloadPathRoot: String, modelId: String): File {
            return File(modelsDownloadPathRoot, getLastFileName(modelId))
        }
    }

    override var cacheRootPath: String = getCachePathRoot(cacheRootPath)
    private var mlApiClient: MlApiClient = MlApiClient()
    override fun setListener(callback: ModelRepoDownloadCallback?) {
        this.callback = callback
    }

    /**
     * Unified method to fetch repo information from ModelScope API.
     * HNN CN ET v1.2.3: the old channel hit modelers.cn, which is dead (404) and
     * made every download fail with "Failed to fetch repo info". We now call the
     * official ModelScope repo API (same as the desktop build, verified 200):
     *   GET https://modelscope.cn/api/v1/models/{owner}/{repo}/repo/files?Recursive=1
     *   -> {"Code":200,"Data":{"Files":[{Path,Size,Type,Sha256,...}]}}
     * @param modelId the model ID to fetch info for
     * @param calculateSize whether to calculate repo size (uses the recursive list)
     * @return MlRepoInfo object
     * @throws FileDownloadException if failed to fetch repo info
     */
    private suspend fun fetchRepoInfo(modelId: String, calculateSize: Boolean = false): MlRepoInfo {
        return withContext(DownloadCoroutineManager.downloadDispatcher) {
            runCatching {
                val modelersId = ModelIdUtils.getRepositoryPath(modelId)
                val split = modelersId.split("/".toRegex()).dropLastWhile { it.isEmpty() }.toTypedArray()
                if (split.size != 2) {
                    throw FileDownloadException("Invalid model ID format for $modelId, expected format: owner/repo")
                }

                val files = fetchModelScopeFiles(split[0], split[1])
                val tree = files.filter { it.type != "dir" && it.type != "tree" }
                val repoInfo = MlRepoInfo(
                    code = "200",
                    msg = "ok",
                    data = MlRepoData(
                        tree = tree,
                        last_commit = null,
                        commit_count = 0
                    )
                )

                // Call onRepoInfo callback with repo metadata
                val repoSize = if (calculateSize) tree.sumOf { it.size } else 0L
                callback?.onRepoInfo(modelId, 0L, repoSize)
                repoInfo
            }.getOrElse { exception ->
                Log.e(TAG, "Failed to fetch repo info for $modelId", exception)
                throw FileDownloadException("Failed to fetch repo info for $modelId: ${exception.message}")
            }
        }
    }

    /** HNN CN ET v1.2.3: raw GET against the official ModelScope repo-files API and
     *  parse into our FileInfo list (etag = Sha256, used as blob key). */
    private fun fetchModelScopeFiles(owner: String, repo: String): List<FileInfo> {
        val url = "https://modelscope.cn/api/v1/models/$owner/$repo/repo/files?Recursive=1"
        val client = okhttp3.OkHttpClient.Builder()
            .connectTimeout(30, java.util.concurrent.TimeUnit.SECONDS)
            .readTimeout(30, java.util.concurrent.TimeUnit.SECONDS)
            .build()
        val request = okhttp3.Request.Builder().url(url).header("User-Agent", "HNN-CN-ET").build()
        client.newCall(request).execute().use { response ->
            if (!response.isSuccessful) {
                throw FileDownloadException(
                    "ModelScope API request failed with code ${response.code}: ${response.message}"
                )
            }
            val body = response.body?.string() ?: throw FileDownloadException("ModelScope API response was empty")
            val json = org.json.JSONObject(body)
            val code = json.optString("Code", "")
            if (code != "200") {
                throw FileDownloadException("ModelScope API returned Code=$code, body=$body")
            }
            val data = json.optJSONObject("Data")
                ?: throw FileDownloadException("ModelScope API response missing Data")
            val filesArr = data.optJSONArray("Files") ?: org.json.JSONArray()
            val result = mutableListOf<FileInfo>()
            for (i in 0 until filesArr.length()) {
                val f = filesArr.optJSONObject(i) ?: continue
                val path = f.optString("Path", "")
                if (path.isBlank()) continue
                result.add(
                    FileInfo(
                        name = path.substringAfterLast('/'),
                        path = path,
                        type = f.optString("Type", "blob"),
                        size = f.optLong("Size", 0L),
                        is_lfs = f.optBoolean("IsLFS", false),
                        etag = f.optString("Sha256", "").ifBlank { path },
                        url = "",
                        commit = null,
                        file_scan = null
                    )
                )
            }
            return result
        }
    }

    override fun download(modelId: String) {
        Log.d(TAG, "start download  ${modelId}")
        DownloadCoroutineManager.launchDownload {
            downloadRepo(modelId)
        }
    }

    override suspend fun checkUpdate(modelId: String) {
        try {
            fetchRepoInfo(modelId, false)
        } catch (e: FileDownloadException) {
            Log.e(TAG, "Failed to check update for $modelId", e)
        }
    }

    override fun getDownloadPath(modelId: String): File {
        return getModelPath(cacheRootPath, modelId)
    }

    override fun deleteRepo(modelId: String) {
        val msModelId = ModelIdUtils.getRepositoryPath(modelId)
        val msRepoFolderName = repoFolderName(msModelId, "model")
        val msStorageFolder = File(this.cacheRootPath, msRepoFolderName)
        Log.d(TAG, "removeStorageFolder: " + msStorageFolder.absolutePath)
        if (msStorageFolder.exists()) {
            val result = deleteDirectoryRecursively(msStorageFolder)
            if (!result) {
                Log.e(TAG, "remove storageFolder" + msStorageFolder.absolutePath + " faield")
            }
        }
        val msLinkFolder = this.getDownloadPath(modelId)
        Log.d(TAG, "removeMsLinkFolder: " + msLinkFolder.absolutePath)
        msLinkFolder.delete()
    }

    override suspend fun getRepoSize(modelId: String): Long {
        return withContext(DownloadCoroutineManager.downloadDispatcher) {
            runCatching {
                val repoInfo = fetchRepoInfo(modelId, calculateSize = true)
                repoInfo.data.tree.filter { it.type != "dir" }.sumOf { it.size }
            }.getOrElse { exception ->
                Log.e(TAG, "Failed to get repo size for $modelId", exception)
                // Try to get file_size from saved market data as fallback
                try {
                    val marketSize = com.alibaba.mls.api.download.DownloadPersistentData.getMarketSizeTotal(ApplicationProvider.get(), modelId)
                    if (marketSize > 0) {
                        Log.d(TAG, "Using saved market size as fallback for $modelId: $marketSize")
                        return@withContext marketSize
                    }
                } catch (e: Exception) {
                    Log.w(TAG, "Failed to get saved market size for $modelId", e)
                }
                0L
            }
        }
    }

    private suspend fun downloadRepo(modelId: String): MlRepoInfo? {
        val modelersId = ModelIdUtils.getRepositoryPath(modelId)
        Log.d(TAG, "downloadRepo: $modelersId")
        val split = modelersId.split("/".toRegex()).dropLastWhile { it.isEmpty() }.toTypedArray()
        if (split.size != 2) {
            callback?.onDownloadFailed(modelId, FileDownloadException("getRepoInfoFailed modelId format error: $modelId"))
            return null
        }
        
        try {
            val modelInfo = fetchRepoInfo(modelId, calculateSize = true)
            callback?.onDownloadTaskAdded()
            downloadMlRepoInner(modelId, modelersId, modelInfo)
            callback?.onDownloadTaskRemoved()
            return modelInfo
        } catch (e: FileDownloadException) {
            callback?.onDownloadFailed(modelId, e)
            return null
        }
    }

    private fun getAllFiles(owner: String, repo: String, path: String, allFiles: MutableList<FileInfo>) {
        Log.d(TAG, "getAllFiles: owner: $owner path $path repo:$repo")
        kotlin.runCatching {
            val response = mlApiClient.apiService.getModelFiles(owner, repo, path).execute()
            val repoInfo = response.body()
            
            repoInfo?.data?.tree?.forEach { fileInfo ->
                allFiles.add(fileInfo)
                // If it's a directory, recursively get its files
                if (fileInfo.type == "dir") {
                    getAllFiles(owner, repo, fileInfo.path, allFiles)
                }
            }
        }.getOrElse { error ->
            Log.e(TAG, "Failed to get files for path $path: ${error.message}")
        }
    }
    private fun downloadMlRepoInner(modelId:String, modelScopeId: String, mlRepoInfo: MlRepoInfo) {
        Log.d(TAG, "downloadMlRepoInner")
        val folderLinkFile =
            File(cacheRootPath, getLastFileName(modelScopeId))
        if (folderLinkFile.exists()) {
            Log.d(TAG, "downloadMlRepoInner already exists")
            callback?.onDownloadFileFinished(modelId, folderLinkFile.absolutePath)
            return
        }
        val modelDownloader = ModelFileDownloader()
        val hasError = false
        val errorInfo = StringBuilder()
        val repoFolderName = repoFolderName(modelScopeId, "model")
        val storageFolder = File(this.cacheRootPath, repoFolderName)
        val parentPointerPath = getPointerPathParent(storageFolder, "_no_sha_")
        val downloadTaskList: List<FileDownloadTask>
        val totalAndDownloadSize = LongArray(2)
        Log.d(TAG, "downloadMlRepoInner collectMsTaskList")
        downloadTaskList = collectTaskList(
            modelScopeId,
            storageFolder,
            parentPointerPath,
            mlRepoInfo,
            totalAndDownloadSize
        )
        Log.d(TAG, "downloadMlRepoInner downloadTaskList： " + downloadTaskList.size)
        val fileDownloadListener =
            object : FileDownloadListener {
                override fun onDownloadDelta(
                    fileName: String?,
                    downloadedBytes: Long,
                    totalBytes: Long,
                    delta: Long
                ): Boolean {
                    totalAndDownloadSize[1] += delta
                    callback?.onDownloadingProgress(
                        modelId, "file", fileName,
                        totalAndDownloadSize[1], totalAndDownloadSize[0]
                    )
                    return pausedSet.contains(modelId)
                }
            }
        try {
            // HNN CN ET v1.2.4: parallel downloads with the main model file first.
            // Before this, files downloaded strictly serially — on a phone with a
            // slow connection a repo with several files took ages and often stalled.
            // Now up to 4 files download concurrently and llm.mnn/model.mnn/*.gguf
            // (the actual usable model) are queued before tokenizer/config sidecars.
            val ordered = downloadTaskList.sortedWith(
                compareByDescending<FileDownloadTask> { task ->
                    val p = task.relativePath.orEmpty().lowercase()
                    when {
                        p.endsWith(".gguf") || p.endsWith(".mnn") ||
                            p.endsWith(".safetensors") || p.endsWith(".bin") -> 2
                        p.contains("tokenizer") || p.contains("config") || p.contains("vocab") -> 1
                        else -> 0
                    }
                }.thenByDescending { it.fileMetadata?.size ?: 0L }
            )
            val executor = java.util.concurrent.Executors.newFixedThreadPool(4)
            try {
                val futures = ordered.map { task ->
                    executor.submit {
                        try {
                            // Fresh downloader per file: ModelFileDownloader is
                            // internally synchronized, so sharing one instance would
                            // serialize the parallel downloads again.
                            ModelFileDownloader().downloadFile(task, fileDownloadListener)
                            null as Throwable?
                        } catch (t: Throwable) {
                            t
                        }
                    }
                }
                for (f in futures) {
                    val err: Throwable? = f.get() as Throwable?
                    if (err != null) throw err
                }
            } finally {
                executor.shutdown()
            }
        } catch (e: DownloadPausedException) {
            pausedSet.remove(modelId)
            callback?.onDownloadPaused(modelId)
            return
        } catch (e: Exception) {
            callback?.onDownloadFailed(modelId, e)
            return
        }
        if (!hasError) {
            val folderLinkPath = folderLinkFile.absolutePath
            createSymlink(parentPointerPath.toString(), folderLinkPath)
            callback?.onDownloadFileFinished(modelId, folderLinkPath)
        } else {
            Log.e(
                TAG,
                "Errors occurred during download: $errorInfo"
            )
        }
    }

    private fun collectTaskList(
        modelScopeId: String,
        storageFolder: File,
        parentPointerPath: File,
        mlRepoInfo: MlRepoInfo,
        totalAndDownloadSize: LongArray
    ): List<FileDownloadTask> {
        val fileDownloadTasks: MutableList<FileDownloadTask> = ArrayList()
        for (i in mlRepoInfo.data.tree.indices) {
            val subFile = mlRepoInfo.data.tree[i]
            if (subFile.type == "dir") {
                continue
            }
            val fileDownloadTask = FileDownloadTask()
            fileDownloadTask.relativePath = subFile.path
            fileDownloadTask.fileMetadata = HfFileMetadata()
            // HNN CN ET v1.2.3: download via the official ModelScope file API
            // (same as the desktop build): GET .../repo?FilePath=<path> -> 200/206.
            fileDownloadTask.fileMetadata!!.location = String.format(
                "https://modelscope.cn/api/v1/models/%s/repo?FilePath=%s",
                modelScopeId,
                subFile.path
            )
            fileDownloadTask.fileMetadata!!.size = subFile.size
            fileDownloadTask.fileMetadata!!.etag = subFile.etag
            fileDownloadTask.etag = subFile.etag
            fileDownloadTask.blobPath = File(storageFolder, "blobs/" + subFile.etag)
            fileDownloadTask.blobPathIncomplete =
                File(storageFolder, "blobs/" + subFile.etag + ".incomplete")
            fileDownloadTask.pointerPath = File(parentPointerPath, subFile.path)
            fileDownloadTask.downloadedSize =
                if (fileDownloadTask.blobPath!!.exists()) fileDownloadTask.blobPath!!.length() else (if (fileDownloadTask.blobPathIncomplete!!.exists()) fileDownloadTask.blobPathIncomplete!!.length() else 0)
            totalAndDownloadSize[0] += subFile.size
            totalAndDownloadSize[1] += fileDownloadTask.downloadedSize
            fileDownloadTasks.add(fileDownloadTask)
        }
        return fileDownloadTasks
    }

}

// Created by ruoyi.sjd on 2024/12/24.
// Copyright (c) 2024 Alibaba Group Holding Limited All rights reserved.
package com.alibaba.mls.api.download

import android.content.Context
import android.util.Log
import com.alibaba.mls.api.ApplicationProvider
import com.alibaba.mls.api.download.hf.HfModelDownloader
import com.alibaba.mls.api.download.ms.MsModelDownloader
import com.alibaba.mls.api.download.DownloadCoroutineManager
import com.alibaba.mls.api.download.DownloadPersistentData
import java.io.File

/**
 * Minimal ModelDownloadManager for framework
 * Apps can extend or wrap this class for additional functionality
 */
class ModelDownloadManager(context: Context) : ModelRepoDownloader.ModelRepoDownloadCallback {

    private val appContext: Context = context.applicationContext
    // HNN CN ET v1.1.8:
    // models prefer public /storage/emulated/0/hnn (needs "All files access" on Android 11+).
    // When that grant is missing OR the check is unreliable on some ROMs, we probe real
    // writability and fall back to the app's own external dir under /storage/emulated/0
    // (Android/data/<pkg>/files/hnn) which needs NO permission at all — the download must
    // never get stuck on "please grant permission" once the user already granted access.
    private val cacheDir: String = resolveModelRoot(context)
    private val hfDownloader: HfModelDownloader
    private val msDownloader: MsModelDownloader

    private var downloadListener: DownloadListener? = null
    private val downloadListeners: MutableList<DownloadListener> = java.util.concurrent.CopyOnWriteArrayList()
    private var progressCallbackIntervalMs = 200L

    init {
        hfDownloader = HfModelDownloader(this, cacheDir)
        msDownloader = MsModelDownloader(this, cacheDir)
        // HNN CN ET v1.2.7: lite editions download with a smaller pool.
        try {
            com.alibaba.mls.api.download.ml.MLModelDownloader.liteMode = isLiteMode()
        } catch (t: Throwable) {
            Log.w("ModelDownloadManager", "liteMode init failed: ${t.message}")
        }
    }
    
    // Cache for download info to support progress and pause states
    private val downloadInfoMap = java.util.concurrent.ConcurrentHashMap<String, DownloadInfo>()

    // HNN CN ET v1.1.8: replace the API-based permission check with a REAL writability
    // probe. Some ROMs (and some users' grant combos) report isExternalStorageManager()
    // false even after the user granted access -> downloads used to get stuck at
    // "please grant permission". If the resolved root is writable we proceed no matter
    // what the API says; the resolver already fell back to a permission-free dir if not.
    private fun hasExternalStoragePermission(): Boolean {
        return try {
            val root = java.io.File(cacheDir)
            if (!root.exists() && !root.mkdirs()) return false
            if (!root.canWrite()) return false
            val probe = java.io.File(root, ".hnn_perm_probe")
            probe.writeText("ok")
            val ok = probe.exists() && probe.length() == 2L
            probe.delete()
            ok
        } catch (t: Throwable) {
            false
        }
    }
    
    fun modelRoot(): String = cacheDir

    /** HNN CN ET v1.2.7: model_downloader is a standalone module without the app
     *  BuildConfig — read the lite flag from the host app's BuildConfig via reflection. */
    fun isLiteMode(): Boolean {
        return runCatching {
            val cls = Class.forName(appContext.packageName + ".BuildConfig")
            cls.getField("LITE_MODE").getBoolean(null)
        }.getOrDefault(false)
    }

    private fun formatSize(bytes: Long): String {
        val mb = bytes / (1024.0 * 1024.0)
        return if (mb >= 1024) {
            String.format(java.util.Locale.US, "%.1f GB", mb / 1024.0)
        } else {
            String.format(java.util.Locale.US, "%.0f MB", mb)
        }
    }

    // Helper to get or create info
    private fun getOrCreateInfo(modelId: String): DownloadInfo {
        return downloadInfoMap.getOrPut(modelId) { DownloadInfo() }
    }

    fun setListener(listener: DownloadListener?) {
        if (this.downloadListener != null) {
            removeListener(this.downloadListener!!)
        }
        if (listener != null) {
            addListener(listener)
        }
        this.downloadListener = listener
    }

    fun addListener(listener: DownloadListener) {
        if (!downloadListeners.contains(listener)) {
            downloadListeners.add(listener)
        }
    }

    fun removeListener(listener: DownloadListener) {
        downloadListeners.remove(listener)
    }

    fun setDownloadListener(listener: DownloadListener?) {
        this.downloadListener = listener
    }

    /**
     * Set minimum interval (ms) between DOWNLOADING progress callbacks for the same model/stage.
     * Set 0 to disable throttling.
     */
    fun setProgressCallbackIntervalMs(intervalMs: Long) {
        progressCallbackIntervalMs = intervalMs.coerceAtLeast(0L)
    }

    fun getProgressCallbackIntervalMs(): Long = progressCallbackIntervalMs

    fun startDownload(modelId: String) {
        // v1.1.7: guard the WHOLE method so a failure at any point (permission, path,
        // pre-check, dispatch) always surfaces via the failure callback -> toast + state
        // refresh. Previously an unexpected throw here (e.g. inside isDownloaded) silently
        // killed the click -> "button does nothing".
        try {
            startDownloadInner(modelId)
        } catch (t: Throwable) {
            val e = com.alibaba.mls.api.FileDownloadException("启动下载失败: ${t.message}")
            try {
                downloadListeners.forEach { it.onDownloadFailed(modelId, e) }
            } catch (t2: Throwable) {
                Log.e("ModelDownloadManager", "failed to dispatch download error", t2)
            }
        }
    }

    private fun startDownloadInner(modelId: String) {
        // HNN CN ET v1.2.7: on 2 GB devices the system often kills the process
        // right after tapping download (low-memory killer, looks like an instant
        // crash). Refuse politely when free RAM is very low instead of dying.
        try {
            if (isLiteMode()) {
                val am = appContext.getSystemService(android.content.Context.ACTIVITY_SERVICE) as android.app.ActivityManager
                val mi = android.app.ActivityManager.MemoryInfo()
                am.getMemoryInfo(mi)
                val availMB = mi.availMem / (1024 * 1024)
                if (availMB < 120) {
                    val e = com.alibaba.mls.api.FileDownloadException(
                        "内存不足（可用 ${availMB}MB < 120MB），系统可能自动关闭应用。请关闭后台应用后重试下载。"
                    )
                    downloadListeners.forEach { it.onDownloadFailed(modelId, e) }
                    return
                }
            }
        } catch (t: Throwable) {
            Log.w("ModelDownloadManager", "low-memory guard skipped: ${t.message}")
        }
        // HNN CN ET v1.1.3: models download into public /storage/emulated/0/hnn —
        // require the external storage permission before starting any transfer.
        if (!hasExternalStoragePermission()) {
            val e = com.alibaba.mls.api.FileDownloadException(
                "需要“所有文件访问”权限才能下载模型到 /storage/emulated/0/hnn，请在系统设置中授权"
            )
            downloadListeners.forEach { it.onDownloadFailed(modelId, e) }
            return
        }
        // HNN CN ET v1.1.4: make sure the model root directory exists before downloading.
        try {
            val root = java.io.File(cacheDir)
            if (!root.exists() && !root.mkdirs()) {
                val e = com.alibaba.mls.api.FileDownloadException(
                    "无法创建模型目录 ${root.absolutePath}，请检查存储权限"
                )
                downloadListeners.forEach { it.onDownloadFailed(modelId, e) }
                return
            }
            // HNN CN ET v1.2.1: check free space before downloading so low-storage
            // devices (e.g. 10 GB phones) get a clear error instead of a silent
            // mid-download failure. Only enforce when we know the model size.
            try {
                val need = getOrCreateInfo(modelId).totalSize
                if (need > 0) {
                    val stat = android.os.StatFs(root.absolutePath)
                    val freeBytes = stat.availableBytes
                    val reserve = 200L * 1024 * 1024
                    if (freeBytes < need + reserve) {
                        val e = com.alibaba.mls.api.FileDownloadException(
                            "存储空间不足：模型需要约 ${formatSize(need + reserve)}，当前可用 ${formatSize(freeBytes)}，请清理存储后再下载"
                        )
                        downloadListeners.forEach { it.onDownloadFailed(modelId, e) }
                        return
                    }
                }
            } catch (t: Throwable) {
                // StatFs can fail on some ROMs — do not block the download for it.
                Log.w("ModelDownloadManager", "space check skipped: ${t.message}")
            }
        } catch (t: Throwable) {
            val e = com.alibaba.mls.api.FileDownloadException("创建模型目录失败: ${t.message}")
            downloadListeners.forEach { it.onDownloadFailed(modelId, e) }
            return
        }
        // HNN CN ET v1.1.5: model already fully downloaded under /storage/emulated/0/hnn?
        // Mark it success and skip the network transfer entirely (fast, no re-download).
        val alreadyInstalled = if (modelId.startsWith("HuggingFace/") || modelId.startsWith("Huggingface/")) {
            hfDownloader.isDownloaded(modelId)
        } else {
            msDownloader.isDownloaded(modelId)
        }
        if (alreadyInstalled) {
            val info = getOrCreateInfo(modelId)
            info.downloadState = DownloadState.DOWNLOAD_SUCCESS
            info.downlodaState = DownloadState.DOWNLOAD_SUCCESS
            info.progress = 1.0
            downloadListeners.forEach { it.onDownloadStart(modelId) }
            downloadListeners.forEach { it.onDownloadFinished(modelId, getDownloader(modelId).getDownloadPath(modelId).absolutePath) }
            return
        }

        // Clear stale pause flags (e.g. after deleteModel on an idle task).
        hfDownloader.clearPause(modelId)
        msDownloader.clearPause(modelId)
        progressDispatchTrackers.remove(modelId)

        // Immediately trigger preparing state to update UI
        val info = getOrCreateInfo(modelId)
        info.downloadState = DownloadState.PREPARING
        info.downlodaState = DownloadState.PREPARING
        info.progress = 0.0
        downloadListeners.forEach { it.onDownloadStart(modelId) }
        
        when {
            modelId.startsWith("HuggingFace/") || modelId.startsWith("Huggingface/") -> {
                hfDownloader.download(modelId)
            }
            modelId.startsWith("ModelScope/") -> {
                msDownloader.download(modelId)
            }
            else -> {
                // Default to ModelScope for unprefixed models
                // This supports models like "taobao-mnn/..." from ModelScope
                msDownloader.download(modelId)
            }
        }
    }

    fun isDownloading(modelId: String): Boolean {
        // Simplified - apps can override
        return false
    }

    fun pauseDownload(modelId: String) {
        hfDownloader.pause(modelId)
        msDownloader.pause(modelId)
    }

    fun pauseAllDownloads() {
        // Simplified - apps can track active downloads and pause them
    }

    fun deleteModel(modelId: String) {
        // Stop ongoing transfer first to avoid progress callbacks restoring stale state.
        pauseDownload(modelId)
        progressDispatchTrackers.remove(modelId)

        // Delete both link folder and repo storage folder via downloader-specific cleanup.
        getDownloader(modelId).deleteRepo(modelId)

        // Clear persisted and in-memory progress so subsequent getDownloadInfo() resets.
        DownloadPersistentData.removeProgress(ApplicationProvider.get(), modelId)
        downloadInfoMap.remove(modelId)
        downloadListeners.forEach { it.onDownloadFileRemoved(modelId) }
    }

    fun downloadQnnLibs(): Boolean {
        // Stub - apps can implement QNN library download if needed
        return false
    }

    fun tryStartForegroundService() {
        // Stub - apps can implement foreground service management if needed
    }

    fun checkForUpdate(modelId: String?) {
        if (modelId == null) return
        DownloadCoroutineManager.launchDownload {
             if (modelId.startsWith("HuggingFace/") || modelId.startsWith("Huggingface/")) {
                 hfDownloader.checkUpdate(modelId)
             } else {
                 // Default to ModelScope for unprefixed models
                 msDownloader.checkUpdate(modelId)
             }
        }
    }

    fun getDownloadInfo(modelId: String): DownloadInfo {
        // First check if we have active/cached info
        val cachedInfo = downloadInfoMap[modelId]
        if (cachedInfo != null) {
             // Always return cached info if it exists, regardless of state
             // This ensures modifications (like totalSize from ModelMarketViewModel) are preserved
             return cachedInfo
        }

        val isDownloaded = if (modelId.startsWith("HuggingFace/") || modelId.startsWith("Huggingface/")) {
            hfDownloader.isDownloaded(modelId)
        } else {
            // Default to ModelScope for unprefixed models
            msDownloader.isDownloaded(modelId)
        }
        
        val info = if (isDownloaded) {
            val totalSize = DownloadPersistentData.getDownloadSizeTotal(ApplicationProvider.get(), modelId)
            DownloadInfo(downloadState = DownloadState.DOWNLOAD_SUCCESS, downlodaState = DownloadState.DOWNLOAD_SUCCESS, progress = 1.0, totalSize = totalSize)
        } else {
            val totalSize = DownloadPersistentData.getDownloadSizeTotal(ApplicationProvider.get(), modelId)
            if (totalSize > 0) {
                 val savedSize = getRealDownloadSize(modelId)
                 val progress = if (totalSize > 0) savedSize.toDouble() / totalSize else 0.0
                 val state = if (savedSize > 0) DownloadState.PAUSED else DownloadState.NOT_START
                 DownloadInfo(downloadState = state, downlodaState = state, progress = progress, totalSize = totalSize, savedSize = savedSize)
            } else {
                 DownloadInfo(downloadState = DownloadState.NOT_START, downlodaState = DownloadState.NOT_START)
            }
        }
        // Cache the info so modifications are preserved
        downloadInfoMap[modelId] = info
        return info
    }

    fun getDownloadState(modelId: String): RepoDownloadSate {
        val info = getDownloadInfo(modelId)
        return RepoDownloadSate(
            modelId = modelId,
            state = info.downloadState,
            downlodaState = info.downloadState,
            progress = info.progress,
            totalSize = info.totalSize,
            savedSize = info.savedSize,
            errorMessage = info.errorMessage
        )
    }

    fun getDownloadedFile(modelId: String): File? {
        return when {
            modelId.startsWith("HuggingFace/") || modelId.startsWith("Huggingface/") -> {
                hfDownloader.getDownloadPath(modelId).takeIf { it.exists() }
            }
            modelId.startsWith("ModelScope/") -> {
                msDownloader.getDownloadPath(modelId).takeIf { it.exists() }
            }
            else -> {
                // Keep backward compatibility for unscoped ids.
                hfDownloader.getDownloadPath(modelId).takeIf { it.exists() }
                    ?: msDownloader.getDownloadPath(modelId).takeIf { it.exists() }
            }
        }
    }

    // ModelRepoDownloadCallback implementation
    override fun onDownloadFailed(modelId: String, e: Exception) {
        progressDispatchTrackers.remove(modelId)
        val info = getOrCreateInfo(modelId)
        info.downloadState = DownloadState.FAILED
        info.downlodaState = DownloadState.FAILED
        info.errorException = e
        info.errorMessage = e.message
        downloadListeners.forEach { it.onDownloadFailed(modelId, e) }
    }

    override fun onDownloadTaskAdded() {
        // No direct mapping
    }

    override fun onDownloadTaskRemoved() {
        // No direct mapping
    }

    override fun onDownloadPending(modelId: String) {
        progressDispatchTrackers.remove(modelId)
        val info = getOrCreateInfo(modelId)
        info.downloadState = DownloadState.PREPARING
        info.progressStage = "Preparing"
        downloadListeners.forEach { it.onDownloadStart(modelId) }
    }

    override fun onDownloadPaused(modelId: String) {
        progressDispatchTrackers.remove(modelId)
        val info = getOrCreateInfo(modelId)
        info.downloadState = DownloadState.PAUSED
        info.downlodaState = DownloadState.PAUSED
        persistProgressIfNeeded(modelId, info.progressStage, info.savedSize, info.totalSize, force = true)
        downloadListeners.forEach { it.onDownloadPaused(modelId) }
    }

    override fun onDownloadFileFinished(modelId: String, absolutePath: String) {
        progressDispatchTrackers.remove(modelId)
        val info = getOrCreateInfo(modelId)
        info.downloadState = DownloadState.DOWNLOAD_SUCCESS
        info.downlodaState = DownloadState.DOWNLOAD_SUCCESS
        info.progress = 1.0
        info.hasUpdate = false
        DownloadPersistentData.saveDownloadedTime(ApplicationProvider.get(), modelId, System.currentTimeMillis())
        
        // Save total size to persistent data for future retrievals
        if (info.totalSize > 0) {
            DownloadPersistentData.saveDownloadSizeTotal(ApplicationProvider.get(), modelId, info.totalSize)
        }
        if (info.savedSize > 0) {
            DownloadPersistentData.saveDownloadSizeSaved(ApplicationProvider.get(), modelId, info.savedSize)
        }
        persistTrackers.remove(modelId)

        downloadListeners.forEach { it.onDownloadFinished(modelId, absolutePath) }
    }

    // Speed tracking helper
    private data class SpeedTracker(var lastTime: Long = 0, var lastSaved: Long = 0)
    private val speedTrackers = java.util.concurrent.ConcurrentHashMap<String, SpeedTracker>()
    private data class ProgressDispatchTracker(var lastDispatchTime: Long = 0L, var lastStage: String = "")
    private val progressDispatchTrackers = java.util.concurrent.ConcurrentHashMap<String, ProgressDispatchTracker>()
    private data class PersistTracker(
        var lastPersistTimeMs: Long = 0L,
        var lastPersistSavedBytes: Long = 0L,
        var lastPersistStage: String = ""
    )
    private val persistTrackers = java.util.concurrent.ConcurrentHashMap<String, PersistTracker>()

    private fun persistProgressIfNeeded(modelId: String, stage: String, saved: Long, total: Long, force: Boolean = false) {
        if (saved <= 0 && total <= 0 && !force) return
        val now = System.currentTimeMillis()
        val tracker = persistTrackers.getOrPut(modelId) { PersistTracker() }
        val intervalElapsed = now - tracker.lastPersistTimeMs >= PERSIST_INTERVAL_MS
        val bytesAdvanced = saved - tracker.lastPersistSavedBytes >= PERSIST_MIN_DELTA_BYTES
        val stageChanged = tracker.lastPersistStage != stage
        if (force || intervalElapsed || bytesAdvanced || stageChanged) {
            if (total > 0) {
                DownloadPersistentData.saveDownloadSizeTotal(ApplicationProvider.get(), modelId, total)
            }
            if (saved > 0) {
                DownloadPersistentData.saveDownloadSizeSaved(ApplicationProvider.get(), modelId, saved)
            }
            tracker.lastPersistTimeMs = now
            tracker.lastPersistSavedBytes = saved
            tracker.lastPersistStage = stage
        }
    }

    override fun onDownloadingProgress(
        modelId: String,
        stage: String,
        currentFile: String?,
        saved: Long,
        total: Long
    ) {
        val progress = if (total > 0) saved.toDouble() / total else 0.0
        
        // Calculate speed with throttling (every 1 second)
        val currentTime = System.currentTimeMillis()
        val info = getOrCreateInfo(modelId)
        val tracker = speedTrackers.getOrPut(modelId) { SpeedTracker(currentTime, saved) }
        
        if (currentTime - tracker.lastTime >= 1000) {
           val timeDelta = currentTime - tracker.lastTime
           val bytesDelta = saved - tracker.lastSaved
           if (timeDelta > 0 && bytesDelta >= 0) {
               val speed = bytesDelta * 1000 / timeDelta
               val speedStr = if (speed < 1024) "$speed B" else if (speed < 1024 * 1024) String.format("%.2f KB", speed / 1024.0) else String.format("%.2f MB", speed / (1024.0 * 1024.0))
               info.speedInfo = "$speedStr/s"
           }
           // Update tracker
           tracker.lastTime = currentTime
           tracker.lastSaved = saved
        }
        
        info.downloadState = DownloadState.DOWNLOADING
        info.downlodaState = DownloadState.DOWNLOADING
        info.progress = progress
        info.savedSize = saved
        // Only update totalSize if it's valid (prevent flickering if total is 0 during preparing)
        if (total > 0) {
            info.totalSize = total
        }
        persistProgressIfNeeded(modelId, stage, saved, total, force = false)
        info.currentFile = currentFile
        info.progressStage = stage
        info.lastProgressUpdateTime = currentTime
        
        val dispatchTracker = progressDispatchTrackers.getOrPut(modelId) { ProgressDispatchTracker() }
        val intervalMs = progressCallbackIntervalMs
        val stageChanged = dispatchTracker.lastStage != stage
        val intervalPassed = intervalMs <= 0 || currentTime - dispatchTracker.lastDispatchTime >= intervalMs
        if (stageChanged || intervalPassed) {
            downloadListeners.forEach { it.onDownloadProgress(modelId, info) }
            dispatchTracker.lastDispatchTime = currentTime
            dispatchTracker.lastStage = stage
        }
        
        // Also trigger onDownloadStart if it's the first progress update (simplified)
        if (saved == 0L) {
             downloadListeners.forEach { it.onDownloadStart(modelId) }
        }
    }

    override fun onRepoInfo(modelId: String, lastModified: Long, repoSize: Long) {
        val info = getOrCreateInfo(modelId)
        val localKnownTotalSize = if (info.totalSize > 0L) {
            info.totalSize
        } else {
            DownloadPersistentData.getDownloadSizeTotal(ApplicationProvider.get(), modelId)
        }
        info.totalSize = repoSize
        if (repoSize > 0) {
            DownloadPersistentData.saveDownloadSizeTotal(ApplicationProvider.get(), modelId, repoSize)
        }
        info.remoteUpdateTime = lastModified
        
        if (info.downloadState == DownloadState.DOWNLOAD_SUCCESS) {
            val localDownloadedTime = getLocalDownloadedTime(modelId)
            val hasUpdate = when {
                lastModified > 0L && localDownloadedTime > 0L -> lastModified > localDownloadedTime
                lastModified <= 0L && localKnownTotalSize > 0L && repoSize > 0L -> localKnownTotalSize != repoSize
                else -> false
            }
            info.hasUpdate = hasUpdate
            if (hasUpdate) {
                downloadListeners.forEach { it.onDownloadHasUpdate(modelId, info) }
            }
        }
        
        downloadListeners.forEach { it.onDownloadTotalSize(modelId, repoSize) }
    }

    private fun getDownloader(modelId: String): ModelRepoDownloader {
        return if (modelId.startsWith("HuggingFace/") || modelId.startsWith("Huggingface/")) {
            hfDownloader
        } else {
             msDownloader
        }
    }

    fun getRealDownloadSize(modelId: String): Long {
         val savedSize = DownloadPersistentData.getDownloadSizeSaved(ApplicationProvider.get(), modelId)
         if (savedSize > 0) return savedSize
         
         val downloader = getDownloader(modelId)
         val realFile = downloader.repoModelRealFile(modelId)
         if (realFile.exists()) {
              return FileUtils.getFileSize(realFile)
         }
         return 0L
    }

    private fun getLocalDownloadedTime(modelId: String): Long {
        val persisted = DownloadPersistentData.getDownloadedTime(ApplicationProvider.get(), modelId)
        if (persisted > 0L) return persisted

        val downloadPath = getDownloader(modelId).getDownloadPath(modelId)
        return if (downloadPath.exists()) downloadPath.lastModified() else 0L
    }

    companion object {
        private const val PREFERRED_ROOT = "/storage/emulated/0/hnn"
        const val REQUEST_CODE_POST_NOTIFICATIONS = 1001  // Stub constant for foreground service notifications
        private const val PERSIST_INTERVAL_MS = 1000L
        private const val PERSIST_MIN_DELTA_BYTES = 1024L * 1024L
        @Volatile
        private var progressCallbackIntervalMs: Long = 0L

        @Volatile
        private var instance: ModelDownloadManager? = null

        // HNN CN ET v1.1.8: model root resolution with REAL writability probing.
        // Prefers /storage/emulated/0/hnn (needs "All files access" on Android 11+);
        // automatically falls back to Android/data/<pkg>/files/hnn which needs NO
        // permission, so downloads never get stuck on the permission dialog even on
        // ROMs where isExternalStorageManager() stays false after granting.
        @JvmStatic
        fun resolveModelRoot(context: Context): String {
            return try {
                val pref = java.io.File(PREFERRED_ROOT)
                if (!pref.exists()) pref.mkdirs()
                if (pref.canWrite()) {
                    // probe an actual write so "looks writable" can't hide a permission deny
                    val probe = java.io.File(pref, ".hnn_perm_probe")
                    probe.writeText("ok")
                    probe.delete()
                    PREFERRED_ROOT
                } else {
                    fallbackRoot(context)
                }
            } catch (t: Throwable) {
                fallbackRoot(context)
            }
        }

        private fun fallbackRoot(context: Context): String {
            val base = context.getExternalFilesDir(null) ?: context.filesDir
            return java.io.File(base, "hnn").absolutePath
        }

        @JvmStatic
        fun getInstance(context: Context): ModelDownloadManager {
            return instance ?: synchronized(this) {
                instance ?: ModelDownloadManager(context.applicationContext).also {
                    instance = it
                }
            }
        }

        @JvmStatic
        fun getInstance(): ModelDownloadManager {
            return getInstance(ApplicationProvider.get())
        }
    }
}

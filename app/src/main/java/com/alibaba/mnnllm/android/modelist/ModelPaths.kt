// HNN CN ET v1.1.3: Model storage migrated to external public storage /storage/emulated/0/hnn
// Copyright (c) ET. All rights reserved.
package com.alibaba.mnnllm.android.modelist

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Environment
import android.util.Log
import androidx.core.content.ContextCompat
import java.io.File

/**
 * HNN CN ET model path manager.
 *
 * Since v1.1.3 all downloaded models (and builtin copies) live in the public
 * external storage folder `/storage/emulated/0/hnn` instead of the app-private
 * `filesDir/.mnnmodels`. This keeps models visible to the user, survives app
 * uninstall and frees internal storage.
 */
object ModelPaths {

    private const val TAG = "ModelPaths"

    /** Public model root directory. */
    const val EXTERNAL_MODEL_ROOT = "/storage/emulated/0/hnn"

    /** Main model directory (downloaded models land here). */
    fun modelsDir(): File = File(EXTERNAL_MODEL_ROOT)

    /** Builtin model directory (copied from assets). */
    fun builtinModelsDir(): File = File(EXTERNAL_MODEL_ROOT, "builtin")

    /** Legacy app-private model directory used before v1.1.3. */
    fun legacyModelsDir(context: Context): File = File(context.filesDir, ".mnnmodels")

    /** True when external public storage is mounted and writable by us. */
    fun isExternalStorageReady(): Boolean =
        Environment.getExternalStorageState() == Environment.MEDIA_MOUNTED

    /**
     * True when we are allowed to read/write arbitrary files under
     * /storage/emulated/0 (MANAGE_EXTERNAL_STORAGE on API 30+,
     * WRITE_EXTERNAL_STORAGE runtime permission on API 26-28).
     */
    fun hasStoragePermission(context: Context): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Environment.isExternalStorageManager()
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            // API 29 with requestLegacyExternalStorage=true may still need the runtime grant
            ContextCompat.checkSelfPermission(
                context, Manifest.permission.WRITE_EXTERNAL_STORAGE
            ) == PackageManager.PERMISSION_GRANTED
        } else {
            ContextCompat.checkSelfPermission(
                context, Manifest.permission.WRITE_EXTERNAL_STORAGE
            ) == PackageManager.PERMISSION_GRANTED
        }
    }

    /**
     * Migrate models from the legacy app-private `filesDir/.mnnmodels` folder
     * into `/storage/emulated/0/hnn`. Idempotent: existing target folders are
     * skipped. The legacy folder is removed once empty.
     *
     * @return number of migrated model folders, or -1 on failure.
     */
    fun migrateLegacyModels(context: Context): Int {
        val legacy = legacyModelsDir(context)
        if (!legacy.exists() || !legacy.isDirectory) {
            return 0
        }
        if (!isExternalStorageReady()) {
            Log.w(TAG, "External storage not ready, skip migration")
            return -1
        }
        val targetRoot = modelsDir()
        try {
            if (!targetRoot.exists() && !targetRoot.mkdirs()) {
                Log.e(TAG, "Failed to create ${targetRoot.absolutePath}")
                return -1
            }
            var migrated = 0
            var failure = false
            legacy.listFiles()?.forEach { child ->
                if (child.isDirectory) {
                    val target = File(targetRoot, child.name)
                    if (target.exists()) {
                        // Already migrated (or a fresh model with same name): keep newest, drop old
                        Log.d(TAG, "Target exists for ${child.name}, skipping")
                        return@forEach
                    }
                    if (child.renameTo(target)) {
                        migrated++
                        Log.d(TAG, "Moved ${child.name} -> ${target.absolutePath}")
                    } else {
                        // renameTo across partitions may fail; fall back to copy+delete
                        Log.w(TAG, "renameTo failed for ${child.name}, trying copy fallback")
                        if (copyRecursively(child, target)) {
                            migrated++
                            child.deleteRecursively()
                        } else {
                            failure = true
                            Log.e(TAG, "Failed to migrate ${child.name}")
                        }
                    }
                } else if (child.isFile) {
                    // Loose file at legacy root (rare); try to move it as well
                    val target = File(targetRoot, child.name)
                    if (!target.exists()) {
                        if (child.renameTo(target)) migrated++ else failure = true
                    }
                }
            }
            if (!failure && legacy.listFiles()?.isEmpty() == true) {
                legacy.deleteRecursively()
                Log.d(TAG, "Legacy model directory removed")
            }
            Log.d(TAG, "Migration done: $migrated model(s) moved to ${targetRoot.absolutePath}")
            return if (failure) -1 else migrated
        } catch (e: Exception) {
            Log.e(TAG, "Migration failed", e)
            return -1
        }
    }

    private fun copyRecursively(src: File, dst: File): Boolean {
        return try {
            if (src.isDirectory) {
                if (!dst.exists() && !dst.mkdirs()) return false
                src.listFiles()?.forEach { child ->
                    if (!copyRecursively(child, File(dst, child.name))) return false
                }
                true
            } else {
                src.copyTo(dst, overwrite = false)
                true
            }
        } catch (e: Exception) {
            Log.e(TAG, "copyRecursively failed ${src.absolutePath} -> ${dst.absolutePath}", e)
            false
        }
    }
}

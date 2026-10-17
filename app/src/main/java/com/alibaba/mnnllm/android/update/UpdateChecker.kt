// HNN CN ET - self-update via GitHub Releases (replaces legacy modelscope manifest)
package com.alibaba.mnnllm.android.update

import android.annotation.SuppressLint
import android.app.Dialog
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.util.Log
import android.view.LayoutInflater
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.FileProvider
import com.alibaba.mnnllm.android.BuildConfig
import com.alibaba.mnnllm.android.R
import com.alibaba.mnnllm.android.chat.DownloadReceiver
import com.alibaba.mnnllm.android.utils.AppUtils.getAppVersionName
import com.alibaba.mnnllm.android.utils.PreferenceUtils
import com.alibaba.mnnllm.android.utils.UiUtils
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.progressindicator.CircularProgressIndicator
import okhttp3.Call
import okhttp3.Callback
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.net.URL
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlin.math.max

class UpdateChecker(private val context: Context) {

    // Self-update channel: this repository's latest GitHub Release.
    private val releaseApiUrl = "https://api.github.com/repos/ETQWFD/HNN-CN-ET/releases/latest"

    private val mainHandler = Handler(Looper.getMainLooper())
    private var progressDialog: Dialog? = null

    @Volatile
    private var cancelDownload = false

    private fun createIndeterminateDialog(context: Context, message: String): Dialog {
        val inflater = LayoutInflater.from(context)
        val view = inflater.inflate(R.layout.dialog_progress_material3, null)
        val indicator = view.findViewById<CircularProgressIndicator>(R.id.progress_indicator)
        val text = view.findViewById<TextView>(R.id.progress_text)
        indicator.isIndeterminate = true
        text.text = message
        val dialog = MaterialAlertDialogBuilder(context)
            .setView(view)
            .setCancelable(false)
            .create()
        dialog.show()
        return dialog
    }

    @SuppressLint("LogNotTimber")
    fun checkForUpdates(context: Context, forceCheck: Boolean) {
        // Do nothing in Google Play build
        if (BuildConfig.IS_GOOGLE_PLAY_BUILD) {
            return
        }

        // Resume pending install after user granted unknown-sources permission.
        if (waitingForInstallPermission && (waitingDownloadId > 0 || waitingFileUri != null)) {
            installApk(context, waitingDownloadId, waitingFileUri)
            waitingForInstallPermission = false
            return
        }
        if (!forceCheck) {
            val lastCheckTime = PreferenceUtils.getLong(context, "download_last_show_time", 0)
            if (System.currentTimeMillis() - lastCheckTime < 1000 * 60 * 60) {
                return
            }
        }
        if (forceCheck) {
            progressDialog = createIndeterminateDialog(context, context.getString(R.string.checking_repo_updates))
        }

        val client = OkHttpClient.Builder()
            .connectTimeout(15, TimeUnit.SECONDS)
            .readTimeout(30, TimeUnit.SECONDS)
            .build()
        val request: Request = Request.Builder()
            .url(releaseApiUrl)
            .header("Accept", "application/vnd.github+json")
            .build()

        client.newCall(request).enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) {
                Log.e(TAG, "release check network failed: ${e.message}")
                mainHandler.post {
                    progressDialog?.dismiss()
                    if (forceCheck) {
                        UiUtils.showToast(context, context.getString(R.string.get_update_info_failed), Toast.LENGTH_SHORT)
                    }
                }
            }

            override fun onResponse(call: Call, response: Response) {
                try {
                    if (!response.isSuccessful) {
                        Log.e(TAG, "release check HTTP ${response.code}")
                        mainHandler.post {
                            progressDialog?.dismiss()
                            if (forceCheck) {
                                UiUtils.showToast(context, context.getString(R.string.get_update_info_failed), Toast.LENGTH_SHORT)
                            }
                        }
                        return
                    }
                    val body = response.body?.string().orEmpty()
                    val json = JSONObject(body)
                    val latestVersion = normalizeVersion(json.optString("tag_name", ""))
                    val notes = json.optString("body", "").trim()
                    val downloadUrl = pickApkAsset(json.optJSONArray("assets"))
                    val currentVersion = getAppVersionName(context)
                    Log.d(TAG, "latest=$latestVersion current=$currentVersion url=$downloadUrl")

                    mainHandler.post { progressDialog?.dismiss() }

                    if (downloadUrl.isNullOrEmpty() || latestVersion.isEmpty()) {
                        if (forceCheck) {
                            mainHandler.post {
                                UiUtils.showToast(context, context.getString(R.string.no_update), Toast.LENGTH_SHORT)
                            }
                        }
                        return
                    }
                    if (isNewerVersion(latestVersion, currentVersion)) {
                        mainHandler.post {
                            showUpdateDialog(context, latestVersion, notes, downloadUrl)
                        }
                    } else if (forceCheck) {
                        mainHandler.post {
                            UiUtils.showToast(context, context.getString(R.string.no_update), Toast.LENGTH_SHORT)
                        }
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "release parse error", e)
                    mainHandler.post {
                        progressDialog?.dismiss()
                        if (forceCheck) {
                            UiUtils.showToast(context, context.getString(R.string.get_update_info_failed), Toast.LENGTH_SHORT)
                        }
                    }
                }
            }
        })
    }

    /** Strip leading "v" and any build metadata after "-" so "v1.2.3-beta" -> "1.2.3". */
    private fun normalizeVersion(raw: String): String {
        var v = raw.trim()
        if (v.startsWith("v", true)) v = v.substring(1)
        val dash = v.indexOf('-')
        if (dash > 0) v = v.substring(0, dash)
        return v
    }

    /** Pick the universal APK asset: prefer HNN-CN-ET / universal, skip per-ABI split APKs. */
    private fun pickApkAsset(assets: JSONArray?): String? {
        if (assets == null) return null
        var fallback: String? = null
        for (i in 0 until assets.length()) {
            val a = assets.optJSONObject(i) ?: continue
            val name = a.optString("name", "")
            val url = a.optString("browser_download_url", "")
            if (!name.endsWith(".apk", true) || url.isEmpty()) continue
            if (name.contains("arm64", true) || name.contains("armeabi", true)) continue
            if (name.contains("HNN-CN-ET", true) || name.contains("universal", true)) return url
            if (fallback == null) fallback = url
        }
        return fallback
    }

    private fun isNewerVersion(latest: String, current: String): Boolean {
        val latestParts = latest.split("\\.".toRegex()).dropLastWhile { it.isEmpty() }.toTypedArray()
        val currentParts = current.split("\\.".toRegex()).dropLastWhile { it.isEmpty() }.toTypedArray()
        val length = max(latestParts.size.toDouble(), currentParts.size.toDouble()).toInt()
        for (i in 0 until length) {
            val latestNum = latestParts.getOrNull(i)?.toIntOrNull() ?: 0
            val currentNum = currentParts.getOrNull(i)?.toIntOrNull() ?: 0
            if (latestNum > currentNum) return true
            if (latestNum < currentNum) return false
        }
        return false
    }

    private fun showUpdateDialog(context: Context, latestVersion: String, notes: String, downloadUrl: String) {
        PreferenceUtils.setLong(context, "download_last_show_time", System.currentTimeMillis())
        val message = if (notes.isBlank()) context.getString(R.string.update_notes_empty) else notes
        MaterialAlertDialogBuilder(context)
            .setTitle(context.getString(R.string.download_update_available, latestVersion))
            .setMessage(message)
            .setPositiveButton(R.string.download) { dialog, _ ->
                dialog.dismiss()
                downloadApk(context, downloadUrl)
            }
            .setNegativeButton(android.R.string.cancel) { dialog, _ -> dialog.dismiss() }
            .show()
    }

    private fun downloadApk(context: Context, downloadUrl: String) {
        if (BuildConfig.IS_GOOGLE_PLAY_BUILD) return

        val apkName = getUrlLastName(downloadUrl) ?: "HNN-CN-ET-update.apk"
        val dir = context.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS)
        val file = File(dir, apkName)

        val dlgView = LayoutInflater.from(context).inflate(R.layout.dialog_update_download, null)
        val progressBar = dlgView.findViewById<ProgressBar>(R.id.progress_update)
        val statusText = dlgView.findViewById<TextView>(R.id.tv_update_status)
        val dialog = MaterialAlertDialogBuilder(context)
            .setView(dlgView)
            .setCancelable(false)
            .setNegativeButton(android.R.string.cancel) { _, _ -> cancelDownload = true }
            .create()
        dialog.show()
        progressDialog = dialog
        cancelDownload = false

        val executor = Executors.newSingleThreadExecutor()
        executor.execute {
            try {
                downloadWithResume(context, downloadUrl, file, statusText) { done, total, speed ->
                    mainHandler.post {
                        if (!dialog.isShowing) return@post
                        if (total > 0) {
                            val pct = (done * 100.0 / total).toInt()
                            progressBar.isIndeterminate = false
                            progressBar.progress = pct
                            statusText.text = context.getString(
                                R.string.update_download_progress,
                                pct.toFloat(),
                                humanSize(done),
                                humanSize(total),
                                humanSpeed(speed)
                            )
                        } else {
                            progressBar.isIndeterminate = true
                            statusText.text = humanSize(done)
                        }
                    }
                }
                mainHandler.post {
                    if (dialog.isShowing) dialog.dismiss()
                    installFinishedApk(context, file)
                }
            } catch (e: Exception) {
                Log.e(TAG, "apk download failed", e)
                mainHandler.post {
                    if (dialog.isShowing) dialog.dismiss()
                    UiUtils.showToast(context, context.getString(R.string.update_download_failed), Toast.LENGTH_LONG)
                }
            }
        }
    }

    /**
     * Resumable download with auto-retry. Uses HTTP Range to continue a partial file when the
     * server replies 206; starts fresh on 200. Reports progress/speed via onProgress.
     */
    private fun downloadWithResume(
        context: Context,
        url: String,
        outFile: File,
        statusText: TextView,
        onProgress: (done: Long, total: Long, speed: Long) -> Unit
    ) {
        val client = OkHttpClient.Builder()
            .connectTimeout(15, TimeUnit.SECONDS)
            .readTimeout(60, TimeUnit.SECONDS)
            .build()
        val maxRetries = 5
        var attempt = 0
        var speed = 0L
        while (true) {
            if (cancelDownload) throw IOException("Cancelled")
            try {
                val existing = outFile.length()
                val builder = Request.Builder().url(url).get()
                if (existing > 0) builder.header("Range", "bytes=$existing-")
                client.newCall(builder.build()).execute().use { response ->
                    val code = response.code
                    val append = code == 206
                    if (!append && code != 200) throw IOException("HTTP $code")
                    val body = response.body ?: throw IOException("empty body")
                    val contentLen = body.contentLength()
                    val total = if (append && contentLen >= 0) existing + contentLen else contentLen
                    if (!append && existing > 0) outFile.delete()
                    RandomAccessFile(outFile, "rw").use { raf ->
                        raf.seek(if (append) existing else 0)
                        val buf = ByteArray(32 * 1024)
                        val stream = body.byteStream()
                        var read: Int
                        var windowStart = System.currentTimeMillis()
                        var windowBytes = 0L
                        while (stream.read(buf).also { read = it } != -1) {
                            if (cancelDownload) throw IOException("Cancelled")
                            raf.write(buf, 0, read)
                            windowBytes += read
                            val now = System.currentTimeMillis()
                            if (now - windowStart >= 500) {
                                speed = windowBytes * 1000 / (now - windowStart)
                                windowStart = now
                                windowBytes = 0
                            }
                            onProgress(raf.length(), total, speed)
                        }
                    }
                }
                return
            } catch (e: IOException) {
                attempt++
                if (attempt >= maxRetries || cancelDownload) throw e
                mainHandler.post {
                    statusText.text = context.getString(R.string.update_download_retrying, attempt, maxRetries)
                }
                Thread.sleep(1000L * attempt)
            }
        }
    }

    private fun installFinishedApk(context: Context, file: File) {
        val authority = "${context.packageName}.fileprovider"
        val uri = FileProvider.getUriForFile(context, authority, file)
        installApk(context, -9999, uri)
    }

    private fun humanSize(bytes: Long): String {
        if (bytes < 0) return "?"
        if (bytes < 1024) return "$bytes B"
        if (bytes < 1024 * 1024) return String.format("%.1f KB", bytes / 1024.0)
        if (bytes < 1024 * 1024 * 1024) return String.format("%.1f MB", bytes / (1024.0 * 1024.0))
        return String.format("%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0))
    }

    private fun humanSpeed(bytesPerSec: Long): String {
        if (bytesPerSec <= 0) return "0 B/s"
        return humanSize(bytesPerSec) + "/s"
    }

    companion object {
        private const val TAG = "UpdateChecker"
        private var waitingDownloadId: Long = -9999
        var waitingForInstallPermission = false
        private var waitingFileUri: Uri? = null

        fun getUrlLastName(urlStr: String?): String? {
            return try {
                val path = URL(urlStr).path
                var lastName = ""
                for (segment in path.split("/".toRegex()).dropLastWhile { it.isEmpty() }) {
                    if (segment.isNotEmpty()) lastName = segment
                }
                lastName
            } catch (e: Exception) {
                null
            }
        }

        fun installApk(context: Context, downloadId: Long, fileUri: Uri? = null) {
            if (BuildConfig.IS_GOOGLE_PLAY_BUILD) return

            if (!context.packageManager.canRequestPackageInstalls()) {
                waitingForInstallPermission = true
                waitingDownloadId = downloadId
                waitingFileUri = fileUri
                Toast.makeText(context, context.getString(R.string.please_grant_permission_install_apps), Toast.LENGTH_LONG).show()
                val intent = Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES)
                intent.data = Uri.parse("package:${context.packageName}")
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                context.startActivity(intent)
                return
            }
            val dm = context.getSystemService(Context.DOWNLOAD_SERVICE) as? android.app.DownloadManager
            val uri = fileUri ?: dm?.getUriForDownloadedFile(downloadId)
            if (uri == null) {
                Log.e(TAG, "installApk: uri is null")
                return
            }
            Log.d(TAG, "installApk uri=$uri")
            val installIntent = Intent(Intent.ACTION_INSTALL_PACKAGE)
            installIntent.setDataAndType(uri, "application/vnd.android.package-archive")
            installIntent.flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK
            context.startActivity(installIntent)
        }

        fun registerDownloadReceiver(context: Context) {
            if (BuildConfig.IS_GOOGLE_PLAY_BUILD) return
            val receiver = DownloadReceiver()
            val filter = IntentFilter(android.app.DownloadManager.ACTION_DOWNLOAD_COMPLETE)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                context.registerReceiver(receiver, filter, Context.RECEIVER_EXPORTED)
            } else {
                context.registerReceiver(receiver, filter)
            }
        }
    }
}

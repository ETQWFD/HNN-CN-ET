// HNN CN ET - unified in-app logging
// Captures Timber logs into an in-memory ring buffer and a rotating on-disk file,
// records uncaught exceptions, and supports copy/share from the logs screen.
package com.alibaba.mnnllm.android.log

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.util.Log
import androidx.core.content.FileProvider
import timber.log.Timber
import java.io.File
import java.io.FileWriter
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.atomic.AtomicBoolean

data class LogEntry(
    val time: Long,
    val level: Int,      // android.util.Log.* constant
    val tag: String,
    val message: String
)

object AppLogger {

    private const val MAX_MEMORY = 2000
    private const val LOG_DIR = "et_logs"
    private const val FILE_BASE = "app"
    private const val MAX_FILE_BYTES = 1L * 1024L * 1024L
    private const val MAX_FILES = 5
    private const val FLUSH_THRESHOLD = 32

    private val buffer = ArrayDeque<LogEntry>()
    private val listeners = mutableListOf<() -> Unit>()
    private val pendingLines = LinkedBlockingQueue<String>()
    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile
    private var appContext: Context? = null
    private var writeThread: HandlerThread? = null
    private var writeHandler: Handler? = null
    private val planted = AtomicBoolean(false)

    private val diskFmt = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US)

    fun initialize(context: Context) {
        if (!planted.compareAndSet(false, true)) return
        appContext = context.applicationContext

        writeThread = HandlerThread("et-log-writer").apply { start() }
        writeHandler = Handler(writeThread!!.looper)

        // Boot marker (captured directly so it survives regardless of Timber ordering).
        append(LogEntry(System.currentTimeMillis(), Log.INFO, "AppLifecycle", "===== HNN CN ET boot ====="))
        enqueueDisk("I/AppLifecycle", "===== HNN CN ET boot =====")

        Timber.plant(ETTree())

        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            try {
                val sw = StringWriter()
                throwable.printStackTrace(PrintWriter(sw))
                val text = "Uncaught on ${thread.name}: ${throwable.javaClass.name}: ${throwable.message}\n$sw"
                append(LogEntry(System.currentTimeMillis(), Log.ERROR, "Crash", text))
                enqueueDisk("E/Crash", text)
                flushBlocking()
            } catch (_: Throwable) {
            }
            previous?.uncaughtException(thread, throwable)
        }
    }

    /** Record an explicit, human-readable lifecycle/feature event. */
    fun event(tag: String, message: String) {
        Timber.tag(tag).i(message)
    }

    fun snapshot(): List<LogEntry> = synchronized(buffer) { buffer.toList() }

    fun addListener(l: () -> Unit) = synchronized(listeners) { listeners.add(l) }
    fun removeListener(l: () -> Unit) = synchronized(listeners) { listeners.remove(l) }

    private fun notifyListeners() = synchronized(listeners) {
        val snapshot = listeners.toList()
        // Logs may arrive on background threads; refresh UI on the main thread.
        mainHandler.post { snapshot.forEach { runCatching { it() } } }
    }

    private fun append(entry: LogEntry) {
        synchronized(buffer) {
            buffer.addLast(entry)
            while (buffer.size > MAX_MEMORY) buffer.removeFirst()
        }
        notifyListeners()
    }

    private fun levelChar(level: Int): String = when (level) {
        Log.VERBOSE -> "V"
        Log.DEBUG -> "D"
        Log.INFO -> "I"
        Log.WARN -> "W"
        Log.ERROR -> "E"
        else -> "A"
    }

    private fun enqueueDisk(levelTag: String, message: String) {
        pendingLines.offer("${diskFmt.format(Date())} $levelTag: $message\n")
        if (pendingLines.size >= FLUSH_THRESHOLD) scheduleFlush()
    }

    private fun scheduleFlush() {
        writeHandler?.post {
            try {
                val lines = ArrayList<String>()
                pendingLines.drainTo(lines)
                if (lines.isEmpty()) return@post
                val dir = logDir()
                val file = currentFile(dir)
                FileWriter(file, true).buffered().use { w ->
                    lines.forEach { w.write(it) }
                }
            } catch (_: Exception) {
            }
        }
    }

    private fun flushBlocking() {
        try {
            val lines = ArrayList<String>()
            pendingLines.drainTo(lines)
            if (lines.isEmpty()) return
            val dir = logDir()
            val file = currentFile(dir)
            FileWriter(file, true).buffered().use { w -> lines.forEach { w.write(it) } }
        } catch (_: Exception) {
        }
    }

    private fun logDir(): File {
        val ctx = appContext ?: throw IllegalStateException("AppLogger not initialized")
        val dir = File(ctx.filesDir, LOG_DIR)
        if (!dir.exists()) dir.mkdirs()
        return dir
    }

    private fun currentFile(dir: File): File {
        val active = File(dir, "$FILE_BASE.log")
        if (active.exists() && active.length() >= MAX_FILE_BYTES) {
            // rotate: app.4 -> delete, app.3 -> app.4, ..., app.log -> app.1
            for (i in (MAX_FILES - 1) downTo 1) {
                val src = File(dir, "$FILE_BASE.$i")
                val dst = File(dir, "$FILE_BASE.${i + 1}")
                if (dst.exists()) dst.delete()
                if (src.exists()) src.renameTo(dst)
            }
            active.renameTo(File(dir, "$FILE_BASE.1"))
        }
        return active
    }

    fun clear() {
        synchronized(buffer) { buffer.clear() }
        notifyListeners()
    }

    fun buildShareText(): String {
        val sb = StringBuilder()
        sb.append("HNN CN ET - App Logs\n")
        sb.append("Device: ${android.os.Build.MANUFACTURER} ${android.os.Build.MODEL} (API ${android.os.Build.VERSION.SDK_INT})\n")
        sb.append("App version: ${runCatching { appContext?.packageManager?.getPackageInfo(appContext!!.packageName, 0)?.versionName }.getOrNull() ?: "?"}\n")
        sb.append("=".repeat(60)).append("\n")
        snapshot().forEach { e ->
            sb.append(diskFmt.format(Date(e.time)))
                .append(' ').append(levelChar(e.level)).append('/').append(e.tag)
                .append(": ").append(e.message).append('\n')
        }
        return sb.toString()
    }

    fun copyAll(context: Context): String {
        val text = buildShareText()
        val cm = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        cm.setPrimaryClip(ClipData.newPlainText("HNN-CN-ET-logs", text))
        return text
    }

    /** Build a share intent pointing at a cached log file via FileProvider. */
    fun buildShareIntent(context: Context): Intent? {
        return try {
            val cacheDir = File(context.cacheDir, "shared_images")
            if (!cacheDir.exists()) cacheDir.mkdirs()
            val out = File(cacheDir, "hnn_cn_et_logs.txt")
            out.writeText(buildShareText())
            val uri = FileProvider.getUriForFile(
                context,
                "${context.packageName}.fileprovider",
                out
            )
            Intent(Intent.ACTION_SEND).apply {
                type = "text/plain"
                putExtra(Intent.EXTRA_STREAM, uri)
                putExtra(Intent.EXTRA_SUBJECT, "HNN CN ET Logs")
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
        } catch (e: Exception) {
            Log.e("AppLogger", "buildShareIntent failed", e)
            null
        }
    }

    private class ETTree : Timber.DebugTree() {
        override fun log(priority: Int, tag: String?, message: String, t: Throwable?) {
            val resolvedTag = tag ?: "App"
            val fullMessage = if (t != null) {
                val sw = StringWriter()
                t.printStackTrace(PrintWriter(sw))
                "$message\n$sw"
            } else message
            append(LogEntry(System.currentTimeMillis(), priority, resolvedTag, fullMessage))
            enqueueDisk("${levelChar(priority)}/$resolvedTag", fullMessage)
        }
    }
}

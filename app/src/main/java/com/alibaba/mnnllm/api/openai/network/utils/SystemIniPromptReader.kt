package com.alibaba.mnnllm.api.openai.network.utils

import com.alibaba.mnnllm.android.MnnLlmApplication
import com.alibaba.mnnllm.android.modelist.ModelPaths
import timber.log.Timber
import java.io.File

/**
 * HNN CN ET v1.3.4: read the user-custom system prompt from system.ini.
 *
 * Resolution order (first non-empty wins):
 *  1. <public model root>/system.ini  — [ModelPaths.modelsDir()], e.g.
 *     /storage/emulated/0/hnn/system.ini (no hard-coded absolute path here).
 *  2. fallback: <app filesDir>/system.ini.
 *
 * Missing / empty / unreadable file -> returns "" and the caller's existing
 * merge logic runs unchanged. Never throws, no UI feedback.
 *
 * File format: one or more lines of
 *     system="提示语"
 * Spaces around "=" are tolerated, Chinese curly quotes “...” are accepted.
 * Content inside the paired quotes of every matching line is collected
 * (joined by newline) and trimmed.
 */
object SystemIniPromptReader {

    private const val FILE_NAME = "system.ini"
    private const val TAG = "SystemIniPrompt"

    fun read(): String {
        return try {
            val context = MnnLlmApplication.getAppContext()
            val primary = File(ModelPaths.modelsDir(), FILE_NAME)
            parseFile(primary).ifEmpty {
                parseFile(File(context.filesDir, FILE_NAME))
            }
        } catch (t: Throwable) {
            Timber.tag(TAG).w(t, "system.ini read failed, falling back to default prompt")
            ""
        }
    }

    private fun parseFile(file: File): String {
        if (!file.exists() || !file.isFile || file.length() == 0L) return ""
        return file.useLines { sequence ->
            sequence.mapNotNull { line -> extractValue(line) }
                .joinToString("\n")
                .trim()
        }
    }

    /** Extract the content inside the paired quotes of a single `system = "..."` line. */
    private fun extractValue(line: String): String? {
        val trimmed = line.trim()
        val eq = trimmed.indexOf('=')
        if (eq <= 0) return null
        val key = trimmed.substring(0, eq).trim()
        if (!key.equals("system", ignoreCase = true)) return null

        var value = trimmed.substring(eq + 1).trim()
        if (value.length >= 2) {
            val openers = setOf('"', '“')
            val closers = setOf('"', '”')
            if (value.first() in openers && value.last() in closers) {
                value = value.substring(1, value.length - 1).trim()
            }
        }
        return value.ifBlank { null }
    }
}

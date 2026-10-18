// Created by ruoyi.sjd on 2025/2/6.
// Copyright (c) 2024 Alibaba Group Holding Limited All rights reserved.
// Modifications Copyright (c) 2026 ET (HNN CN ET)
package com.alibaba.mnnllm.android.utils

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.widget.Toast
import com.alibaba.mnnllm.android.R

object GithubUtils {
    private const val repoGithubUrl = "https://github.com/ETQWFD/HNN-CN-ET"
    private const val feedbackEmail = "2416444244@qq.com"

    fun openInBrowser(context: Context, url: String?) {
        val intent = Intent(Intent.ACTION_VIEW, Uri.parse(url))
        context.startActivity(intent)
    }

    fun starProject(context: Context) {
        openInBrowser(context, repoGithubUrl)
    }

    /**
     * Open the repository page for HNN CN ET (used by the Star dialog).
     */
    fun openRepo(context: Context) {
        openInBrowser(context, repoGithubUrl)
    }

    /**
     * Send feedback by email to the HNN CN ET ops team.
     * The email subject carries version + device info so users can describe the issue clearly.
     */
    fun reportIssue(context: Context) {
        openFeedbackEmail(context)
    }

    /**
     * Build the diagnostic body (version, device, ABI, Android version) and open the mail client.
     * If no mail client is available, copy the address to the clipboard instead of crashing.
     */
    fun openFeedbackEmail(context: Context, extraBody: String = "") {
        val sb = StringBuilder()
        sb.append("请描述你遇到的问题（复现步骤、期望结果、实际结果）：\n\n")
        sb.append("---- 诊断信息（请保留，便于定位问题）----\n")
        sb.append("App: HNN CN ET ").append(AppUtils.getAppVersionName(context))
        sb.append(" (").append(AppUtils.getAppVersionCode(context)).append(")\n")
        sb.append("设备: ").append(Build.MANUFACTURER).append(" ").append(Build.MODEL)
        sb.append(" (API ").append(Build.VERSION.SDK_INT).append(")\n")
        sb.append("Android: ").append(Build.VERSION.RELEASE)
        sb.append("\nABI: ").append(AppUtils.getAbiSummary())
        if (extraBody.isNotBlank()) {
            sb.append("\n").append(extraBody)
        }

        val subject = context.getString(R.string.feedback_email_subject)
        val mailIntent = Intent(Intent.ACTION_SENDTO).apply {
            data = Uri.parse("mailto:$feedbackEmail")
            putExtra(Intent.EXTRA_SUBJECT, subject)
            putExtra(Intent.EXTRA_TEXT, sb.toString())
        }
        try {
            context.startActivity(Intent.createChooser(mailIntent, context.getString(R.string.feedback_email_chooser)))
        } catch (e: Exception) {
            // No mail client: copy the address to clipboard so the user can still contact us.
            val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            clipboard.setPrimaryClip(ClipData.newPlainText("HNN CN ET feedback email", feedbackEmail))
            Toast.makeText(
                context,
                context.getString(R.string.feedback_no_mail_app),
                Toast.LENGTH_LONG
            ).show()
        }
    }
}

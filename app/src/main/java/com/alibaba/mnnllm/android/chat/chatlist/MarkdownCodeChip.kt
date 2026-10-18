// HNN CN ET v1.0.3: per-code-block "copy code" chip.
// After Markwon renders a message, we scan the source markdown for fenced code
// blocks and append a right-aligned clickable "复制代码" chip after each block.
// Tapping the chip copies only that code block to the clipboard.
package com.alibaba.mnnllm.android.chat.chatlist

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Typeface
import android.text.Layout
import android.text.Spanned
import android.text.SpannableStringBuilder
import android.text.TextPaint
import android.text.method.LinkMovementMethod
import android.text.style.AlignmentSpan
import android.text.style.BackgroundColorSpan
import android.text.style.ClickableSpan
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat
import io.noties.markwon.Markwon
import com.alibaba.mnnllm.android.R

object MarkdownCodeChip {

    /** Extract fenced code blocks (``` or ~~~) from a markdown source. */
    fun extractCodeBlocks(source: String?): List<String> {
        if (source.isNullOrBlank()) return emptyList()
        val blocks = mutableListOf<String>()
        val pattern = Regex("(?s)```[^`\\n]*\\n(.*?)```|~~~[^\\n]*\\n(.*?)~~~")
        for (m in pattern.findAll(source)) {
            val content = m.groupValues[1].ifBlank { m.groupValues[2] }
            if (content.isNotBlank()) blocks.add(content)
        }
        return blocks
    }

    /**
     * Render [source] markdown into [view] via [markdown], appending a
     * clickable "复制代码" chip after every fenced code block.
     */
    fun render(markdown: Markwon, view: TextView, source: String?) {
        val sourceText = source ?: ""
        val base: Spanned = markdown.toMarkdown(sourceText)
        val blocks = extractCodeBlocks(sourceText)
        if (blocks.isEmpty()) {
            view.text = base
            return
        }
        val builder = SpannableStringBuilder(base)
        val context = view.context
        val chipText = context.getString(R.string.copy_code)
        val chipLabel = "  $chipText  "
        val chipColor = ContextCompat.getColor(context, R.color.app_theme_color)
        val chipBg = ContextCompat.getColor(context, R.color.code_chip_bg)
        var searchFrom = 0
        for (content in blocks) {
            val needle = content.trim()
            if (needle.isEmpty()) continue
            val idx = builder.toString().indexOf(needle, searchFrom)
            if (idx < 0) continue
            val end = idx + needle.length
            // insert "\n" + chip after the code block content
            builder.insert(end, "\n")
            val chipStart = end + 1
            builder.insert(chipStart, chipLabel)
            val chipEnd = chipStart + chipLabel.length
            val click = object : ClickableSpan() {
                override fun onClick(widget: android.view.View) {
                    copyCode(context, content)
                }

                override fun updateDrawState(ds: TextPaint) {
                    super.updateDrawState(ds)
                    ds.color = chipColor
                    ds.isUnderlineText = false
                    ds.bgColor = chipBg
                    ds.typeface = Typeface.DEFAULT_BOLD
                }
            }
            builder.setSpan(click, chipStart, chipEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            builder.setSpan(
                ForegroundColorSpan(chipColor),
                chipStart, chipEnd,
                Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
            )
            builder.setSpan(
                BackgroundColorSpan(chipBg),
                chipStart, chipEnd,
                Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
            )
            builder.setSpan(
                StyleSpan(Typeface.BOLD),
                chipStart, chipEnd,
                Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
            )
            builder.setSpan(
                AlignmentSpan.Standard(Layout.Alignment.ALIGN_OPPOSITE),
                chipStart, chipEnd,
                Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
            )
            searchFrom = chipEnd
        }
        view.text = builder
        view.movementMethod = LinkMovementMethod.getInstance()
    }

    fun copyCode(context: Context, code: String) {
        val cm = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        cm.setPrimaryClip(ClipData.newPlainText("code", code))
        Toast.makeText(context, context.getString(R.string.code_copied), Toast.LENGTH_SHORT).show()
    }
}

// HNN CN ET v1.0.9: theme color + wallpaper decoration helpers.
package com.alibaba.mnnllm.android.chat

import android.app.Activity
import android.graphics.Color
import android.graphics.PorterDuff
import android.view.View
import android.widget.ImageView
import com.alibaba.mnnllm.android.mainsettings.MainSettings
import java.io.File

object ThemeDecor {

    /** Accent colors for the preset palette. */
    fun accentColor(activity: Activity): Int {
        return when (MainSettings.getThemeColor(activity)) {
            "green" -> 0xFF00B38A.toInt()
            "red" -> 0xFFE5484D.toInt()
            "yellow" -> 0xFFF5A623.toInt()
            "purple" -> 0xFF8B5CF6.toInt()
            "cyan" -> 0xFF06B6D4.toInt()
            else -> 0xFF2E6BFF.toInt() // blue default
        }
    }

    /**
     * Insert the wallpaper as the bottom-most layer of [root], at ~50% opacity
     * with a translucent surface overlay for a frosted-glass feel. Returns the
     * created background view (or null when no wallpaper is set).
     */
    fun applyWallpaper(activity: Activity, root: android.view.ViewGroup): View? {
        val path = MainSettings.getWallpaperPath(activity) ?: return null
        val file = File(path)
        if (!file.exists()) return null
        return try {
            val bg = ImageView(activity).apply {
                setImageURI(android.net.Uri.fromFile(file))
                scaleType = ImageView.ScaleType.CENTER_CROP
                alpha = 0.5f
            }
            root.addView(bg, 0, android.view.ViewGroup.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.MATCH_PARENT
            ))
            // translucent overlay → frosted glass feel
            val overlay = View(activity).apply {
                setBackgroundColor(
                    if (isDark(activity)) Color.argb(120, 10, 12, 18)
                    else Color.argb(110, 250, 250, 252)
                )
            }
            root.addView(overlay, 1, android.view.ViewGroup.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.MATCH_PARENT
            ))
            overlay
        } catch (e: Exception) {
            null
        }
    }

    fun tintSendButton(activity: Activity, btnSend: ImageView) {
        try {
            val color = if (MainSettings.getThemeColor(activity) == "blue") {
                Color.argb(255, 46, 107, 255)
            } else accentColor(activity)
            btnSend.setColorFilter(color, PorterDuff.Mode.SRC_IN)
        } catch (e: Exception) {
            // ignore
        }
    }

    fun isDark(activity: Activity): Boolean {
        val mode = activity.resources.configuration.uiMode and
                android.content.res.Configuration.UI_MODE_NIGHT_MASK
        return mode == android.content.res.Configuration.UI_MODE_NIGHT_YES
    }
}

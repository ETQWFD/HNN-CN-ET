// HNN CN ET v1.3.0 — draggable floating ball shown while the model API
// service is running. Copyright (c) 2026 ET.
package com.alibaba.mnnllm.android.utils

import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.graphics.PixelFormat
import android.graphics.Typeface
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.TextView
import com.alibaba.mnnllm.android.R

/**
 * Draggable floating circle for the server edition. While the model API
 * service is running it sits on top of every app; the user can drag it
 * anywhere (放置) and tap it to stop the service (关闭). Tapping it also
 * offers to open the server panel.
 */
class FloatingServiceBall(private val context: Context) {

    companion object {
        const val TAG = "FloatingBall"
    }

    private val wm: WindowManager =
        context.getSystemService(Context.WINDOW_SERVICE) as WindowManager
    private var ball: View? = null
    private var visible = false
    private var downX = 0f
    private var downY = 0f
    private var moved = false

    fun canShow(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            Settings.canDrawOverlays(context)
        } else true
    }

    fun show() {
        if (visible) return
        if (!canShow()) return
        try {
            val tv = TextView(context).apply {
                text = "HNN"
                setTextColor(0xFFFFFFFF.toInt())
                textSize = 13f
                typeface = Typeface.DEFAULT_BOLD
                gravity = Gravity.CENTER
                setBackgroundResource(R.drawable.ic_ball_background)
            }
            val size = dp(52)
            val params = WindowManager.LayoutParams(
                size, size,
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                } else {
                    @Suppress("DEPRECATION")
                    WindowManager.LayoutParams.TYPE_PHONE
                },
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
                PixelFormat.TRANSLUCENT
            ).apply {
                gravity = Gravity.TOP or Gravity.START
                x = dp(12)
                y = dp(160)
            }
            tv.setOnTouchListener { v, event ->
                when (event.action) {
                    MotionEvent.ACTION_DOWN -> {
                        downX = event.rawX
                        downY = event.rawY
                        moved = false
                        v.performClick()
                        true
                    }
                    MotionEvent.ACTION_MOVE -> {
                        val dx = event.rawX - downX
                        val dy = event.rawY - downY
                        if (kotlin.math.abs(dx) > 4f || kotlin.math.abs(dy) > 4f) {
                            moved = true
                            params.x += dx.toInt()
                            params.y += dy.toInt()
                            downX = event.rawX
                            downY = event.rawY
                            runCatching { wm.updateViewLayout(v, params) }
                        }
                        true
                    }
                    MotionEvent.ACTION_UP -> {
                        if (!moved) {
                            onTap()
                        }
                        true
                    }
                    else -> true
                }
            }
            tv.setOnClickListener { }
            wm.addView(tv, params)
            ball = tv
            visible = true
        } catch (t: Throwable) {
            android.util.Log.w(TAG, "show failed", t)
        }
    }

    private fun onTap() {
        try {
            AlertDialog.Builder(context)
                .setTitle("HNN 服务运行中")
                .setMessage("模型 API 服务正在运行。可拖动本圆球放置到任意位置。\n\n停止服务，或打开服务面板查看地址/密钥？")
                .setPositiveButton("停止服务") { _, _ ->
                    stopServerFromBall()
                }
                .setNegativeButton("打开服务面板", null)
                .show()
        } catch (t: Throwable) {
            android.util.Log.w(TAG, "dialog failed", t)
        }
    }

    private fun stopServerFromBall() {
        runCatching {
            val intent = Intent(context, com.alibaba.mnnllm.android.mainsettings.ModelServerActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                .putExtra("action", "stop")
            context.startActivity(intent)
            hide()
        }.onFailure { t ->
            android.util.Log.w(TAG, "stop failed", t)
        }
    }

    fun hide() {
        if (!visible) return
        try {
            ball?.let { wm.removeView(it) }
        } catch (t: Throwable) {
            android.util.Log.w(TAG, "hide failed", t)
        }
        ball = null
        visible = false
    }

    private fun dp(v: Int): Int {
        return (v * context.resources.displayMetrics.density).toInt()
    }
}

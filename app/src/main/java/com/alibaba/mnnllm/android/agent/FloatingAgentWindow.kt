// HNN CN ET v1.0.3: Agent floating window.
// When Agent mode is on and the user leaves the chat screen, a small draggable
// "H" bubble stays on screen so the Agent keeps running and is one tap away.
package com.alibaba.mnnllm.android.agent

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.graphics.PixelFormat
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.util.Log
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.TextView
import android.widget.Toast
import com.alibaba.mnnllm.android.R

object FloatingAgentWindow {

    private const val TAG = "FloatingAgentWindow"

    @Volatile
    private var windowView: View? = null
    private var windowManager: WindowManager? = null
    private var params: WindowManager.LayoutParams? = null

    fun canShow(context: Context): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            Settings.canDrawOverlays(context)
        } else true
    }

    /** Open the system overlay-permission page. */
    fun openOverlaySettings(context: Context) {
        try {
            val intent = Intent(
                Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:${context.packageName}")
            )
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(intent)
        } catch (e: Exception) {
            Toast.makeText(context, "无法打开悬浮窗设置", Toast.LENGTH_LONG).show()
        }
    }

    /**
     * Show the floating bubble. If the overlay permission is missing,
     * open the system settings so the user can grant it.
     */
    fun show(context: Context) {
        hide()
        if (!canShow(context)) {
            Log.d(TAG, "Overlay permission missing, opening settings")
            openOverlaySettings(context)
            return
        }
        try {
            val wm = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager
            val view = TextView(context).apply {
                text = "H"
                textSize = 20f
                setTextColor(0xFFFFFFFF.toInt())
                gravity = Gravity.CENTER
                background = GradientDrawable().apply {
                    shape = GradientDrawable.OVAL
                    setColor(0xFF00B38A.toInt())
                }
                // 48dp circle
                layoutParams = android.view.ViewGroup.LayoutParams(dp(48), dp(48))
                contentDescription = "HNN CN ET Agent 运行中"
            }
            val wmParams = WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
                PixelFormat.TRANSLUCENT
            ).apply {
                gravity = Gravity.TOP or Gravity.START
                x = dp(16)
                y = dp(120)
            }
            // Draggable
            var lastX = 0
            var lastY = 0
            view.setOnTouchListener(object : View.OnTouchListener {
                private var downX = 0f
                private var downY = 0f
                private var moved = false
                @SuppressLint("ClickableViewAccessibility")
                override fun onTouch(v: View, event: MotionEvent): Boolean {
                    when (event.actionMasked) {
                        MotionEvent.ACTION_DOWN -> {
                            lastX = event.rawX.toInt()
                            lastY = event.rawY.toInt()
                            downX = event.rawX
                            downY = event.rawY
                            moved = false
                            return true
                        }
                        MotionEvent.ACTION_MOVE -> {
                            val dx = event.rawX.toInt() - lastX
                            val dy = event.rawY.toInt() - lastY
                            wmParams.x += dx
                            wmParams.y += dy
                            lastX = event.rawX.toInt()
                            lastY = event.rawY.toInt()
                            if (kotlin.math.abs(event.rawX - downX) > 8 || kotlin.math.abs(event.rawY - downY) > 8) {
                                moved = true
                            }
                            wm.updateViewLayout(v, wmParams)
                            return true
                        }
                        MotionEvent.ACTION_UP -> {
                            if (!moved) {
                                openMain(context)
                            }
                            return true
                        }
                    }
                    return false
                }
            })
            wm.addView(view, wmParams)
            windowView = view
            windowManager = wm
            params = wmParams
            Toast.makeText(context, "Agent 悬浮窗已开启", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Log.e(TAG, "Failed to show floating window: ${e.message}")
        }
    }

    private fun openMain(context: Context) {
        try {
            val launch = context.packageManager.getLaunchIntentForPackage(context.packageName)
            launch?.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
            context.startActivity(launch)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to reopen app: ${e.message}")
        }
    }

    fun hide() {
        try {
            windowView?.let { v ->
                windowManager?.removeView(v)
            }
        } catch (e: Exception) {
            // already removed
        }
        windowView = null
        windowManager = null
        params = null
    }

    private fun dp(value: Int): Int {
        val density = android.content.res.Resources.getSystem().displayMetrics.density
        return (value * density).toInt()
    }
}

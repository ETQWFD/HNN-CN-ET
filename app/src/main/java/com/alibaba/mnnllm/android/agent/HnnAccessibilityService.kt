// HNN CN ET v1.0.3: Agent accessibility service.
// Lets the Agent operate other apps after the user grants the
// accessibility permission: open apps, click elements by text,
// type text, press back/home.
package com.alibaba.mnnllm.android.agent

import android.accessibilityservice.AccessibilityService
import android.content.Context
import android.content.Intent
import android.provider.Settings
import android.util.Log
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.Toast

class HnnAccessibilityService : AccessibilityService() {

    companion object {
        private const val TAG = "HnnA11y"

        @Volatile
        var instance: HnnAccessibilityService? = null
            private set

        /** Check whether the service is enabled in system settings. */
        fun isEnabled(context: Context): Boolean {
            val expected = context.packageName + "/" + HnnAccessibilityService::class.java.name
            val enabled = Settings.Secure.getString(
                context.contentResolver,
                Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES
            ) ?: ""
            return enabled.split(':').any { it.equals(expected, ignoreCase = true) }
        }

        /** Open the system accessibility settings page for the user to grant permission. */
        fun openSettings(context: Context) {
            try {
                val intent = Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                context.startActivity(intent)
            } catch (e: Exception) {
                Toast.makeText(context, "无法打开无障碍设置", Toast.LENGTH_LONG).show()
            }
        }
    }

    override fun onServiceConnected() {
        super.onServiceConnected()
        instance = this
        Log.d(TAG, "HnnAccessibilityService connected")
    }

    override fun onDestroy() {
        instance = null
        super.onDestroy()
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {
        // No-op: actions are performed on demand by the Agent engine.
    }

    override fun onInterrupt() {
    }

    /** Find the first node whose text or content-desc contains [text]. */
    fun findNodeByText(text: String): AccessibilityNodeInfo? {
        val root = rootInActiveWindow ?: return null
        return findNode(root, text)
    }

    private fun findNode(node: AccessibilityNodeInfo, text: String): AccessibilityNodeInfo? {
        if (node == null) return null
        val label = node.text?.toString() ?: ""
        val desc = node.contentDescription?.toString() ?: ""
        if (label.contains(text, ignoreCase = true) || desc.contains(text, ignoreCase = true)) {
            return node
        }
        for (i in 0 until node.childCount) {
            val child = node.getChild(i)
            if (child != null) {
                val found = findNode(child, text)
                if (found != null) return found
            }
        }
        return null
    }

    /** Click the first node matching [text]. Returns true on success. */
    fun clickByText(text: String): Boolean {
        val node = findNodeByText(text) ?: return false
        val actionable = node.isClickable || node.parent?.isClickable == true
        val target = if (actionable) node else node.parent
        if (target != null) {
            target.performAction(AccessibilityNodeInfo.ACTION_CLICK)
            return true
        }
        return false
    }

    /** Type [text] into the first editable text node. Returns true on success. */
    fun typeText(text: String): Boolean {
        val root = rootInActiveWindow ?: return false
        val editable = findEditable(root)
        if (editable != null) {
            val bundle = android.os.Bundle().apply {
                putString(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, text)
            }
            editable.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, bundle)
            return true
        }
        return false
    }

    private fun findEditable(node: AccessibilityNodeInfo): AccessibilityNodeInfo? {
        if (node.isEditable) return node
        for (i in 0 until node.childCount) {
            val child = node.getChild(i)
            if (child != null) {
                val found = findEditable(child)
                if (found != null) return found
            }
        }
        return null
    }

    /** Click a send-like button (text 发送/Send or content-desc containing send). */
    fun clickSend(): Boolean {
        val root = rootInActiveWindow ?: return false
        val node = findSendNode(root)
        if (node != null) {
            node.performAction(AccessibilityNodeInfo.ACTION_CLICK)
            return true
        }
        return false
    }

    private fun findSendNode(node: AccessibilityNodeInfo): AccessibilityNodeInfo? {
        val label = node.text?.toString() ?: ""
        val desc = node.contentDescription?.toString() ?: ""
        if (label == "发送" || label.equals("Send", true) || desc.contains("send", true) ||
            desc == "发送" || label.contains("send", true)
        ) {
            return if (node.isClickable) node else node.parent
        }
        for (i in 0 until node.childCount) {
            val child = node.getChild(i)
            if (child != null) {
                val found = findSendNode(child)
                if (found != null) return found
            }
        }
        return null
    }

    fun goBack(): Boolean = performGlobalAction(GLOBAL_ACTION_BACK)
    fun goHome(): Boolean = performGlobalAction(GLOBAL_ACTION_HOME)

    /** Best-effort: tap the first clickable node after launching a media app. */
    fun tapFirstClickable(): Boolean {
        val root = rootInActiveWindow ?: return false
        val node = findFirstClickable(root)
        return if (node != null) {
            node.performAction(AccessibilityNodeInfo.ACTION_CLICK)
            true
        } else false
    }

    private fun findFirstClickable(node: AccessibilityNodeInfo): AccessibilityNodeInfo? {
        if (node.isClickable && node.isVisibleToUser) return node
        for (i in 0 until node.childCount) {
            val child = node.getChild(i)
            if (child != null) {
                val found = findFirstClickable(child)
                if (found != null) return found
            }
        }
        return null
    }
}

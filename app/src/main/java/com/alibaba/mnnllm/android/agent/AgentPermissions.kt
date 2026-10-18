// HNN CN ET v1.0.3: Agent runtime permission helper.
// Requests storage access (runtime on old Android, MANAGE_EXTERNAL_STORAGE on
// Android 11+) and overlay permission (for the floating window) when Agent mode
// is enabled or the Agent needs to act.
package com.alibaba.mnnllm.android.agent

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.Settings
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.alibaba.mnnllm.android.R

object AgentPermissions {

    const val REQUEST_CODE_STORAGE = 4001

    fun hasStorage(context: Context): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Environment.isExternalStorageManager()
        } else {
            ContextCompat.checkSelfPermission(context, Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
        }
    }

    /** Request storage access. Shows the system dialog / settings page. */
    fun requestStorage(activity: Activity) {
        if (hasStorage(activity)) return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                val intent = Intent(
                    Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:${activity.packageName}")
                )
                activity.startActivity(intent)
            } catch (e: Exception) {
                val fallback = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                activity.startActivity(fallback)
            }
        } else {
            ActivityCompat.requestPermissions(
                activity,
                arrayOf(
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
                ),
                REQUEST_CODE_STORAGE
            )
        }
    }

    /** Request overlay permission for the floating window. */
    fun requestOverlay(activity: Activity) {
        if (FloatingAgentWindow.canShow(activity)) return
        AlertDialog.Builder(activity)
            .setTitle(R.string.agent_mode_title)
            .setMessage(R.string.agent_overlay_request)
            .setPositiveButton(R.string.agent_enable_accessibility) { _, _ ->
                FloatingAgentWindow.openOverlaySettings(activity)
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    /** Request both storage and overlay (used when Agent mode is enabled). */
    fun requestAll(activity: Activity) {
        requestStorage(activity)
        requestOverlay(activity)
    }

    fun showStorageDenied(context: Context) {
        Toast.makeText(context, R.string.agent_storage_needed, Toast.LENGTH_LONG).show()
    }
}

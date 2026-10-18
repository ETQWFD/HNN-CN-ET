// HNN CN ET v1.0.2: announcement system.
// The backend publishes announcement.json on the official website;
// the app checks it at startup and shows each new announcement only once.
package com.alibaba.mnnllm.android.update

import android.app.Activity
import android.content.Context
import android.util.Log
import androidx.appcompat.app.AlertDialog
import androidx.preference.PreferenceManager
import com.alibaba.mnnllm.android.R
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

object AnnouncementManager {

    private const val TAG = "AnnouncementManager"
    private const val KEY_SEEN_ANNOUNCEMENT_ID = "seen_announcement_id"

    // Primary: official site. Fallback: GitHub Pages raw.
    private val announcementUrls = listOf(
        "https://hnncnet.name666.com/announcement.json",
        "https://etqwfd.github.io/HNN-CN-ET/announcement.json"
    )

    /**
     * Check for a new announcement and show it once.
     * Called on the main thread; network runs on IO.
     */
    fun checkAndShow(activity: Activity, forceShow: Boolean = false) {
        val prefs = PreferenceManager.getDefaultSharedPreferences(activity)
        val seenId = prefs.getString(KEY_SEEN_ANNOUNCEMENT_ID, null)
        CoroutineScope(Dispatchers.Main).launch {
            val announcement = withContext(Dispatchers.IO) { fetchAnnouncement() }
            if (announcement == null) {
                Log.d(TAG, "No announcement available")
                return@launch
            }
            val id = announcement.optString("id", "")
            if (!forceShow && id.isNotEmpty() && id == seenId) {
                Log.d(TAG, "Announcement already seen ($id), skipping")
                return@launch
            }
            if (id.isNotEmpty()) {
                prefs.edit().putString(KEY_SEEN_ANNOUNCEMENT_ID, id).apply()
            }
            if (activity.isFinishing || activity.isDestroyed) {
                return@launch
            }
            val title = announcement.optString("title", activity.getString(R.string.announcement_title))
            val content = announcement.optString("content", "")
            AlertDialog.Builder(activity)
                .setTitle(title)
                .setMessage(content)
                .setPositiveButton(R.string.announcement_dismiss, null)
                .setCancelable(true)
                .show()
        }
    }

    private fun fetchAnnouncement(): JSONObject? {
        for (urlStr in announcementUrls) {
            try {
                val url = URL(urlStr)
                val conn = url.openConnection() as HttpURLConnection
                conn.connectTimeout = 8000
                conn.readTimeout = 8000
                conn.requestMethod = "GET"
                conn.setRequestProperty("Accept", "application/json")
                val code = conn.responseCode
                if (code == 200) {
                    val body = conn.inputStream.bufferedReader().use { it.readText() }
                    val json = JSONObject(body)
                    if (json.has("content")) {
                        return json
                    }
                }
                conn.disconnect()
            } catch (e: Exception) {
                Log.w(TAG, "Failed to fetch announcement from $urlStr: ${e.message}")
            }
        }
        return null
    }
}

// Created by ET / ET Association. Copyright (c) 2026 ET. All rights reserved.
package com.alibaba.mnnllm.android.mainsettings

import android.app.ActivityManager
import android.content.Context
import java.net.NetworkInterface
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.WindowManager
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.alibaba.mnnllm.android.BuildConfig
import com.alibaba.mnnllm.android.R
import com.alibaba.mnnllm.android.databinding.ActivityModelServerBinding
import com.alibaba.mnnllm.android.model.ModelUtils
import com.alibaba.mls.api.download.ModelDownloadManager
import com.alibaba.mnnllm.api.openai.manager.ApiServiceManager
import com.alibaba.mnnllm.api.openai.service.ApiServerConfig
import java.net.Inet4Address

/** HNN CN ET v1.2.5: server-panel activity for the lite / server editions.
 *  Clicking a downloaded model lands here instead of the chat screen: the model is
 *  loaded and the built-in OpenAI-compatible API server starts automatically, the
 *  screen stays on, and the panel shows the endpoint / model name / API key. */
class ModelServerActivity : AppCompatActivity() {

    private lateinit var binding: ActivityModelServerBinding
    private val mainHandler = Handler(Looper.getMainLooper())
    private var modelId: String? = null
    private var serverRunning = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityModelServerBinding.inflate(layoutInflater)
        setContentView(binding.root)
        // Keep the screen on while the server is running.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        modelId = intent.getStringExtra("model_id")
        binding.btnStopServer.setOnClickListener { stopServer() }

        if (modelId.isNullOrBlank()) {
            binding.tvStatus.text = getString(R.string.model_not_found, "")
            binding.progressLoading.visibility = View.GONE
            return
        }

        startLoading()
    }

    private fun startLoading() {
        binding.progressLoading.visibility = View.VISIBLE
        binding.tvStatus.text = getString(R.string.server_loading_model)
        binding.panelServerInfo.visibility = View.GONE

        Thread {
            // LITE_MODE: refuse to load when free RAM is too low — prevents the
            // "loads then instantly crashes" death on 2 GB devices.
            if (BuildConfig.LITE_MODE && !hasEnoughMemory()) {
                mainHandler.post {
                    binding.progressLoading.visibility = View.GONE
                    binding.tvStatus.text = getString(R.string.server_low_memory)
                }
                return@Thread
            }
            val resolved = resolveModelId()
            val ok = ApiServiceManager.startApiService(this, resolved)
            mainHandler.post {
                binding.progressLoading.visibility = View.GONE
                if (ok) {
                    serverRunning = true
                    binding.tvStatus.text = getString(R.string.server_running)
                    showServerInfo(resolved)
                    Toast.makeText(this, R.string.server_running_toast, Toast.LENGTH_LONG).show()
                } else {
                    binding.tvStatus.text = getString(R.string.server_start_failed)
                }
            }
        }.start()
    }

    private fun hasEnoughMemory(): Boolean {
        return runCatching {
            val am = getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
            val mi = ActivityManager.MemoryInfo()
            am.getMemoryInfo(mi)
            val availMB = mi.availMem / (1024 * 1024)
            // Require at least ~350 MB free before loading a model on lite devices.
            availMB >= 350
        }.getOrDefault(true)
    }

    private fun resolveModelId(): String? {
        return runCatching {
            val raw = modelId ?: return null
            if (raw.startsWith("local") || raw.startsWith("Builtin") || raw.contains("/")) raw
            else ModelUtils.getValidModelIdFromName(ModelDownloadManager.getInstance(this), raw) ?: raw
        }.getOrDefault(modelId)
    }

    private fun showServerInfo(modelId: String?) {
        binding.panelServerInfo.visibility = View.VISIBLE
        binding.tvModelName.text = modelId ?: "unknown"
        val port = ApiServerConfig.getPort(this)
        val ip = getLocalIpAddress()
        binding.tvEndpoint.text = getString(R.string.server_endpoint, "http://$ip:$port")
        binding.tvApiKey.text = getString(R.string.server_api_key, ApiServerConfig.getApiKey(this))
        binding.tvAuth.text = getString(R.string.server_auth,
            if (ApiServerConfig.isAuthEnabled(this)) getString(R.string.server_auth_on)
            else getString(R.string.server_auth_off))
        binding.tvHint.text = getString(R.string.server_hint, "http://$ip:$port/v1/chat/completions")
    }

    private fun getLocalIpAddress(): String {
        return runCatching {
            val enis = NetworkInterface.getNetworkInterfaces()
            while (enis.hasMoreElements()) {
                val eni = enis.nextElement()
                if (!eni.isUp || eni.isLoopback) continue
                val addrs = eni.inetAddresses
                while (addrs.hasMoreElements()) {
                    val addr = addrs.nextElement()
                    if (addr is Inet4Address && !addr.isLoopbackAddress) {
                        return addr.hostAddress ?: ""
                    }
                }
            }
            ""
        }.getOrDefault("")
    }

    private fun stopServer() {
        ApiServiceManager.stopApiService(this)
        serverRunning = false
        binding.tvStatus.text = getString(R.string.server_stopped)
        binding.panelServerInfo.visibility = View.GONE
        Toast.makeText(this, R.string.server_stopped_toast, Toast.LENGTH_SHORT).show()
    }

    override fun onDestroy() {
        // Do NOT stop the server here: in server mode the API service must keep
        // serving after the panel is closed (foreground service keeps it alive).
        super.onDestroy()
    }
}

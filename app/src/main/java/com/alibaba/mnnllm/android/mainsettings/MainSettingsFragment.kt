// Created by ruoyi.sjd on 2025/2/28.
// Copyright (c) 2024 Alibaba Group Holding Limited All rights reserved.

package com.alibaba.mnnllm.android.mainsettings

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.preference.PreferenceManager
import com.alibaba.mls.api.source.ModelSources
import com.alibaba.mnnllm.android.MNN
import com.alibaba.mnnllm.android.MnnLlmApplication
import com.alibaba.mnnllm.android.R
import com.alibaba.mnnllm.android.databinding.FragmentMainSettingsBinding
import com.alibaba.mnnllm.android.debug.DebugActivity
import com.alibaba.mnnllm.android.modelmarket.ModelRepository
import com.alibaba.mnnllm.android.privacy.PrivacyPolicyManager
import com.alibaba.mnnllm.android.update.UpdateChecker
import com.alibaba.mnnllm.android.utils.AppUtils
import com.alibaba.mnnllm.api.openai.manager.ApiServiceManager
import com.alibaba.mnnllm.api.openai.service.ApiServerConfig
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.delay

class MainSettingsFragment : Fragment() {

    companion object {
        const val TAG = "MainSettingsFragment"
        private const val DEBUG_CLICK_COUNT = 5
        private const val DEBUG_CLICK_TIMEOUT = 3000L // 3 seconds
    }

    private var _binding: FragmentMainSettingsBinding? = null
    private val binding get() = _binding!!

    private var updateChecker: UpdateChecker? = null
    private var debugClickCount = 0
    private val debugClickHandler = Handler(Looper.getMainLooper())
    private var debugClickRunnable: Runnable? = null
    private var updateCheckRunnable: Runnable? = null

    private var suppressCrashDiagnosticsCallback = false
    private var crashDiagnosticsToggleInProgress = false
    private var crashDiagnosticsDialogShowing = false

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentMainSettingsBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        setupSettings()
    }

    override fun onResume() {
        super.onResume()
        updateChecker?.checkForUpdates(requireContext(), false)
    }

    private fun setupSettings() {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(requireContext())

        binding.itemStopDownload.isChecked = sharedPreferences.getBoolean("stop_download_on_chat", true)
        binding.itemStopDownload.setOnCheckedChangeListener { isChecked ->
            sharedPreferences.edit().putBoolean("stop_download_on_chat", isChecked).apply()
        }

        // HNN CN ET v1.0.2: AI 模型审核开关（默认关闭 → 原生对话，不经过任何附加审核）
        binding.itemAiSafetyFilter.isChecked = MainSettings.isAiSafetyFilterEnabled(requireContext())
        binding.itemAiSafetyFilter.setOnCheckedChangeListener { isChecked ->
            MainSettings.setAiSafetyFilterEnabled(requireContext(), isChecked)
        }

        // HNN CN ET v1.0.3: Agent 工作模式开关
        binding.itemAgentMode.isChecked = MainSettings.isAgentModeEnabled(requireContext())
        binding.itemAgentMode.setOnCheckedChangeListener { isChecked ->
            MainSettings.setAgentModeEnabled(requireContext(), isChecked)
            if (isChecked) {
                // 开启 Agent 模式时申请存储 + 悬浮窗权限
                com.alibaba.mnnllm.android.agent.AgentPermissions.requestAll(requireActivity())
            }
        }

        // HNN CN ET v1.0.3: MCP 连接配置入口
        binding.itemMcpConfig.setOnClickListener {
            startActivity(Intent(requireContext(), com.alibaba.mnnllm.android.agent.McpSettingsActivity::class.java))
        }

        // HNN CN ET v1.0.5: 高速直答模式（直接回答、不拒绝、跳过深度思考）
        binding.itemDirectReply.isChecked = MainSettings.isDirectReplyEnabled(requireContext())
        binding.itemDirectReply.setOnCheckedChangeListener { isChecked ->
            MainSettings.setDirectReplyEnabled(requireContext(), isChecked)
        }

        setupAppearance()
        setupDownloadProvider(sharedPreferences)
        setupVoiceModelManagement()
        setupStorageManagement()

        binding.itemEnableApi.isChecked = MainSettings.isApiServiceEnabled(requireContext())
        binding.itemEnableApi.setOnCheckedChangeListener { isChecked ->
            sharedPreferences.edit().putBoolean("enable_api_service", isChecked).apply()
        }

        setupCrashDiagnostics()
        setupResetApiConfig()
        setupUpdateAndVersion()
        setupContactAndLogs()
        setupDebugMode(sharedPreferences)
    }

    private fun setupAppearance() {
        // Theme mode: system / dark / light
        binding.rowThemeMode.setOnClickListener {
            val mode = MainSettings.getThemeMode(requireContext())
            val labels = arrayOf(
                getString(R.string.theme_mode_system),
                getString(R.string.theme_mode_dark),
                getString(R.string.theme_mode_light)
            )
            val keys = arrayOf("system", "dark", "light")
            val checked = keys.indexOf(mode).coerceAtLeast(0)
            MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.settings_theme_mode)
                .setSingleChoiceItems(labels, checked) { d, which ->
                    MainSettings.setThemeMode(requireContext(), keys[which])
                    androidx.appcompat.app.AppCompatDelegate.setDefaultNightMode(
                        when (keys[which]) {
                            "light" -> androidx.appcompat.app.AppCompatDelegate.MODE_NIGHT_NO
                            "dark" -> androidx.appcompat.app.AppCompatDelegate.MODE_NIGHT_YES
                            else -> androidx.appcompat.app.AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM
                        }
                    )
                    requireActivity().recreate()
                    d.dismiss()
                }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        }

        // Theme color: presets + custom
        binding.rowThemeColor.setOnClickListener {
            val presets = arrayOf(
                "blue" to 0xFF2E6BFF.toInt(),
                "green" to 0xFF00B38A.toInt(),
                "red" to 0xFFE5484D.toInt(),
                "yellow" to 0xFFF5A623.toInt(),
                "purple" to 0xFF8B5CF6.toInt(),
                "cyan" to 0xFF06B6D4.toInt()
            )
            val holder = android.widget.LinearLayout(requireContext()).apply {
                orientation = android.widget.LinearLayout.VERTICAL
                setPadding(24, 16, 24, 16)
                addView(android.widget.TextView(context).apply {
                    text = getString(R.string.theme_color_presets)
                    setTextColor(0xFF9E9E9E.toInt())
                    textSize = 14f
                })
                val row = android.widget.LinearLayout(context).apply {
                    orientation = android.widget.LinearLayout.HORIZONTAL
                    gravity = android.view.Gravity.CENTER
                    setPadding(0, 8, 0, 8)
                }
                presets.forEach { (key, color) ->
                    val dot = android.view.View(context)
                    val size = dpv(40)
                    dot.layoutParams = android.widget.LinearLayout.LayoutParams(size, size).apply {
                        marginEnd = dpv(14)
                    }
                    dot.background = android.graphics.drawable.GradientDrawable().apply {
                        shape = android.graphics.drawable.GradientDrawable.OVAL
                        setColor(color)
                    }
                    dot.setOnClickListener {
                        MainSettings.setThemeColor(requireContext(), key)
                        Toast.makeText(context, getString(R.string.settings_complete), Toast.LENGTH_SHORT).show()
                    }
                    row.addView(dot)
                }
                addView(row)
                addView(android.widget.TextView(context).apply {
                    text = getString(R.string.theme_color_custom)
                    setTextColor(0xFF9E9E9E.toInt())
                    textSize = 14f
                    setPadding(0, 8, 0, 4)
                })
                val preview = android.view.View(context)
                preview.layoutParams = android.widget.LinearLayout.LayoutParams(
                    android.widget.LinearLayout.LayoutParams.MATCH_PARENT, dpv(48)
                )
                preview.background = android.graphics.drawable.GradientDrawable().apply {
                    setColor(MainSettings.getCustomThemeColor(requireContext()))
                    cornerRadius = dpv(8).toFloat()
                }
                val hexInput = android.widget.EditText(context).apply {
                    setHint("#RRGGBB")
                    setSingleLine(true)
                    setPadding(dpv(12), dpv(4), dpv(12), dpv(4))
                }
                preview.setOnClickListener {
                    try {
                        val hex = hexInput.text.toString().trim().removePrefix("#")
                        if (hex.length == 6) {
                            MainSettings.setCustomThemeColor(requireContext(), android.graphics.Color.parseColor("#" + hex))
                            preview.background = android.graphics.drawable.GradientDrawable().apply {
                                setColor(android.graphics.Color.parseColor("#" + hex))
                                cornerRadius = dpv(8).toFloat()
                            }
                            Toast.makeText(context, getString(R.string.settings_complete), Toast.LENGTH_SHORT).show()
                        } else {
                            Toast.makeText(context, "请输入 6 位颜色值，如 FF0000", Toast.LENGTH_SHORT).show()
                        }
                    } catch (e: Exception) {
                        Toast.makeText(context, "颜色格式无效", Toast.LENGTH_SHORT).show()
                    }
                }
                addView(hexInput)
                addView(preview)
            }
            MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.settings_theme_color)
                .setView(holder)
                .setPositiveButton(android.R.string.ok, null)
                .show()
        }

        // Chat background image (wallpaper)
        binding.rowWallpaper.setOnClickListener {
            val holder = android.widget.LinearLayout(requireContext()).apply {
                orientation = android.widget.LinearLayout.VERTICAL
                setPadding(24, 16, 24, 16)
                addView(android.widget.Button(context).apply {
                    text = getString(R.string.wallpaper_pick)
                    setOnClickListener { pickWallpaper() }
                })
                addView(android.widget.Button(context).apply {
                    text = getString(R.string.wallpaper_reset)
                    setOnClickListener {
                        MainSettings.setWallpaperPath(requireContext(), null)
                        Toast.makeText(context, getString(R.string.settings_complete), Toast.LENGTH_SHORT).show()
                    }
                })
            }
            MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.settings_wallpaper)
                .setView(holder)
                .setPositiveButton(android.R.string.ok, null)
                .show()
        }
    }

    private fun pickWallpaper() {
        try {
            val intent = Intent(Intent.ACTION_GET_CONTENT).apply {
                type = "image/*"
                addCategory(Intent.CATEGORY_OPENABLE)
            }
            wallpaperLauncher.launch(intent)
        } catch (e: Exception) {
            Toast.makeText(context, "无法打开图片选择器", Toast.LENGTH_SHORT).show()
        }
    }

    private val wallpaperLauncher = registerForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == android.app.Activity.RESULT_OK && result.data?.data != null) {
            val uri = result.data!!.data!!
            lifecycleScope.launch {
                val path = withContext(Dispatchers.IO) {
                    try {
                        val dir = requireContext().filesDir
                        val dst = java.io.File(dir, "wallpaper.jpg")
                        requireContext().contentResolver.openInputStream(uri)?.use { input ->
                            dst.outputStream().use { out -> input.copyTo(out) }
                        }
                        dst.absolutePath
                    } catch (e: Exception) {
                        Log.e(TAG, "copy wallpaper failed: ${e.message}")
                        null
                    }
                }
                if (path != null) {
                    MainSettings.setWallpaperPath(requireContext(), path)
                    Toast.makeText(context, getString(R.string.settings_complete), Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(context, "背景图保存失败", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun dpv(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    private fun setupDownloadProvider(
        sharedPreferences: android.content.SharedPreferences
    ) {
        val providers = listOf(
            ModelSources.sourceHuffingFace,
            ModelSources.sourceModelScope,
            "Modelers"
        )

        fun providerLabel(provider: String): String {
            return when (provider) {
                ModelSources.sourceHuffingFace -> provider
                ModelSources.sourceModelScope -> getString(R.string.modelscope)
                else -> getString(R.string.modelers)
            }
        }

        val defaultProvider = MainSettings.getDownloadProviderString(requireContext())
        if (!sharedPreferences.contains("download_provider")) {
            sharedPreferences.edit().putString("download_provider", defaultProvider).apply()
        }
        val currentProvider = MainSettings.getDownloadProviderString(requireContext())

        binding.dropdownDownloadProvider.setDropDownItems(
            providers,
            itemToString = { providerLabel(it as String) },
            onDropdownItemSelected = { _, selected ->
                val provider = selected as String
                MainSettings.setDownloadProvider(requireContext(), provider)
                val sourceType = when (provider) {
                    ModelSources.sourceHuffingFace -> ModelSources.ModelSourceType.HUGGING_FACE
                    ModelSources.sourceModelScope -> ModelSources.ModelSourceType.MODEL_SCOPE
                    else -> ModelSources.ModelSourceType.MODELERS
                }
                ModelSources.setSourceType(sourceType)
                ModelRepository.clear()
                Toast.makeText(context, R.string.settings_complete, Toast.LENGTH_LONG).show()
            }
        )
        binding.dropdownDownloadProvider.setCurrentItem(currentProvider)
    }

    private fun setupVoiceModelManagement() {
        binding.btnVoiceModelManagement.setOnClickListener {
            val sheet = com.alibaba.mnnllm.android.chat.voice.VoiceModelMarketBottomSheet.newInstance()
            sheet.show(childFragmentManager, "voice_model_market")
        }
    }

    private fun setupStorageManagement() {
        binding.btnStorageManagement.setOnClickListener {
            startActivity(Intent(requireContext(), StorageManagementActivity::class.java))
        }
    }

    private fun setupResetApiConfig() {
        binding.btnResetApi.setOnClickListener {
            MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.reset_api_config)
                .setMessage(R.string.reset_api_config_confirm_message)
                .setPositiveButton(android.R.string.ok) { _, _ ->
                    ApiServerConfig.resetToDefault(requireContext())
                    if (MainSettings.isApiServiceEnabled(requireContext()) && ApiServiceManager.isApiServiceRunning()) {
                        // Run stop/start off main thread to avoid ANR
                        lifecycleScope.launch {
                            withContext(Dispatchers.IO) {
                                ApiServiceManager.stopApiService(requireContext())
                                delay(500)
                                ApiServiceManager.startApiService(requireContext())
                            }
                            Toast.makeText(
                                requireContext(),
                                getString(R.string.api_config_reset_service_restarted),
                                Toast.LENGTH_LONG
                            ).show()
                        }
                    } else {
                        Toast.makeText(
                            requireContext(),
                            getString(R.string.api_config_reset_to_default),
                            Toast.LENGTH_LONG
                        ).show()
                    }
                }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        }
    }

    private fun setupCrashDiagnostics() {
        val privacyManager = PrivacyPolicyManager.getInstance(requireContext())
        setCrashDiagnosticsChecked(privacyManager.isCrashReportingConsented())

        binding.itemCrashDiagnostics.setOnCheckedChangeListener { isChecked ->
            if (suppressCrashDiagnosticsCallback || crashDiagnosticsToggleInProgress) {
                return@setOnCheckedChangeListener
            }
            crashDiagnosticsToggleInProgress = true
            binding.root.post { crashDiagnosticsToggleInProgress = false }

            if (isChecked) {
                privacyManager.setUserConsent(consented = true)
                (requireActivity().application as? MnnLlmApplication)?.applyCrashReportingConsent()
                Toast.makeText(
                    requireContext(),
                    getString(R.string.privacy_policy_consent_enabled),
                    Toast.LENGTH_LONG
                ).show()
                return@setOnCheckedChangeListener
            }

            if (crashDiagnosticsDialogShowing) {
                return@setOnCheckedChangeListener
            }
            crashDiagnosticsDialogShowing = true

            MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.crash_diagnostics_disable_title)
                .setMessage(R.string.crash_diagnostics_disable_confirm_message)
                .setPositiveButton(R.string.crash_diagnostics_disable_confirm_action) { _, _ ->
                    privacyManager.setUserConsent(consented = false)
                    (requireActivity().application as? MnnLlmApplication)?.applyCrashReportingConsent()
                    setCrashDiagnosticsChecked(false)
                    Toast.makeText(
                        requireContext(),
                        getString(R.string.privacy_policy_consent_disabled),
                        Toast.LENGTH_LONG
                    ).show()
                }
                .setNegativeButton(android.R.string.cancel) { _, _ ->
                    setCrashDiagnosticsChecked(true)
                }
                .setOnDismissListener {
                    crashDiagnosticsDialogShowing = false
                }
                .show()
        }
    }

    private fun setCrashDiagnosticsChecked(checked: Boolean) {
        suppressCrashDiagnosticsCallback = true
        binding.itemCrashDiagnostics.isChecked = checked
        suppressCrashDiagnosticsCallback = false
    }

    private fun setupUpdateAndVersion() {
        if (com.alibaba.mnnllm.android.BuildConfig.IS_GOOGLE_PLAY_BUILD) {
            binding.btnCheckUpdate.isClickable = false
            binding.btnCheckUpdate.text = getString(R.string.current_version, AppUtils.getAppVersionName(requireContext()))
        } else {
            binding.btnCheckUpdate.text = getString(
                R.string.current_version_check_update,
                AppUtils.getAppVersionName(requireContext())
            )
            binding.btnCheckUpdate.setOnClickListener {
                handleDebugClick()
                updateCheckRunnable?.let { debugClickHandler.removeCallbacks(it) }
                updateCheckRunnable = Runnable {
                    updateChecker = UpdateChecker(requireContext())
                    updateChecker?.checkForUpdates(requireContext(), true)
                }
                debugClickHandler.postDelayed(updateCheckRunnable!!, 1000L)
            }
        }

        // HNN CN ET v1.0.2: display as "HNN <version> · 基于 MNN <version>" while keeping real attribution.
        val mnnVersion = try {
            MNN.getVersion()
        } catch (_: Exception) {
            "3.6.0"
        }
        binding.tvMnnVersion.text = getString(
            R.string.engine_label_format,
            AppUtils.getAppVersionName(requireContext()),
            mnnVersion
        )
    }

    private fun setupContactAndLogs() {
        binding.btnContactAuthor.setOnClickListener {
            val mailIntent = Intent(Intent.ACTION_SENDTO).apply {
                data = Uri.parse("mailto:2416444244@qq.com")
            }
            try {
                startActivity(Intent.createChooser(mailIntent, getString(R.string.contact_author)))
            } catch (e: Exception) {
                Toast.makeText(context, R.string.qq_group_open_failed, Toast.LENGTH_SHORT).show()
            }
        }

        binding.btnJoinQqGroup.setOnClickListener {
            val groupUrl = "http://qm.qq.com/cgi-bin/qm/qr?_wv=1027&k=U-cTOkqDyq6AxSaXIw3JKzW6z-CpI6-G&authKey=jNbP%2FSn08WY%2FU9or2kiKqewkNeXfoMvnVUXda7k98aunYkYwn2J9zZlCeg0OtKjK&noverify=0&group_code=917902000"
            try {
                startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(groupUrl)))
            } catch (e: Exception) {
                Toast.makeText(context, R.string.qq_group_open_failed, Toast.LENGTH_SHORT).show()
            }
        }

        binding.btnViewLogs.setOnClickListener {
            startActivity(Intent(requireContext(), com.alibaba.mnnllm.android.log.LogsActivity::class.java))
        }
    }

    private fun setupDebugMode(
        sharedPreferences: android.content.SharedPreferences
    ) {
        val isActivated = sharedPreferences.getBoolean("debug_mode_activated", false)
        updateDebugModeVisibility(isActivated)

        binding.btnDebugMode.setOnClickListener {
            startActivity(Intent(requireContext(), DebugActivity::class.java))
        }
    }

    private fun handleDebugClick() {
        debugClickCount++

        debugClickRunnable?.let { debugClickHandler.removeCallbacks(it) }

        if (debugClickCount >= DEBUG_CLICK_COUNT) {
            updateCheckRunnable?.let { debugClickHandler.removeCallbacks(it) }
            updateDebugModeVisibility(true)
            PreferenceManager.getDefaultSharedPreferences(requireContext()).edit()
                .putBoolean("debug_mode_activated", true)
                .apply()
            debugClickCount = 0
            Log.d(TAG, "Debug mode preference activated")
        } else {
            debugClickRunnable = Runnable {
                debugClickCount = 0
                Log.d(TAG, "Debug click count reset due to timeout")
            }
            debugClickHandler.postDelayed(debugClickRunnable!!, DEBUG_CLICK_TIMEOUT)
        }
    }

    private fun updateDebugModeVisibility(visible: Boolean) {
        binding.dividerDebug.visibility = if (visible) View.VISIBLE else View.GONE
        binding.btnDebugMode.visibility = if (visible) View.VISIBLE else View.GONE
    }

    override fun onDestroyView() {
        super.onDestroyView()
        debugClickRunnable?.let { debugClickHandler.removeCallbacks(it) }
        updateCheckRunnable?.let { debugClickHandler.removeCallbacks(it) }
        _binding = null
    }
}

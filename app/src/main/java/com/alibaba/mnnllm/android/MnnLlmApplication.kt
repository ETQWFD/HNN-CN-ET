// Created by ruoyi.sjd on 2024/12/18.
// Copyright (c) 2024 Alibaba Group Holding Limited All rights reserved.
package com.alibaba.mnnllm.android

import android.app.Application
import com.alibaba.mls.api.ApplicationProvider
import com.alibaba.mls.api.download.ModelDownloadManager
import com.alibaba.mnnllm.android.update.UpdateChecker
import com.alibaba.mnnllm.android.utils.CrashUtil
import com.alibaba.mnnllm.android.utils.CurrentActivityTracker
import com.alibaba.mnnllm.android.utils.TimberConfig
import timber.log.Timber
import android.content.Context
import com.jaredrummler.android.device.DeviceName
import com.alibaba.mnnllm.android.modelist.ModelListManager
import com.alibaba.mnnllm.android.privacy.PrivacyPolicyManager
import com.google.firebase.crashlytics.FirebaseCrashlytics

class MnnLlmApplication : Application() {
    
    override fun onCreate() {
        super.onCreate()
        // HNN CN ET v1.0.9: user-selected theme mode (system / light / dark).
        try {
            when (com.alibaba.mnnllm.android.mainsettings.MainSettings.getThemeMode(this)) {
                "light" -> androidx.appcompat.app.AppCompatDelegate.setDefaultNightMode(
                    androidx.appcompat.app.AppCompatDelegate.MODE_NIGHT_NO)
                "dark" -> androidx.appcompat.app.AppCompatDelegate.setDefaultNightMode(
                    androidx.appcompat.app.AppCompatDelegate.MODE_NIGHT_YES)
                else -> androidx.appcompat.app.AppCompatDelegate.setDefaultNightMode(
                    androidx.appcompat.app.AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM)
            }
        } catch (e: Exception) {
            // ignore — DayNight falls back to system
        }
        ApplicationProvider.set(this)
        UpdateChecker.registerDownloadReceiver(applicationContext)
        CrashUtil.init(this)
        instance = this
        DeviceName.init(this)

        // Initialize CurrentActivityTracker
        CurrentActivityTracker.initialize(this)

        applyCrashReportingConsent()

        // Initialize Timber logging based on configuration
        TimberConfig.initialize(this)

        // HNN CN ET: unified in-app logging (file rotation + memory ring buffer + crash capture)
        com.alibaba.mnnllm.android.log.AppLogger.initialize(this)

        // Set context for ModelListManager (enables auto-initialization)
        ModelListManager.setContext(getInstance())
        ModelDownloadManager.getInstance(this).setProgressCallbackIntervalMs(500L)

        // HNN CN ET v1.1.3: migrate downloaded models from the legacy app-private
        // filesDir/.mnnmodels into public /storage/emulated/0/hnn (background).
        // Model paths now point to the public dir; the legacy dir is only scanned
        // as a fallback while migration is still in flight.
        try {
            if (com.alibaba.mnnllm.android.modelist.ModelPaths.legacyModelsDir(this).exists()) {
                Thread {
                    try {
                        com.alibaba.mnnllm.android.modelist.ModelPaths.migrateLegacyModels(this)
                    } catch (t: Throwable) {
                        Timber.w(t, "Model migration failed")
                    }
                }.apply { isDaemon = true }.start()
            }
        } catch (t: Throwable) {
            Timber.w(t, "Failed to schedule model migration")
        }

        StethoInitializer.initialize(this)
    }

    fun applyCrashReportingConsent() {
        if (!BuildConfig.ENABLE_FIREBASE) {
            return
        }
        val consented = PrivacyPolicyManager.getInstance(this).isCrashReportingConsented()
        try {
            FirebaseCrashlytics.getInstance().setCrashlyticsCollectionEnabled(consented)
            FirebaseCrashlytics.getInstance().setCustomKey("user_crash_reporting_consent", consented)
        } catch (t: Throwable) {
            Timber.w(t, "Failed to apply Crashlytics consent state")
        }
    }

    companion object {
        private lateinit var instance: MnnLlmApplication

        fun getAppContext(): Context {
            return instance.applicationContext
        }
        
        /**
         * Get the application instance for accessing Timber configuration
         */
        fun getInstance(): MnnLlmApplication {
            return instance
        }
    }
}

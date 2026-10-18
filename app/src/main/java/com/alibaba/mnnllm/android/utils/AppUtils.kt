// Created by ruoyi.sjd on 2025/2/28.
// Copyright (c) 2024 Alibaba Group Holding Limited All rights reserved.
package com.alibaba.mnnllm.android.utils

import android.content.Context
import android.os.Build

object AppUtils {
    //get app version name
    fun getAppVersionName(context: Context): String {
        val packageManager = context.packageManager
        val packageInfo = packageManager.getPackageInfo(context.packageName, 0)
        return packageInfo.versionName!!
    }

    //get app version code
    fun getAppVersionCode(context: Context): Long {
        val packageManager = context.packageManager
        val packageInfo = packageManager.getPackageInfo(context.packageName, 0)
        return packageInfo.longVersionCode
    }

    //get device supported ABIs joined by comma
    fun getAbiSummary(): String {
        return Build.SUPPORTED_ABIS.joinToString(", ")
    }
}
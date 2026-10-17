// HNN CN ET - no-op Stetho initializer for release builds.
// The real implementation lives in src/debug; Stetho is a debug-only dependency.
package com.alibaba.mnnllm.android

import android.content.Context

object StethoInitializer {
    @JvmStatic
    fun initialize(context: Context) {
        // no-op in release builds
    }
}

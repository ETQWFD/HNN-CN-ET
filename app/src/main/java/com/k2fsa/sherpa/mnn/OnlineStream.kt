package com.k2fsa.sherpa.mnn

class OnlineStream(var ptr: Long = 0) {
    fun acceptWaveform(samples: FloatArray, sampleRate: Int) =
        acceptWaveform(ptr, samples, sampleRate)

    fun inputFinished() = inputFinished(ptr)

    protected fun finalize() {
        if (ptr != 0L) {
            delete(ptr)
            ptr = 0
        }
    }

    fun release() = finalize()

    fun use(block: (OnlineStream) -> Unit) {
        try {
            block(this)
        } finally {
            release()
        }
    }

    private external fun acceptWaveform(ptr: Long, samples: FloatArray, sampleRate: Int)
    private external fun inputFinished(ptr: Long)
    private external fun delete(ptr: Long)


    companion object {
        var isAvailable: Boolean = false
            private set

        init {
            try {
                System.loadLibrary("sherpa-mnn-jni")
                isAvailable = true
            } catch (e: UnsatisfiedLinkError) {
                android.util.Log.w("OnlineStream", "sherpa-mnn-jni not available: ${e.message}")
            } catch (e: Throwable) {
                android.util.Log.w("OnlineStream", "Failed to load sherpa-mnn-jni: ${e.message}")
            }
        }
    }
}

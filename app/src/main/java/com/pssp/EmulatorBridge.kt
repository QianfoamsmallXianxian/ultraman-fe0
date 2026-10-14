package com.pssp

import android.app.Activity
import android.util.Log
import android.view.Surface

object EmulatorBridge {
    @Volatile private var loaded = false

    fun ensureLoaded(): Boolean {
        if (loaded) return true
        return try {
            System.loadLibrary("ultraman_bridge")
            loaded = true
            true
        } catch (t: Throwable) {
            Log.e("UltramanFE0", "load fail", t)
            false
        }
    }

    external fun nativeInit(activity: Activity)
    external fun nativeSetBackend(backend: Int)
    external fun nativeSetOptions(resolution: Int, vsync: Boolean, audio: Boolean)
    external fun nativeSetAudioBuffer(level: Int)
    external fun nativeGetFrameCount(): Long
    external fun nativeGetCoreFps(): Double
    external fun nativeLoadGame(path: String): Boolean
    external fun nativeStart()
    external fun nativeStop()
    external fun nativePause()
    external fun nativeResetContext()
    external fun nativeResume()
    external fun nativeSetSurface(surface: Surface?)
    external fun nativeSetAudioEnabled(enabled: Boolean)
    external fun nativeKey(port: Int, id: Int, down: Boolean)
    external fun nativeAnalog(x: Int, y: Int)
    external fun nativeAnalogRight(x: Int, y: Int)
    external fun nativeReset()
    external fun nativeVersion(): String
    external fun nativeSaveState(path: String): Boolean
    external fun nativeLoadState(path: String): Boolean
    external fun nativeSaveSram(path: String): Boolean
    external fun nativeLoadSram(path: String): Boolean

    const val BTN_B = 0
    const val BTN_Y = 1
    const val BTN_SELECT = 2
    const val BTN_START = 3
    const val BTN_UP = 4
    const val BTN_DOWN = 5
    const val BTN_LEFT = 6
    const val BTN_RIGHT = 7
    const val BTN_A = 8
    const val BTN_X = 9
    const val BTN_L = 10
    const val BTN_R = 11
}

package com.ultraman.fe0

import android.content.Context

object NativeCrashLogger {
    init {
        System.loadLibrary("ultraman_bridge")
    }

    external fun installNativeCrashHandler(dir: String)

    fun install(ctx: Context) {
        try {
            val dir = ctx.getExternalFilesDir(null)
            dir?.mkdirs()
            installNativeCrashHandler(dir!!.absolutePath)
        } catch (_: Throwable) {}
    }
}

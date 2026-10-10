package com.ultraman.fe0
import android.app.Application
import android.util.Log
import java.io.File
class UltramanApp : Application() {
    override fun onCreate() {
        super.onCreate()
        instance = this
        listOf("system", "saves", "config").forEach { File(filesDir, it).mkdirs() }
        Log.i("UltramanFE0", "core=" + EmulatorBridge.nativeVersion())
    }
    companion object {
        lateinit var instance: UltramanApp
            private set
    }
}

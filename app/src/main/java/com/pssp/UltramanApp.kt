package com.pssp

import android.app.Application

class UltramanApp : Application() {
    override fun onCreate() {
        super.onCreate()
        instance = this
        CrashLogger.install(this)
        NativeCrashLogger.install(this)
    }
    companion object {
        lateinit var instance: UltramanApp
            private set
    }
}

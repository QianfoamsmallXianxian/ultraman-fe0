package com.ultraman.fe0

import android.app.Application

class UltramanApp : Application() {
    override fun onCreate() {
        super.onCreate()
        instance = this
    }
    companion object {
        lateinit var instance: UltramanApp
            private set
    }
}

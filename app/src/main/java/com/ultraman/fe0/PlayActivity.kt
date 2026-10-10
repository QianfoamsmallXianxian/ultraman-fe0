package com.ultraman.fe0

import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.FrameLayout
import androidx.appcompat.app.AppCompatActivity

class PlayActivity : AppCompatActivity() {

    private lateinit var view: EmulatorView
    private val cfg by lazy { ConfigManager(this) }
    private var gamePath: String? = null
    private var loaded = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        if (cfg.fullscreen) applyFullscreen()

        val root = FrameLayout(this)
        view = EmulatorView(this)
        root.addView(view, FrameLayout.LayoutParams(-1, -1))
        val overlay = OverlayView(this)
        root.addView(overlay, FrameLayout.LayoutParams(-1, -1))
        setContentView(root)

        gamePath = intent.dataString
        if (gamePath.isNullOrEmpty()) { finish(); return }
        cfg.lastGamePath = gamePath

        if (!EmulatorBridge.ensureLoaded()) { finish(); return }
        EmulatorBridge.nativeInit(this)
        EmulatorBridge.nativeSetOptions(cfg.resolution, cfg.vsync, cfg.audioEnabled)
        EmulatorBridge.nativeSetAudioEnabled(cfg.audioEnabled)

        loaded = EmulatorBridge.nativeLoadGame(gamePath!!)
        if (!loaded) { finish(); return }
        EmulatorBridge.nativeStart()
    }

    private fun applyFullscreen() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let {
                it.hide(WindowInsets.Type.systemBars())
                it.systemBarsBehavior =
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            )
        }
    }

    override fun onPause() { super.onPause(); if (loaded) EmulatorBridge.nativeStop() }
    override fun onDestroy() { super.onDestroy(); if (loaded) EmulatorBridge.nativeStop() }
    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (loaded) EmulatorBridge.nativeStop()
        super.onBackPressed()
    }
}

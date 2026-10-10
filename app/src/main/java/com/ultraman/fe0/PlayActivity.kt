package com.ultraman.fe0

import android.os.Bundle
import android.util.Log
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

        val root = FrameLayout(this)
        view = EmulatorView(this)
        root.addView(view, FrameLayout.LayoutParams(-1, -1))
        val overlay = OverlayView(this)
        root.addView(overlay, FrameLayout.LayoutParams(-1, -1))
        setContentView(root)

        gamePath = intent.dataString
        if (gamePath.isNullOrEmpty()) {
            Log.e("UltramanFE0", "no game path")
            finish()
            return
        }
        cfg.lastGamePath = gamePath
        Log.i("UltramanFE0", "game path: $gamePath")

        if (!EmulatorBridge.ensureLoaded()) {
            Log.e("UltramanFE0", "load library failed")
            finish()
            return
        }
        EmulatorBridge.nativeInit(this)
        EmulatorBridge.nativeSetAudioEnabled(cfg.audioEnabled)

        loaded = EmulatorBridge.nativeLoadGame(gamePath!!)
        Log.i("UltramanFE0", "loadGame result: $loaded")

        if (!loaded) {
            Log.e("UltramanFE0", "load game failed")
            finish()
            return
        }
        EmulatorBridge.nativeStart()
    }

    override fun onPause() {
        super.onPause()
        if (loaded) EmulatorBridge.nativeStop()
    }

    override fun onDestroy() {
        super.onDestroy()
        if (loaded) EmulatorBridge.nativeStop()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (loaded) EmulatorBridge.nativeStop()
        super.onBackPressed()
    }
}

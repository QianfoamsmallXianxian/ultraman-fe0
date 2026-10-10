package com.ultraman.fe0
import android.os.Bundle
import android.view.WindowManager
import android.widget.FrameLayout
import androidx.appcompat.app.AppCompatActivity

class PlayActivity : AppCompatActivity() {

    private lateinit var view: EmulatorView
    private val cfg by lazy { ConfigManager(this) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val root = FrameLayout(this)
        view = EmulatorView(this)
        root.addView(view, FrameLayout.LayoutParams(-1, -1))
        setContentView(root)

        val path = intent.dataString
        if (path.isNullOrEmpty()) { finish(); return }
        cfg.lastGamePath = path

        EmulatorBridge.nativeSetAudioEnabled(cfg.audioEnabled)
        if (EmulatorBridge.nativeLoadGame(path)) {
            EmulatorBridge.nativeStart()
        } else {
            finish()
        }
    }

    override fun onPause() { super.onPause(); EmulatorBridge.nativeStop() }
    override fun onDestroy() { super.onDestroy(); EmulatorBridge.nativeStop() }
    @Deprecated("Deprecated in Java")
    override fun onBackPressed() { EmulatorBridge.nativeStop(); super.onBackPressed() }
}

package com.ultraman.fe0

import android.app.AlertDialog
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import java.io.File

class PlayActivity : AppCompatActivity() {

    private lateinit var view: EmulatorView
    private val cfg by lazy { ConfigManager(this) }
    private var gamePath: String? = null
    private var loaded = false
    private lateinit var savesDir: File
    private var gameKey = "game"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        if (cfg.fullscreen) applyFullscreen()

        val root = FrameLayout(this)
        view = EmulatorView(this)
        root.addView(view, FrameLayout.LayoutParams(-1, -1))
        val overlay = OverlayView(this)
        root.addView(overlay, FrameLayout.LayoutParams(-1, -1))
        val menuBtn = android.widget.Button(this).apply {
            text = "菜单"
            alpha = 0.6f
            textSize = 12f
            setBackgroundColor(0xAA000000.toInt())
            setTextColor(0xFFFFFFFF.toInt())
            setOnClickListener { showMenu() }
        }
        val lp = FrameLayout.LayoutParams(160, 90)
        lp.leftMargin = 30
        lp.topMargin = 30
        root.addView(menuBtn, lp)
        setContentView(root)

        gamePath = intent.dataString
        if (gamePath.isNullOrEmpty()) { finish(); return }
        cfg.lastGamePath = gamePath

        savesDir = File(getExternalFilesDir(null), "saves").apply { mkdirs() }
        gameKey = File(gamePath!!).nameWithoutExtension
            .replace(Regex("[^a-zA-Z0-9_]"), "_")
            .ifEmpty { "game" }

        if (!EmulatorBridge.ensureLoaded()) { finish(); return }
        EmulatorBridge.nativeInit(this)
        EmulatorBridge.nativeSetOptions(cfg.resolution, cfg.vsync, cfg.audioEnabled)
        EmulatorBridge.nativeSetAudioEnabled(cfg.audioEnabled)

        loaded = EmulatorBridge.nativeLoadGame(gamePath!!)
        if (!loaded) { finish(); return }

        // 游戏启动时自动加载 SRAM（游戏内存档）
        val sramFile = File(savesDir, "${gameKey}.sram")
        if (sramFile.exists()) {
            EmulatorBridge.nativeLoadSram(sramFile.absolutePath)
        }

        EmulatorBridge.nativeStart()
    }

    private fun showMenu() {
        val items = arrayOf(
            "保存即时存档",
            "加载即时存档",
            "保存游戏内存档",
            "加载游戏内存档",
            "退出游戏"
        )
        AlertDialog.Builder(this)
            .setTitle("菜单")
            .setItems(items) { _, which ->
                when (which) {
                    0 -> saveState()
                    1 -> loadState()
                    2 -> saveSram()
                    3 -> loadSram()
                    4 -> finish()
                }
            }
            .setNegativeButton("取消", null)
            .show()
    }

    private fun stateFile(): File = File(savesDir, "${gameKey}.state")

    private fun sramFile(): File = File(savesDir, "${gameKey}.sram")

    private fun saveState() {
        val f = stateFile()
        val ok = EmulatorBridge.nativeSaveState(f.absolutePath)
        toast(if (ok) "即时存档已保存" else "保存失败")
    }

    private fun loadState() {
        val f = stateFile()
        if (!f.exists()) { toast("没有存档"); return }
        val ok = EmulatorBridge.nativeLoadState(f.absolutePath)
        toast(if (ok) "已读取即时存档" else "读取失败")
    }

    private fun saveSram() {
        val f = sramFile()
        val ok = EmulatorBridge.nativeSaveSram(f.absolutePath)
        toast(if (ok) "游戏内存档已保存" else "保存失败")
    }

    private fun loadSram() {
        val f = sramFile()
        if (!f.exists()) { toast("没有游戏内存档"); return }
        val ok = EmulatorBridge.nativeLoadSram(f.absolutePath)
        toast(if (ok) "已读取游戏内存档" else "读取失败")
    }

    private fun toast(s: String) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show()
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

    override fun onPause() {
        super.onPause()
        if (loaded) {
            // 退出前自动保存 SRAM
            EmulatorBridge.nativeSaveSram(sramFile().absolutePath)
            EmulatorBridge.nativeStop()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (loaded) EmulatorBridge.nativeStop()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (loaded) {
            EmulatorBridge.nativeSaveSram(sramFile().absolutePath)
            EmulatorBridge.nativeStop()
        }
        super.onBackPressed()
    }
}

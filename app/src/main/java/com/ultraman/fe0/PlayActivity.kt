package com.ultraman.fe0

import android.app.AlertDialog
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.io.File
import java.io.FileOutputStream

class PlayActivity : AppCompatActivity() {

    private lateinit var view: EmulatorView
    private val cfg by lazy { ConfigManager(this) }
    private var gamePath: String? = null
    private var loaded = false
    private var initGuard = false
    private lateinit var savesDir: File
    private var gameKey = "game"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        WindowCompat.setDecorFitsSystemWindows(window, false)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS)
        window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS)
        window.statusBarColor = Color.TRANSPARENT
        window.navigationBarColor = Color.TRANSPARENT
        window.setFlags(
            WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
        )
        window.setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN
        )
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }

        val root = FrameLayout(this)
        view = EmulatorView(this)
        root.addView(view, FrameLayout.LayoutParams(-1, -1))
        val overlay = OverlayView(this)
        root.addView(overlay, FrameLayout.LayoutParams(-1, -1))
        val menuBtn = Button(this)
        menuBtn.text = "菜单"
        menuBtn.alpha = 0.55f
        menuBtn.textSize = 11f
        menuBtn.setBackgroundColor(0xAA000000.toInt())
        menuBtn.setTextColor(0xFFFFFFFF.toInt())
        menuBtn.setOnClickListener { showMenu() }
        val lp = FrameLayout.LayoutParams(130, 70)
        lp.leftMargin = 20
        lp.topMargin = 20
        root.addView(menuBtn, lp)
        setContentView(root)

        hideSystemBars()
        window.decorView.post { hideSystemBars() }
        window.decorView.postDelayed({ hideSystemBars() }, 300)
        window.decorView.postDelayed({ hideSystemBars() }, 1000)

        gamePath = intent.dataString
        if (gamePath.isNullOrEmpty()) { finish(); return }
        cfg.lastGamePath = gamePath

        savesDir = File(getExternalFilesDir(null), "saves")
        savesDir.mkdirs()
        val raw = File(gamePath!!).nameWithoutExtension
        gameKey = raw.replace(Regex("[^a-zA-Z0-9_]"), "_")
        if (gameKey.isEmpty()) gameKey = "game"

        if (!EmulatorBridge.ensureLoaded()) { finish(); return }
        EmulatorBridge.nativeInit(this)
        EmulatorBridge.nativeSetBackend(cfg.backend)
        EmulatorBridge.nativeSetOptions(cfg.resolution, cfg.vsync, cfg.audioEnabled)
        EmulatorBridge.nativeSetAudioEnabled(cfg.audioEnabled)
        EmulatorBridge.nativeSetAudioBuffer(cfg.audioBuffer)

        view.onSurfaceRecreated = {
            Log.i("UltramanFE0", "surface recreated: reset ctx + resume")
            if (loaded) {
                EmulatorBridge.nativeResetContext()
                EmulatorBridge.nativeResume()
            }
        }

        view.onSurfaceReady = surfaceReady@{
            if (initGuard) return@surfaceReady
            initGuard = true
            if (loaded) return@surfaceReady

            val realPath = copyIsoToPrivate(Uri.parse(gamePath!!))
            if (realPath == null) {
                runOnUiThread {
                    Toast.makeText(this, "ISO 复制失败", Toast.LENGTH_LONG).show()
                    finish()
                }
                return@surfaceReady
            }
            Log.i("UltramanFE0", "native path: $realPath")
            loaded = EmulatorBridge.nativeLoadGame(realPath)
            if (!loaded) {
                runOnUiThread {
                    Toast.makeText(this, "加载失败", Toast.LENGTH_LONG).show()
                    finish()
                }
                return@surfaceReady
            }
            val sram = File(savesDir, "$gameKey.sram")
            if (sram.exists()) EmulatorBridge.nativeLoadSram(sram.absolutePath)
            EmulatorBridge.nativeStart()
        }
    }

    private fun copyIsoToPrivate(src: Uri): String? {
        return try {
            val dir = File(filesDir, "iso")
            dir.mkdirs()
            val dst = File(dir, "game.iso")
            if (dst.exists() && dst.length() > 0) {
                Log.i("UltramanFE0", "iso cached: ${dst.absolutePath}")
                return dst.absolutePath
            }
            Log.i("UltramanFE0", "copying iso")
            val input = contentResolver.openInputStream(src) ?: return null
            val output = FileOutputStream(dst)
            val buf = ByteArray(1024 * 1024)
            var total = 0L
            while (true) {
                val n = input.read(buf)
                if (n <= 0) break
                output.write(buf, 0, n)
                total += n
            }
            output.close()
            input.close()
            Log.i("UltramanFE0", "iso copied: $total bytes")
            if (dst.length() > 0) dst.absolutePath else null
        } catch (t: Throwable) {
            Log.e("UltramanFE0", "copy iso failed", t)
            null
        }
    }

    private fun hideSystemBars() {
        try {
            val c = WindowInsetsControllerCompat(window, window.decorView)
            c.hide(WindowInsetsCompat.Type.systemBars())
            c.systemBarsBehavior =
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE

            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            )
        } catch (_: Throwable) {}
    }

    private fun showMenu() {
        val items = arrayOf("保存即时存档", "加载即时存档", "保存游戏内存档", "加载游戏内存档", "退出游戏")
        AlertDialog.Builder(this)
            .setTitle("菜单")
            .setItems(items) { _, w ->
                when (w) {
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

    private fun stateFile(): File = File(savesDir, "$gameKey.state")
    private fun sramFile(): File = File(savesDir, "$gameKey.sram")

    private fun saveState() {
        toast(if (EmulatorBridge.nativeSaveState(stateFile().absolutePath)) "即时存档已保存" else "保存失败")
    }

    private fun loadState() {
        val f = stateFile()
        if (!f.exists()) { toast("无存档"); return }
        toast(if (EmulatorBridge.nativeLoadState(f.absolutePath)) "已读取" else "读取失败")
    }

    private fun saveSram() {
        toast(if (EmulatorBridge.nativeSaveSram(sramFile().absolutePath)) "内存档已保存" else "保存失败")
    }

    private fun loadSram() {
        val f = sramFile()
        if (!f.exists()) { toast("无内存档"); return }
        toast(if (EmulatorBridge.nativeLoadSram(f.absolutePath)) "已读取" else "读取失败")
    }

    private fun toast(s: String) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show()
    }

    override fun onPause() {
        super.onPause()
        if (loaded) {
            EmulatorBridge.nativeSaveSram(sramFile().absolutePath)
            EmulatorBridge.nativePause()
        }
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
        if (loaded) EmulatorBridge.nativeResume()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    override fun onDestroy() {
        super.onDestroy()
        if (loaded) {
            EmulatorBridge.nativeStop()
            loaded = false
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (loaded) {
            EmulatorBridge.nativeSaveSram(sramFile().absolutePath)
            EmulatorBridge.nativeStop()
            loaded = false
        }
        super.onBackPressed()
    }
}

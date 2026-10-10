package com.ultraman.fe0

import android.app.AlertDialog
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import java.io.File

class PlayActivity : AppCompatActivity() {

    private lateinit var view: EmulatorView
    private val cfg by lazy { ConfigManager(this) }
    private var gamePath: String? = null
    private var loaded = false
    private var initGuard = false
    private lateinit var savesDir: File
    private var gameKey = "game"

        view.onSurfaceReady = {
            if (initGuard) return@onSurfaceReady
            initGuard = true
            if (!loaded) {
                val realPath = copyIsoToPrivate(Uri.parse(gamePath!!))
                if (realPath == null) {
                    runOnUiThread {
                        Toast.makeText(this, "ISO 复制失败", Toast.LENGTH_LONG).show()
                        finish()
                    }
                    return@onSurfaceReady
                }
                android.util.Log.i("UltramanFE0", "native path: $realPath")
                loaded = EmulatorBridge.nativeLoadGame(realPath)
                if (!loaded) {
                    runOnUiThread {
                        Toast.makeText(this, "加载失败", Toast.LENGTH_LONG).show()
                        finish()
                    }
                } else {
                    val sramFile = File(savesDir, "${gameKey}.sram")
                    if (sramFile.exists()) {
                        EmulatorBridge.nativeLoadSram(sramFile.absolutePath)
                    }
                    EmulatorBridge.nativeStart()
                }
            }
        }
    }

    private fun copyIsoToPrivate(src: Uri): String? {
        return try {
            val dir = File(filesDir, "iso").apply { mkdirs() }
            val dst = File(dir, "game.iso")
            if (dst.exists() && dst.length() > 0) {
                android.util.Log.i("UltramanFE0", "iso cached: ${dst.absolutePath} (${dst.length()} bytes)")
                return dst.absolutePath
            }
            android.util.Log.i("UltramanFE0", "copying iso from $src")
            contentResolver.openInputStream(src)?.use { input ->
                java.io.FileOutputStream(dst).use { output ->
                    val buf = ByteArray(1024 * 1024)
                    var total = 0L
                    while (true) {
                        val n = input.read(buf)
                        if (n <= 0) break
                        output.write(buf, 0, n)
                        total += n
                    }
                    android.util.Log.i("UltramanFE0", "iso copied: $total bytes")
                }
            } ?: return null
            if (dst.length() > 0) dst.absolutePath else null
        } catch (t: Throwable) {
            android.util.Log.e("UltramanFE0", "copy iso failed", t)
            null
        }
    }

    private fun applyFullscreen() {
        try {
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
        } catch (_: Throwable) {}
    }

    private fun showMenu() {
        val items = arrayOf("保存即时存档", "加载即时存档", "保存游戏内存档", "加载游戏内存档", "退出游戏")
        AlertDialog.Builder(this).setTitle("菜单").setItems(items) { _, w ->
            when (w) {
                0 -> saveState()
                1 -> loadState()
                2 -> saveSram()
                3 -> loadSram()
                4 -> finish()
            }
        }.setNegativeButton("取消", null).show()
    }

    private fun stateFile(): File = File(savesDir, "${gameKey}.state")
    private fun sramFile(): File = File(savesDir, "${gameKey}.sram")

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

    private fun toast(s: String) = Toast.makeText(this, s, Toast.LENGTH_SHORT).show()

    override fun onPause() {
        super.onPause()
        if (loaded) {
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

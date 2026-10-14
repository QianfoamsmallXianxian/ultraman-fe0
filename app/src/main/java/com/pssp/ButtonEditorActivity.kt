package com.pssp

import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.Button
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

class ButtonEditorActivity : AppCompatActivity() {
    private lateinit var editor: ButtonEditorView
    private lateinit var sbSize: SeekBar
    private lateinit var sbAlpha: SeekBar
    private lateinit var tvAlpha: TextView
    private lateinit var sbDistance: SeekBar
    private lateinit var tvDist: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContentView(R.layout.activity_button_editor)
        window.decorView.post { applyFullscreen() }

        editor = findViewById(R.id.editor)
        sbSize = findViewById(R.id.sbSize)
        sbAlpha = findViewById(R.id.sbAlpha)
        tvAlpha = findViewById(R.id.tvAlpha)
        sbDistance = findViewById(R.id.sbDistance)
        tvDist = findViewById(R.id.tvDist)

        editor.onSelectionChanged = { idx ->
            if (idx >= 0) {
                sbSize.isEnabled = true; sbAlpha.isEnabled = true; sbDistance.isEnabled = true
                sbSize.progress = ((editor.getSelectedSize() - 0.03f) / 0.09f * 100).toInt().coerceIn(0, 100)
                sbAlpha.progress = (editor.getSelectedAlpha() * 100).toInt().coerceIn(0, 100)
                tvAlpha.text = "${sbAlpha.progress}%"
            } else { sbSize.isEnabled = false; sbAlpha.isEnabled = false; sbDistance.isEnabled = false }
        }

        sbSize.max = 100
        sbSize.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar, p: Int, u: Boolean) { editor.setSelectedSize(0.03f + p / 100f * 0.09f) }
            override fun onStartTrackingTouch(s: SeekBar) {}
            override fun onStopTrackingTouch(s: SeekBar) {}
        })

        sbAlpha.max = 100
        sbAlpha.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar, p: Int, u: Boolean) { editor.setSelectedAlpha(p / 100f); tvAlpha.text = "$p%" }
            override fun onStartTrackingTouch(s: SeekBar) {}
            override fun onStopTrackingTouch(s: SeekBar) {}
        })

        sbDistance.max = 170
        sbDistance.progress = 100
        sbDistance.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar, p: Int, u: Boolean) {
                tvDist.text = "$p%"
                editor.setSelectedDistance(p / 100f)
            }
            override fun onStartTrackingTouch(s: SeekBar) {}
            override fun onStopTrackingTouch(s: SeekBar) {}
        })

        findViewById<Button>(R.id.btnSave).setOnClickListener {
            ButtonConfigManager(this).save(editor.buttons)
            Toast.makeText(this, "已保存", Toast.LENGTH_SHORT).show()
            finish()
        }
        findViewById<Button>(R.id.btnReset).setOnClickListener {
            ButtonConfigManager(this).reset()
            editor.buttons.clear()
            editor.buttons.addAll(ButtonConfigManager.DEFAULT)
            editor.invalidate()
            Toast.makeText(this, "已重置", Toast.LENGTH_SHORT).show()
        }
        findViewById<Button>(R.id.btnBack).setOnClickListener { finish() }
    }

    private fun applyFullscreen() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                window.setDecorFitsSystemWindows(false)
                window.insetsController?.let {
                    it.hide(WindowInsets.Type.systemBars())
                    it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                }
            } else {
                @Suppress("DEPRECATION")
                window.decorView.systemUiVisibility = (
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                )
            }
        } catch (_: Throwable) {}
    }
}

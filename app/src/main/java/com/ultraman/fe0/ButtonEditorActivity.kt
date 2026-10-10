package com.ultraman.fe0

import android.os.Bundle
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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_button_editor)
        editor = findViewById(R.id.editor)
        sbSize = findViewById(R.id.sbSize)
        sbAlpha = findViewById(R.id.sbAlpha)
        tvAlpha = findViewById(R.id.tvAlpha)

        editor.onSelectionChanged = { idx ->
            if (idx >= 0) {
                sbSize.isEnabled = true
                sbAlpha.isEnabled = true
                sbSize.progress = ((editor.getSelectedSize() - 0.03f) / 0.09f * 100).toInt().coerceIn(0, 100)
                sbAlpha.progress = (editor.getSelectedAlpha() * 100).toInt().coerceIn(0, 100)
                tvAlpha.text = "${sbAlpha.progress}%"
            } else {
                sbSize.isEnabled = false
                sbAlpha.isEnabled = false
            }
        }

        sbSize.max = 100
        sbSize.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar, p: Int, u: Boolean) {
                editor.setSelectedSize(0.03f + p / 100f * 0.09f)
            }
            override fun onStartTrackingTouch(s: SeekBar) {}
            override fun onStopTrackingTouch(s: SeekBar) {}
        })

        sbAlpha.max = 100
        sbAlpha.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar, p: Int, u: Boolean) {
                editor.setSelectedAlpha(p / 100f)
                tvAlpha.text = "$p%"
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
}

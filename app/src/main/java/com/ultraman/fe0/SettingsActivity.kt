package com.ultraman.fe0

import android.content.Intent
import android.os.Bundle
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.SwitchCompat

class SettingsActivity : AppCompatActivity() {
    private val cfg by lazy { ConfigManager(this) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        val sbRes = findViewById<SeekBar>(R.id.sbRes)
        val tvRes = findViewById<TextView>(R.id.tvRes)
        val swFull = findViewById<SwitchCompat>(R.id.swFullscreen)
        val swShowFps = findViewById<SwitchCompat>(R.id.swShowFps)
        val spAudioBuffer = findViewById<Spinner>(R.id.spAudioBuffer)
        val btnBack = findViewById<Button>(R.id.btnBack)
        val btnEdit = findViewById<Button>(R.id.btnEditButtons)

        sbRes.max = 2
        sbRes.progress = cfg.resolution - 1
        tvRes.text = "${cfg.resolution}x"
        sbRes.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar, p: Int, u: Boolean) {
                tvRes.text = "${p + 1}x"
                cfg.resolution = p + 1
            }
            override fun onStartTrackingTouch(s: SeekBar) {}
            override fun onStopTrackingTouch(s: SeekBar) {}
        })

        swFull.isChecked = cfg.fullscreen
        swFull.setOnCheckedChangeListener { _, v -> cfg.fullscreen = v }

        swShowFps.isChecked = cfg.showFps
        swShowFps.setOnCheckedChangeListener { _, v -> cfg.showFps = v }

        spAudioBuffer.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_dropdown_item,
            arrayOf("低 (延迟小)", "中 (推荐)", "高 (减少爆音)")
        )
        spAudioBuffer.setSelection(cfg.audioBuffer.coerceIn(0, 2))
        spAudioBuffer.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: android.view.View?, pos: Int, id: Long) {
                cfg.audioBuffer = pos
            }
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }

        btnEdit.setOnClickListener { startActivity(Intent(this, ButtonEditorActivity::class.java)) }
        btnBack.setOnClickListener { finish() }
    }
}

package com.ultraman.fe0

import android.content.Intent
import android.os.Bundle
import android.widget.Button
import android.widget.SeekBar
import android.widget.TextView
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
        val swVsync = findViewById<SwitchCompat>(R.id.swVsync)
        val swAudio = findViewById<SwitchCompat>(R.id.swAudio)
        val btnBack = findViewById<Button>(R.id.btnBack)
        val btnEdit = findViewById<Button>(R.id.btnEditButtons)

        sbRes.max = 4
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
        swVsync.isChecked = cfg.vsync
        swVsync.setOnCheckedChangeListener { _, v -> cfg.vsync = v }
        swAudio.isChecked = cfg.audioEnabled
        swAudio.setOnCheckedChangeListener { _, v -> cfg.audioEnabled = v }

        btnEdit.setOnClickListener {
            startActivity(Intent(this, ButtonEditorActivity::class.java))
        }
        btnBack.setOnClickListener { finish() }
    }
}

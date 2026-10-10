package com.ultraman.fe0

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.widget.Button
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {
    private lateinit var tvGameName: TextView
    private lateinit var tvCore: TextView
    private lateinit var btnLaunch: Button
    private lateinit var btnPick: Button
    private lateinit var btnSettings: Button
    private val cfg by lazy { ConfigManager(this) }
    private var gameUri: Uri? = null

    private val filePicker = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri ?: return@registerForActivityResult
        try {
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (_: Exception) {}
        gameUri = uri
        val name = getFileName(uri)
        tvGameName.text = name
        btnLaunch.isEnabled = true
        cfg.lastGamePath = uri.toString()
        cfg.lastGameName = name
    }

    private fun getFileName(uri: Uri): String {
        var name = ""
        try {
            contentResolver.query(uri, null, null, null, null)?.use { c ->
                val idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (idx >= 0 && c.moveToFirst()) name = c.getString(idx) ?: ""
            }
        } catch (_: Exception) {}
        if (name.isEmpty()) {
            name = uri.lastPathSegment?.let { Uri.decode(it) } ?: "game.iso"
        }
        return name
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        tvGameName = findViewById(R.id.tvGameName)
        tvCore = findViewById(R.id.tvCore)
        btnLaunch = findViewById(R.id.btnLaunch)
        btnPick = findViewById(R.id.btnPick)
        btnSettings = findViewById(R.id.btnSettings)

        val ok = EmulatorBridge.ensureLoaded()
        tvCore.text = if (ok) "内核: ppsspp" else "内核: 加载失败"

        btnPick.setOnClickListener { filePicker.launch(arrayOf("*/*")) }
        btnLaunch.setOnClickListener {
            val u = gameUri ?: cfg.lastGamePath?.let { Uri.parse(it) }
            if (u != null) startActivity(Intent(this, PlayActivity::class.java).apply { data = u })
        }
        btnSettings.setOnClickListener { startActivity(Intent(this, SettingsActivity::class.java)) }

        cfg.lastGamePath?.let {
            gameUri = Uri.parse(it)
            tvGameName.text = cfg.lastGameName ?: it.substringAfterLast('/')
            btnLaunch.isEnabled = true
        }
    }
}

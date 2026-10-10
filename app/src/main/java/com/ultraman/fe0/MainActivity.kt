package com.ultraman.fe0
import android.content.Intent
import android.net.Uri
import android.os.Bundle
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
        val name = uri.lastPathSegment?.substringAfterLast('/') ?: "game.iso"
        tvGameName.text = name
        btnLaunch.isEnabled = true
        cfg.lastGamePath = uri.toString()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        tvGameName = findViewById(R.id.tvGameName)
        tvCore = findViewById(R.id.tvCore)
        btnLaunch = findViewById(R.id.btnLaunch)
        btnPick = findViewById(R.id.btnPick)
        btnSettings = findViewById(R.id.btnSettings)

        EmulatorBridge.nativeInit(this)
        tvCore.text = "core: ppsspp"

        btnPick.setOnClickListener { filePicker.launch(arrayOf("*/*")) }

        btnLaunch.setOnClickListener {
            val u = gameUri ?: cfg.lastGamePath?.let { Uri.parse(it) }
            if (u != null) startActivity(Intent(this, PlayActivity::class.java).apply { data = u })
        }

        btnSettings.setOnClickListener { startActivity(Intent(this, SettingsActivity::class.java)) }

        cfg.lastGamePath?.let {
            tvGameName.text = it.substringAfterLast('/')
            btnLaunch.isEnabled = true
            gameUri = Uri.parse(it)
        }
    }
}

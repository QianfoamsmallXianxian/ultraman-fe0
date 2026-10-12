package com.pssp

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.ListView
import android.widget.TextView
import android.widget.ImageView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.documentfile.provider.DocumentFile
import java.util.Locale

class MainActivity : AppCompatActivity() {
    private lateinit var tvCore: TextView
    private lateinit var tvDirPath: TextView
    private lateinit var btnPickDir: Button
    private lateinit var btnSettings: Button
    private lateinit var listGames: ListView
    private lateinit var tvEmpty: TextView
    private val cfg by lazy { ConfigManager(this) }
    private val games = mutableListOf<DocumentFile>()

    private val exts = setOf("iso", "cso", "zso", "chd", "pbp", "elf", "prx", "bin")

    private val dirPicker = registerForActivityResult(
        ActivityResultContracts.OpenDocumentTree()
    ) { uri ->
        uri ?: return@registerForActivityResult
        try {
            contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            )
        } catch (_: Exception) {}
        cfg.gamesDirUri = uri.toString()
        scanDir(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        tvCore = findViewById(R.id.tvCore)
        tvDirPath = findViewById(R.id.tvDirPath)
        btnPickDir = findViewById(R.id.btnPickDir)
        btnSettings = findViewById(R.id.btnSettings)
        listGames = findViewById(R.id.listGames)
        tvEmpty = findViewById(R.id.tvEmpty)

        val ok = EmulatorBridge.ensureLoaded()
        tvCore.text = if (ok) "内核: ppsspp" else "内核: 加载失败"

        listGames.setOnItemClickListener { _, _, pos, _ ->
            val f = games.getOrNull(pos) ?: return@setOnItemClickListener
            val name = f.name ?: "game.iso"
            cfg.lastGamePath = f.uri.toString()
            cfg.lastGameName = name
            startActivity(Intent(this, PlayActivity::class.java).apply { data = f.uri })
        }

        btnPickDir.setOnClickListener {
            dirPicker.launch(null)
        }

        btnSettings.setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }

        cfg.gamesDirUri?.let { s ->
            try {
                val u = Uri.parse(s)
                val persisted = contentResolver.persistedUriPermissions
                val has = persisted.any { it.uri == u && it.isReadPermission }
                if (has) scanDir(u) else tvDirPath.text = "目录权限失效，请重新选择"
            } catch (_: Exception) {}
        }
    }

    private fun scanDir(treeUri: Uri) {
        games.clear()
        tvDirPath.text = Uri.decode(treeUri.lastPathSegment ?: treeUri.toString())
        try {
            val root = DocumentFile.fromTreeUri(this, treeUri)
            if (root == null) {
                tvDirPath.text = "无法打开目录"
                refresh()
                return
            }
            collect(root)
        } catch (e: Exception) {
            tvDirPath.text = "扫描失败: ${e.message}"
        }
        refresh()
    }

    private fun collect(dir: DocumentFile) {
        val children = try { dir.listFiles() } catch (_: Exception) { return }
        for (f in children) {
            if (f.isDirectory) {
                collect(f)
            } else {
                val name = f.name ?: continue
                val ext = name.substringAfterLast('.', "").lowercase(Locale.ROOT)
                if (ext in exts) games.add(f)
            }
        }
    }

    private fun refresh() {
        games.sortBy { it.name?.lowercase(Locale.ROOT) ?: "" }
        val names = games.map { it.name ?: "(unknown)" }
        listGames.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_list_item_1,
            names
        )
        tvEmpty.visibility = if (games.isEmpty()) android.view.View.VISIBLE else android.view.View.GONE
        if (games.isEmpty()) {
            tvEmpty.text = if (cfg.gamesDirUri == null) "选择目录后，这里会列出所有游戏"
                            else "该目录下未找到 PSP 镜像"
        }
    }
}

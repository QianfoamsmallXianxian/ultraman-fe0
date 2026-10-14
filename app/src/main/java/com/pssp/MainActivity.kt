package com.pssp

import android.content.Intent
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.widget.BaseAdapter
import android.widget.Button
import android.widget.ImageView
import android.widget.ListView
import android.widget.TextView
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

    private val exts = setOf("iso", "cso", "zso", "chd", "pbp", "elf", "prx", "bin")
    private val imgExts = setOf("jpg", "jpeg", "png", "webp")

    data class GameItem(val file: DocumentFile, val name: String, val cover: Uri?)

    private val games = mutableListOf<GameItem>()

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
        tvCore.text = if (ok) "内核: ppsspp" else " 内核: 加载失败"

        listGames.adapter = GameAdapter()
        listGames.setOnItemClickListener { _, _, pos, _ ->
            val it = games.getOrNull(pos) ?: return@setOnItemClickListener
            cfg.lastGamePath = it.file.uri.toString()
            cfg.lastGameName = it.name
            startActivity(Intent(this, PlayActivity::class.java).apply { data = it.file.uri })
        }

        btnPickDir.setOnClickListener { dirPicker.launch(null) }
        btnSettings.setOnClickListener { startActivity(Intent(this, SettingsActivity::class.java)) }

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
        val covers = HashMap<String, Uri>()
        for (f in children) {
            if (f.isDirectory) continue
            val n = f.name ?: continue
            val ext = n.substringAfterLast('.', "").lowercase(Locale.ROOT)
            if (ext in imgExts) {
                val base = n.substringBeforeLast('.').lowercase(Locale.ROOT)
                covers[base] = f.uri
            }
        }
        for (f in children) {
            if (f.isDirectory) {
                collect(f)
            } else {
                val name = f.name ?: continue
                val ext = name.substringAfterLast('.', "").lowercase(Locale.ROOT)
                if (ext in exts) {
                    val base = name.substringBeforeLast('.').lowercase(Locale.ROOT)
                    games.add(GameItem(f, name, covers[base]))
                }
            }
        }
    }

    private fun refresh() {
        games.sortBy { it.name.lowercase(Locale.ROOT) }
        (listGames.adapter as BaseAdapter).notifyDataSetChanged()
        tvEmpty.visibility = if (games.isEmpty()) View.VISIBLE else View.GONE
        if (games.isEmpty()) {
            tvEmpty.text = if (cfg.gamesDirUri == null) "选择目录后，这里会列出所有游戏"
                            else "该目录下未找到 PSP 镜像"
        }
    }

    private inner class GameAdapter : BaseAdapter() {
        override fun getCount(): Int = games.size
        override fun getItem(position: Int): Any = games[position]
        override fun getItemId(position: Int): Long = position.toLong()

        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
            val v = convertView ?: layoutInflater.inflate(R.layout.item_game, parent, false)
            val it = games[position]
            v.findViewById<TextView>(R.id.tvName).text = it.name
            v.findViewById<TextView>(R.id.tvSub).text = Uri.decode(it.file.uri.lastPathSegment ?: "")
            val iv = v.findViewById<ImageView>(R.id.ivCover)

            iv.setImageResource(R.drawable.ic_ultraman)
            if (it.cover != null) {
                try {
                    val stream = contentResolver.openInputStream(it.cover)
                    val bmp = BitmapFactory.decodeStream(stream)
                    stream?.close()
                    if (bmp != null) iv.setImageBitmap(bmp)
                } catch (_: Exception) {}
            } else {
                iv.tag = it.name
                CoverDownloader.load(this@MainActivity, it.name) { bmp ->
                    if (iv.tag == it.name && bmp != null) iv.setImageBitmap(bmp)
                }
            }
            return v
        }
    }
}

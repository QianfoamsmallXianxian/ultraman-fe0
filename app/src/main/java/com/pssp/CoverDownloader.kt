package com.pssp

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Handler
import android.os.Looper
import android.util.LruCache
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.security.MessageDigest
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors

object CoverDownloader {
    private const val BASE = "https://thumbnails.libretro.com/Sony%20-%20PlayStation%20Portable/Named_Boxarts/"
    private val pool = Executors.newFixedThreadPool(4)
    private val mainHandler = Handler(Looper.getMainLooper())
    private val memCache = LruCache<String, Bitmap>(60)
    private val inFlight = ConcurrentHashMap<String, Boolean>()

    fun coverUrl(gameName: String): String {
        val noExt = gameName.substringBeforeLast('.')
        val encoded = noExt.split("/").joinToString("/") {
            URLEncoder.encode(it, "UTF-8").replace("+", "%20")
        }
        return BASE + encoded + ".png"
    }

    fun cacheFile(ctx: Context, gameName: String): File {
        val noExt = gameName.substringBeforeLast('.')
        val hash = md5(noExt)
        val dir = File(ctx.cacheDir, "covers")
        if (!dir.exists()) dir.mkdirs()
        return File(dir, "$hash.png")
    }

    fun load(ctx: Context, gameName: String, callback: (Bitmap?) -> Unit) {
        val key = gameName.substringBeforeLast('.')
        val cached = memCache.get(key)
        if (cached != null) {
            callback(cached)
            return
        }
        val disk = cacheFile(ctx, gameName)
        if (disk.exists()) {
            val bmp = BitmapFactory.decodeFile(disk.absolutePath)
            if (bmp != null) {
                memCache.put(key, bmp)
                callback(bmp)
                return
            }
        }
        if (inFlight.containsKey(key)) return
        inFlight[key] = true
        pool.execute {
            var bmp: Bitmap? = null
            try {
                val url = URL(coverUrl(gameName))
                val conn = url.openConnection() as HttpURLConnection
                conn.connectTimeout = 5000
                conn.readTimeout = 5000
                conn.setRequestProperty("User-Agent", "PSSP-Android/1.0")
                if (conn.responseCode == 200) {
                    val bytes = conn.inputStream.readBytes()
                    bmp = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                    if (bmp != null) {
                        try { disk.writeBytes(bytes) } catch (_: Exception) {}
                    }
                }
                conn.disconnect()
            } catch (_: Exception) {}
            if (bmp != null) memCache.put(key, bmp)
            inFlight.remove(key)
            val result = bmp
            mainHandler.post { callback(result) }
        }
    }

    private fun md5(s: String): String {
        val md = MessageDigest.getInstance("MD5")
        val d = md.digest(s.toByteArray())
        return d.joinToString("") { "%02x".format(it) }
    }
}

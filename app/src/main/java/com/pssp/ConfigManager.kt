package com.pssp

import android.content.Context

class ConfigManager(ctx: Context) {
    private val sp = ctx.getSharedPreferences("ultraman_config", Context.MODE_PRIVATE)

    var resolution: Int
        get() = sp.getInt("resolution", 2).coerceIn(1, 3)
        set(v) = sp.edit().putInt("resolution", v).apply()

    var fullscreen: Boolean
        get() = sp.getBoolean("fullscreen", true)
        set(v) = sp.edit().putBoolean("fullscreen", v).apply()

    var vsync: Boolean
        get() = sp.getBoolean("vsync", true)
        set(v) = sp.edit().putBoolean("vsync", v).apply()

    val audioEnabled: Boolean
        get() = true

    var gamesDirUri: String?
        get() = sp.getString("games_dir_uri", null)
        set(v) = sp.edit().putString("games_dir_uri", v).apply()

    var showFps: Boolean
        get() = sp.getBoolean("show_fps", true)
        set(v) = sp.edit().putBoolean("show_fps", v).apply()

    var audioBuffer: Int
        get() = sp.getInt("audio_buffer", 1)
        set(v) = sp.edit().putInt("audio_buffer", v).apply()

    var lastGamePath: String?
        get() = sp.getString("last_game", null)
        set(v) = sp.edit().putString("last_game", v).apply()

    var lastGameName: String?
        get() = sp.getString("last_game_name", null)
        set(v) = sp.edit().putString("last_game_name", v).apply()

    companion object {
        const val BACKEND_OPENGL = 0
        const val BACKEND_VULKAN = 1
    }
}

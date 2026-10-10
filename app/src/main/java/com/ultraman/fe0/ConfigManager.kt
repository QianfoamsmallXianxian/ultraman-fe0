package com.ultraman.fe0

import android.content.Context

class ConfigManager(ctx: Context) {
    private val sp = ctx.getSharedPreferences("ultraman_config", Context.MODE_PRIVATE)
    var resolution: Int
        get() = sp.getInt("resolution", 2)
        set(v) = sp.edit().putInt("resolution", v).apply()
    var fullscreen: Boolean
        get() = sp.getBoolean("fullscreen", true)
        set(v) = sp.edit().putBoolean("fullscreen", v).apply()
    var vsync: Boolean
        get() = sp.getBoolean("vsync", true)
        set(v) = sp.edit().putBoolean("vsync", v).apply()
    var audioEnabled: Boolean
        get() = sp.getBoolean("audio", true)
        set(v) = sp.edit().putBoolean("audio", v).apply()
    var backend: Int
        get() = sp.getInt("backend", BACKEND_OPENGL)
        set(v) = sp.edit().putInt("backend", v).apply()
    var lastGamePath: String?
        get() = sp.getString("last_game", null)
        set(v) = sp.edit().putString("last_game", v).apply()
    var lastGameName: String?
        get() = sp.getString("last_game_name", null)
        set(v) = sp.edit().putString("last_game_name", v).apply()
    companion object {
        const val BACKEND_OPENGL = 0
        const val BACKEND_VULKAN = 1
        const val BACKEND_SOFTWARE = 2
    }
}

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
    var lastGamePath: String?
        get() = sp.getString("last_game", null)
        set(v) = sp.edit().putString("last_game", v).apply()
}

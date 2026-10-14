package com.pssp

import android.content.Context

data class ButtonDef(
    val label: String,
    var nx: Float,
    var ny: Float,
    var r: Float,
    var alpha: Float,
    val id: Int,
    var type: Int = 0
)

class ButtonConfigManager(ctx: Context) {
    private val sp = ctx.getSharedPreferences("ultraman_buttons", Context.MODE_PRIVATE)

    companion object {
        const val TYPE_KEY = 0
        const val TYPE_ANALOG = 1
        const val JOY_LEFT = -1
        const val JOY_RIGHT = -2

        val DEFAULT = listOf(
            ButtonDef("L",   0.08f,  0.10f,  0.055f, 0.5f, EmulatorBridge.BTN_L),
            ButtonDef("R",   0.92f,  0.10f,  0.055f, 0.5f, EmulatorBridge.BTN_R),
            ButtonDef("^",   0.115f, 0.592f, 0.055f, 0.5f, EmulatorBridge.BTN_UP),
            ButtonDef("v",   0.115f, 0.888f, 0.055f, 0.5f, EmulatorBridge.BTN_DOWN),
            ButtonDef("<",   0.048f, 0.74f,  0.055f, 0.5f, EmulatorBridge.BTN_LEFT),
            ButtonDef(">",   0.182f, 0.74f,  0.055f, 0.5f, EmulatorBridge.BTN_RIGHT),
            ButtonDef("SEL", 0.44f,  0.92f,  0.045f, 0.5f, EmulatorBridge.BTN_SELECT),
            ButtonDef("STA", 0.56f,  0.92f,  0.045f, 0.5f, EmulatorBridge.BTN_START),
            ButtonDef("△",   0.86f,  0.572f, 0.055f, 0.5f, EmulatorBridge.BTN_X),
            ButtonDef("○",   0.927f, 0.72f,  0.055f, 0.5f, EmulatorBridge.BTN_A),
            ButtonDef("✕",   0.793f, 0.72f,  0.055f, 0.5f, EmulatorBridge.BTN_B),
            ButtonDef("□",   0.86f,  0.868f, 0.055f, 0.5f, EmulatorBridge.BTN_Y),
            ButtonDef("L3",  0.30f,  0.72f,  0.095f, 0.5f, JOY_LEFT,  TYPE_ANALOG)
        )
    }

    fun load(): MutableList<ButtonDef> {
        if (!sp.contains("btn_0")) return DEFAULT.toMutableList()
        val list = mutableListOf<ButtonDef>()
        for (i in DEFAULT.indices) {
            val s = sp.getString("btn_$i", null) ?: continue
            val p = s.split("|")
            if (p.size >= 6) {
                list.add(ButtonDef(
                    p[0],
                    p[1].toFloatOrNull() ?: 0f,
                    p[2].toFloatOrNull() ?: 0f,
                    p[3].toFloatOrNull() ?: 0.05f,
                    p[4].toFloatOrNull() ?: 0.5f,
                    p[5].toIntOrNull() ?: 0,
                    if (p.size >= 7) p[6].toIntOrNull() ?: 0 else 0
                ))
            }
        }
        return if (list.size == DEFAULT.size) list else DEFAULT.toMutableList()
    }

    fun save(list: List<ButtonDef>) {
        val e = sp.edit()
        list.forEachIndexed { i, b ->
            e.putString("btn_$i", "${b.label}|${b.nx}|${b.ny}|${b.r}|${b.alpha}|${b.id}|${b.type}")
        }
        e.apply()
    }

    fun reset() { sp.edit().clear().apply() }
}

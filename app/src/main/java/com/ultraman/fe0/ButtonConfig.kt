package com.ultraman.fe0

import android.content.Context

data class ButtonDef(
    val label: String,
    var nx: Float,
    var ny: Float,
    var r: Float,
    var alpha: Float,
    val id: Int
)

class ButtonConfigManager(ctx: Context) {
    private val sp = ctx.getSharedPreferences("ultraman_buttons", Context.MODE_PRIVATE)

    companion object {
        val DEFAULT = listOf(
            ButtonDef("L", 0.08f, 0.10f, 0.055f, 0.5f, EmulatorBridge.BTN_L),
            ButtonDef("R", 0.92f, 0.10f, 0.055f, 0.5f, EmulatorBridge.BTN_R),
            ButtonDef("^", 0.12f, 0.62f, 0.055f, 0.5f, EmulatorBridge.BTN_UP),
            ButtonDef("v", 0.12f, 0.86f, 0.055f, 0.5f, EmulatorBridge.BTN_DOWN),
            ButtonDef("<", 0.05f, 0.74f, 0.055f, 0.5f, EmulatorBridge.BTN_LEFT),
            ButtonDef(">", 0.19f, 0.74f, 0.055f, 0.5f, EmulatorBridge.BTN_RIGHT),
            ButtonDef("SEL", 0.42f, 0.90f, 0.045f, 0.5f, EmulatorBridge.BTN_SELECT),
            ButtonDef("STA", 0.58f, 0.90f, 0.045f, 0.5f, EmulatorBridge.BTN_START),
            ButtonDef("T", 0.82f, 0.56f, 0.055f, 0.5f, EmulatorBridge.BTN_Y),
            ButtonDef("X", 0.74f, 0.72f, 0.055f, 0.5f, EmulatorBridge.BTN_X),
            ButtonDef("O", 0.90f, 0.72f, 0.055f, 0.5f, EmulatorBridge.BTN_B),
            ButtonDef("S", 0.82f, 0.88f, 0.055f, 0.5f, EmulatorBridge.BTN_A)
        )
    }

    fun load(): MutableList<ButtonDef> {
        if (!sp.contains("btn_0")) return DEFAULT.toMutableList()
        val list = mutableListOf<ButtonDef>()
        for (i in DEFAULT.indices) {
            val s = sp.getString("btn_$i", null) ?: continue
            val p = s.split("|")
            if (p.size == 6) {
                list.add(ButtonDef(p[0],
                    p[1].toFloatOrNull() ?: 0f,
                    p[2].toFloatOrNull() ?: 0f,
                    p[3].toFloatOrNull() ?: 0.05f,
                    p[4].toFloatOrNull() ?: 0.5f,
                    p[5].toIntOrNull() ?: 0))
            } else if (p.size == 5) {
                list.add(ButtonDef(p[0],
                    p[1].toFloatOrNull() ?: 0f,
                    p[2].toFloatOrNull() ?: 0f,
                    p[3].toFloatOrNull() ?: 0.05f,
                    0.5f,
                    p[4].toIntOrNull() ?: 0))
            }
        }
        return if (list.size == DEFAULT.size) list else DEFAULT.toMutableList()
    }

    fun save(list: List<ButtonDef>) {
        val e = sp.edit()
        list.forEachIndexed { i, b ->
            e.putString("btn_$i", "${b.label}|${b.nx}|${b.ny}|${b.r}|${b.alpha}|${b.id}")
        }
        e.apply()
    }

    fun reset() { sp.edit().clear().apply() }
}

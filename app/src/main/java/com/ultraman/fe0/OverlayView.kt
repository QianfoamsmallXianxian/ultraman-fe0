package com.ultraman.fe0

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.min

class OverlayView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : View(context, attrs) {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val held = HashMap<Int, Int>()

    data class Btn(val label: String, val nx: Float, val ny: Float, val rRatio: Float, val id: Int)

    private val buttons = listOf(
        Btn("L", 0.08f, 0.10f, 0.055f, EmulatorBridge.BTN_L),
        Btn("R", 0.92f, 0.10f, 0.055f, EmulatorBridge.BTN_R),
        Btn("^", 0.12f, 0.62f, 0.055f, EmulatorBridge.BTN_UP),
        Btn("v", 0.12f, 0.86f, 0.055f, EmulatorBridge.BTN_DOWN),
        Btn("<", 0.05f, 0.74f, 0.055f, EmulatorBridge.BTN_LEFT),
        Btn(">", 0.19f, 0.74f, 0.055f, EmulatorBridge.BTN_RIGHT),
        Btn("SEL", 0.42f, 0.90f, 0.045f, EmulatorBridge.BTN_SELECT),
        Btn("STA", 0.58f, 0.90f, 0.045f, EmulatorBridge.BTN_START),
        Btn("T", 0.82f, 0.56f, 0.055f, EmulatorBridge.BTN_Y),
        Btn("X", 0.74f, 0.72f, 0.055f, EmulatorBridge.BTN_X),
        Btn("O", 0.90f, 0.72f, 0.055f, EmulatorBridge.BTN_B),
        Btn("S", 0.82f, 0.88f, 0.055f, EmulatorBridge.BTN_A)
    )

    init {
        paint.textAlign = Paint.Align.CENTER
        isFocusable = true
    }

    private fun px(b: Btn): Float = b.nx * width
    private fun py(b: Btn): Float = b.ny * height
    private fun pr(b: Btn): Float = min(width, height) * b.rRatio

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                val b = hit(event.getX(i), event.getY(i))
                if (b != null) {
                    held[event.getPointerId(i)] = b.id
                    EmulatorBridge.nativeKey(0, b.id, true)
                    invalidate()
                }
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) {
                    val pid = event.getPointerId(i)
                    val cur = hit(event.getX(i), event.getY(i))
                    val old = held[pid]
                    if (cur?.id != old) {
                        if (old != null) EmulatorBridge.nativeKey(0, old, false)
                        if (cur != null) {
                            held[pid] = cur.id
                            EmulatorBridge.nativeKey(0, cur.id, true)
                        } else held.remove(pid)
                    }
                }
                invalidate()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val pid = event.getPointerId(event.actionIndex)
                held.remove(pid)?.let { EmulatorBridge.nativeKey(0, it, false) }
                invalidate()
            }
            MotionEvent.ACTION_CANCEL -> {
                held.values.forEach { EmulatorBridge.nativeKey(0, it, false) }
                held.clear()
                invalidate()
            }
        }
        return true
    }

    private fun hit(x: Float, y: Float): Btn? {
        for (b in buttons) {
            val dx = x - px(b)
            val dy = y - py(b)
            val r = pr(b)
            if (dx * dx + dy * dy < r * r) return b
        }
        return null
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        for (b in buttons) {
            val cx = px(b)
            val cy = py(b)
            val r = pr(b)
            val pressed = held.values.contains(b.id)
            paint.style = Paint.Style.FILL
            paint.color = if (pressed) 0x99FFD24A.toInt() else 0x44000000
            canvas.drawCircle(cx, cy, r, paint)
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = 4f
            paint.color = if (pressed) 0xFFFFD24A.toInt() else 0xAA4A8CFF.toInt()
            canvas.drawCircle(cx, cy, r, paint)
            paint.style = Paint.Style.FILL
            paint.color = Color.WHITE
            paint.textSize = r * 0.9f
            val fm = paint.fontMetrics
            val baseline = cy - (fm.ascent + fm.descent) / 2f
            canvas.drawText(b.label, cx, baseline, paint)
        }
    }
}

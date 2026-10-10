package com.ultraman.fe0

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View

class OverlayView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : View(context, attrs) {

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val held = HashMap<Int, Int>()

    data class Btn(val label: String, val cx: Float, val cy: Float, val r: Float, val id: Int)

    private val buttons = listOf(
        Btn("L", 0.06f, 0.78f, 0.055f, EmulatorBridge.BTN_L),
        Btn("R", 0.94f, 0.78f, 0.055f, EmulatorBridge.BTN_R),
        Btn("A", 0.90f, 0.86f, 0.050f, EmulatorBridge.BTN_A),
        Btn("B", 0.82f, 0.74f, 0.050f, EmulatorBridge.BTN_B),
        Btn("X", 0.74f, 0.86f, 0.050f, EmulatorBridge.BTN_X),
        Btn("Y", 0.82f, 0.98f, 0.050f, EmulatorBridge.BTN_Y),
        Btn("^", 0.14f, 0.74f, 0.045f, EmulatorBridge.BTN_UP),
        Btn("v", 0.14f, 0.98f, 0.045f, EmulatorBridge.BTN_DOWN),
        Btn("<", 0.06f, 0.86f, 0.045f, EmulatorBridge.BTN_LEFT),
        Btn(">", 0.22f, 0.86f, 0.045f, EmulatorBridge.BTN_RIGHT),
        Btn("SEL", 0.42f, 0.98f, 0.045f, EmulatorBridge.BTN_SELECT),
        Btn("STA", 0.58f, 0.98f, 0.045f, EmulatorBridge.BTN_START)
    )

    init {
        paint.textAlign = Paint.Align.CENTER
        isFocusable = true
    }

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
        val w = width.toFloat()
        val h = height.toFloat()
        for (b in buttons) {
            val dx = (x / w - b.cx) * w
            val dy = (y / h - b.cy) * h * 1.4f
            if (dx * dx + dy * dy < (b.r * w) * (b.r * w)) return b
        }
        return null
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        for (b in buttons) {
            val pressed = held.values.contains(b.id)
            paint.style = Paint.Style.FILL
            paint.color = if (pressed) 0x884A8CFF.toInt() else 0x33000000
            canvas.drawCircle(b.cx * w, b.cy * h, b.r * w, paint)
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = 3f
            paint.color = 0x887AB7FF.toInt()
            canvas.drawCircle(b.cx * w, b.cy * h, b.r * w, paint)
            paint.style = Paint.Style.FILL
            paint.color = Color.WHITE
            paint.textSize = b.r * w * 1.1f
            canvas.drawText(b.label, b.cx * w, b.cy * h + paint.textSize / 3, paint)
        }
    }
}

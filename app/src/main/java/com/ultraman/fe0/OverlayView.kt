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
    private val buttons: MutableList<ButtonDef> = ButtonConfigManager(context).load()

    init {
        paint.textAlign = Paint.Align.CENTER
        isFocusable = true
    }

    private fun px(b: ButtonDef): Float = b.nx * width
    private fun py(b: ButtonDef): Float = b.ny * height
    private fun pr(b: ButtonDef): Float = min(width, height) * b.r

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

    private fun hit(x: Float, y: Float): ButtonDef? {
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
            val a = b.alpha.coerceIn(0f, 1f)

            paint.style = Paint.Style.FILL
            if (pressed) {
                paint.color = Color.argb((255 * a).toInt(), 0xFF, 0xD2, 0x4A)
            } else {
                paint.color = Color.argb((100 * a).toInt(), 0, 0, 0)
            }
            canvas.drawCircle(cx, cy, r, paint)

            paint.style = Paint.Style.STROKE
            paint.strokeWidth = 4f
            if (pressed) {
                paint.color = Color.argb((255 * a).toInt(), 0xFF, 0xD2, 0x4A)
            } else {
                paint.color = Color.argb((170 * a).toInt(), 0x4A, 0x8C, 0xFF)
            }
            canvas.drawCircle(cx, cy, r, paint)

            paint.style = Paint.Style.FILL
            paint.color = Color.argb((255 * a).toInt(), 0xFF, 0xFF, 0xFF)
            paint.textSize = r * 0.9f
            val fm = paint.fontMetrics
            val baseline = cy - (fm.ascent + fm.descent) / 2f
            canvas.drawText(b.label, cx, baseline, paint)
        }
    }
}

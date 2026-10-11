package com.ultraman.fe0

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.util.Log
import android.view.MotionEvent
import android.view.View
import kotlin.math.min

class OverlayView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : View(context, attrs) {

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val held = HashMap<Int, List<Int>>()
    private val buttons: MutableList<ButtonDef> = ButtonConfigManager(context).load()

    init {
        paint.textAlign = Paint.Align.CENTER
        isFocusable = true
        isClickable = true
    }

    private fun px(b: ButtonDef): Float = b.nx * width
    private fun py(b: ButtonDef): Float = b.ny * height
    private fun pr(b: ButtonDef): Float = min(width, height) * b.r

    private fun hitAll(x: Float, y: Float): List<Int> {
        val res = mutableListOf<Int>()
        val w = width.toFloat()
        val h = height.toFloat()
        for (b in buttons) {
            val dx = (x - px(b)) / pr(b)
            val dy = (y - py(b)) / pr(b) * 1.6f
            val d2 = dx * dx + dy * dy
            if (d2 < 1.75f * 1.75f) {
                res.add(b.id)
            }
        }
        return res
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                val pid = event.getPointerId(i)
                val hits = hitAll(event.getX(i), event.getY(i))
                val prev = held[pid] ?: emptyList()

                for (old in prev) {
                    if (old !in hits) EmulatorBridge.nativeKey(0, old, false)
                }
                for (nw in hits) {
                    if (nw !in prev) EmulatorBridge.nativeKey(0, nw, true)
                }

                held[pid] = hits
                parent?.requestDisallowInterceptTouchEvent(true)
                invalidate()
            }

            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) {
                    val pid = event.getPointerId(i)
                    val hits = hitAll(event.getX(i), event.getY(i))
                    val prev = held[pid] ?: emptyList()

                    for (old in prev) {
                        if (old !in hits) EmulatorBridge.nativeKey(0, old, false)
                    }
                    for (nw in hits) {
                        if (nw !in prev) EmulatorBridge.nativeKey(0, nw, true)
                    }
                    held[pid] = hits
                }
                invalidate()
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val pid = event.getPointerId(event.actionIndex)
                held.remove(pid)?.forEach { EmulatorBridge.nativeKey(0, it, false) }
                invalidate()
            }

            MotionEvent.ACTION_CANCEL -> {
                held.values.flatten().forEach { EmulatorBridge.nativeKey(0, it, false) }
                held.clear()
                invalidate()
            }
        }
        return true
    }

    private fun isPressed(id: Int): Boolean {
        for (list in held.values) {
            if (id in list) return true
        }
        return false
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        for (b in buttons) {
            val cx = px(b)
            val cy = py(b)
            val r = pr(b)
            val pressed = isPressed(b.id)
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
            canvas.drawText(b.label, cx, cy - (fm.ascent + fm.descent) / 2f, paint)
        }
    }
}

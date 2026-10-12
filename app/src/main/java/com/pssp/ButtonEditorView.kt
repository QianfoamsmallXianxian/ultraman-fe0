package com.pssp

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.min

class ButtonEditorView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : View(context, attrs) {
    val buttons: MutableList<ButtonDef> = ButtonConfigManager(context).load()
    private var selected = -1
    private var dragDx = 0f
    private var dragDy = 0f
    var onSelectionChanged: ((Int) -> Unit)? = null
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)

    init { paint.textAlign = Paint.Align.CENTER; isFocusable = true }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val sw = width.toFloat(); val sh = height.toFloat()
        for (i in buttons.indices) {
            val b = buttons[i]
            val cx = b.nx * sw; val cy = b.ny * sh; val r = min(sw, sh) * b.r
            val a = b.alpha.coerceIn(0f, 1f)
            paint.style = Paint.Style.FILL
            paint.color = if (i == selected) Color.argb((255 * a).toInt(), 0xFF, 0xD2, 0x4A) else Color.argb((100 * a).toInt(), 0, 0, 0)
            canvas.drawCircle(cx, cy, r, paint)
            paint.style = Paint.Style.STROKE; paint.strokeWidth = 4f
            paint.color = if (i == selected) Color.argb((255 * a).toInt(), 0xFF, 0xD2, 0x4A) else Color.argb((170 * a).toInt(), 0x4A, 0x8C, 0xFF)
            canvas.drawCircle(cx, cy, r, paint)
            paint.style = Paint.Style.FILL
            paint.color = Color.argb((255 * a).toInt(), 0xFF, 0xFF, 0xFF)
            paint.textSize = r * 0.9f
            val fm = paint.fontMetrics
            canvas.drawText(b.label, cx, cy - (fm.ascent + fm.descent) / 2f, paint)
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val sw = width.toFloat(); val sh = height.toFloat()
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val x = event.x; val y = event.y
                var hit = -1
                for (i in buttons.indices.reversed()) {
                    val b = buttons[i]
                    val dx = x - b.nx * sw; val dy = y - b.ny * sh
                    val r = min(sw, sh) * b.r
                    if (dx * dx + dy * dy < r * r) { hit = i; break }
                }
                selected = hit
                if (hit >= 0) { dragDx = event.x - buttons[hit].nx * sw; dragDy = event.y - buttons[hit].ny * sh }
                onSelectionChanged?.invoke(hit)
                invalidate()
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (selected >= 0) {
                    val b = buttons[selected]
                    b.nx = ((event.x - dragDx) / sw).coerceIn(0.03f, 0.97f)
                    b.ny = ((event.y - dragDy) / sh).coerceIn(0.03f, 0.97f)
                    invalidate()
                }
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> return true
        }
        return super.onTouchEvent(event)
    }

    fun setSelectedSize(r: Float) { if (selected >= 0) { buttons[selected].r = r.coerceIn(0.03f, 0.12f); invalidate() } }
    fun getSelectedSize(): Float = if (selected >= 0) buttons[selected].r else 0.055f
    fun setSelectedAlpha(a: Float) { if (selected >= 0) { buttons[selected].alpha = a.coerceIn(0f, 1f); invalidate() } }
    fun getSelectedAlpha(): Float = if (selected >= 0) buttons[selected].alpha else 0.5f
}

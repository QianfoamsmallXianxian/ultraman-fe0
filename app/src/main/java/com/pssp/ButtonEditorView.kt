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

    private val groups: List<List<Int>> = listOf(
        listOf(0, 1),
        listOf(2, 3, 4, 5),
        listOf(6, 7),
        listOf(8, 9, 10, 11),
        listOf(12)
    )

    private var selectedGroup = -1
    private var dragX = 0f
    private var dragY = 0f
    var onSelectionChanged: ((Int) -> Unit)? = null
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)

    init {
        paint.textAlign = Paint.Align.CENTER
        isFocusable = true
    }

    private fun groupOf(btnIdx: Int): Int = groups.indexOfFirst { it.contains(btnIdx) }

    private fun groupCenter(g: Int): Pair<Float, Float> {
        val list = groups[g]
        val cx = list.map { buttons[it].nx }.average().toFloat()
        val cy = list.map { buttons[it].ny }.average().toFloat()
        return cx to cy
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val sw = width.toFloat()
        val sh = height.toFloat()
        for (i in buttons.indices) {
            val b = buttons[i]
            val cx = b.nx * sw
            val cy = b.ny * sh
            val r = min(sw, sh) * b.r
            val a = b.alpha.coerceIn(0f, 1f)
            val isSel = selectedGroup >= 0 && groups[selectedGroup].contains(i)

            paint.style = Paint.Style.FILL
            paint.color = if (isSel)
                Color.argb((255 * a).toInt(), 0xFF, 0xD2, 0x4A)
            else
                Color.argb((100 * a).toInt(), 0, 0, 0)
            canvas.drawCircle(cx, cy, r, paint)

            paint.style = Paint.Style.STROKE
            paint.strokeWidth = 4f
            paint.color = if (isSel)
                Color.argb((255 * a).toInt(), 0xFF, 0xD2, 0x4A)
            else
                Color.argb((170 * a).toInt(), 0x4A, 0x8C, 0xFF)
            canvas.drawCircle(cx, cy, r, paint)

            paint.style = Paint.Style.FILL
            paint.color = Color.argb((255 * a).toInt(), 0xFF, 0xFF, 0xFF)
            paint.textSize = r * 0.9f
            val fm = paint.fontMetrics
            canvas.drawText(b.label, cx, cy - (fm.ascent + fm.descent) / 2f, paint)
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val sw = width.toFloat()
        val sh = height.toFloat()
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val x = event.x
                val y = event.y
                var hitBtn = -1
                for (i in buttons.indices.reversed()) {
                    val b = buttons[i]
                    val dx = x - b.nx * sw
                    val dy = y - b.ny * sh
                    val r = min(sw, sh) * b.r
                    if (dx * dx + dy * dy < r * r) { hitBtn = i; break }
                }
                selectedGroup = if (hitBtn >= 0) groupOf(hitBtn) else -1
                if (selectedGroup >= 0) {
                    dragX = x
                    dragY = y
                }
                onSelectionChanged?.invoke(selectedGroup)
                invalidate()
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (selectedGroup >= 0) {
                    val dxN = (event.x - dragX) / sw
                    val dyN = (event.y - dragY) / sh
                    val group = groups[selectedGroup]
                    var minX = Float.MAX_VALUE
                    var maxX = -Float.MAX_VALUE
                    var minY = Float.MAX_VALUE
                    var maxY = -Float.MAX_VALUE
                    for (i in group) {
                        val nx = buttons[i].nx + dxN
                        val ny = buttons[i].ny + dyN
                        if (nx < minX) minX = nx
                        if (nx > maxX) maxX = nx
                        if (ny < minY) minY = ny
                        if (ny > maxY) maxY = ny
                    }
                    var ddx = dxN
                    var ddy = dyN
                    if (minX < 0.03f) ddx += 0.03f - minX
                    if (maxX > 0.97f) ddx -= maxX - 0.97f
                    if (minY < 0.03f) ddy += 0.03f - minY
                    if (maxY > 0.97f) ddy -= maxY - 0.97f
                    for (i in group) {
                        buttons[i].nx += ddx
                        buttons[i].ny += ddy
                    }
                    dragX = event.x
                    dragY = event.y
                    invalidate()
                }
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> return true
        }
        return super.onTouchEvent(event)
    }

    fun setSelectedSize(r: Float) {
        if (selectedGroup < 0) return
        val center = groupCenter(selectedGroup)
        val cx = center.first
        val cy = center.second
        val curAvgR = groups[selectedGroup].map { buttons[it].r }.average().toFloat()
        if (curAvgR <= 0.0001f) return
        val target = r.coerceIn(0.03f, 0.12f)
        val scale = target / curAvgR
        for (i in groups[selectedGroup]) {
            buttons[i].nx = cx + (buttons[i].nx - cx) * scale
            buttons[i].ny = cy + (buttons[i].ny - cy) * scale
            buttons[i].r = (buttons[i].r * scale).coerceIn(0.03f, 0.12f)
        }
        invalidate()
    }

    fun getSelectedSize(): Float {
        if (selectedGroup < 0) return 0.055f
        return groups[selectedGroup].map { buttons[it].r }.average().toFloat()
    }

    fun setSelectedAlpha(a: Float) {
        if (selectedGroup < 0) return
        val v = a.coerceIn(0f, 1f)
        for (i in groups[selectedGroup]) buttons[i].alpha = v
        invalidate()
    }

    fun getSelectedAlpha(): Float {
        if (selectedGroup < 0) return 0.5f
        return groups[selectedGroup].map { buttons[it].alpha }.average().toFloat()
    }
}

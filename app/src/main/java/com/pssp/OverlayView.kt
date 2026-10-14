package com.pssp

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.os.Handler
import android.os.Looper
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.min
import kotlin.math.sqrt

class OverlayView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : View(context, attrs) {

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val fpsPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = Paint.Align.LEFT
        textSize = 32f
        typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
        setShadowLayer(6f, 2f, 2f, 0xFF000000.toInt())
    }
    private val held = HashMap<Int, List<Int>>()
    private val buttons: MutableList<ButtonDef> = ButtonConfigManager(context).load()
    private val cfg = ConfigManager(context)

    private var joyPid = -1
    private var joyRef: ButtonDef? = null
    private var joyDx = 0f
    private var joyDy = 0f
    private var joyMaxR = 0f
    private var joyCx = 0f
    private var joyCy = 0f

    private val fpsHandler = Handler(Looper.getMainLooper())
    private var lastFrameCount = 0L
    private var lastTime = 0L
    private var fpsValue = 0f

    @Volatile var isVisible = true

    private val fpsRunnable = object : Runnable {
        override fun run() {
            if (!isVisible) {
                fpsHandler.postDelayed(this, 500)
                return
            }
            val now = System.currentTimeMillis()
            val count = EmulatorBridge.nativeGetFrameCount()
            if (lastTime > 0 && now - lastTime > 200) {
                val dt = now - lastTime
                val raw = (count - lastFrameCount) * 1000f / dt
                fpsValue = if (fpsValue <= 0f) raw else fpsValue * 0.6f + raw * 0.4f
            }
            lastFrameCount = count
            lastTime = now
            if (cfg.showFps) invalidate()
            fpsHandler.postDelayed(this, 500)
        }
    }

    init {
        paint.textAlign = Paint.Align.CENTER
        isFocusable = true
        isClickable = true
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        fpsHandler.post(fpsRunnable)
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        fpsHandler.removeCallbacks(fpsRunnable)
    }

    private fun px(b: ButtonDef): Float = b.nx * width
    private fun py(b: ButtonDef): Float = b.ny * height
    private fun pr(b: ButtonDef): Float = min(width, height) * b.r

    private fun hitAll(x: Float, y: Float): List<Int> {
        val res = mutableListOf<Int>()
        for (b in buttons) {
            if (b.type != 0) continue
            val dx = x - px(b)
            val dy = y - py(b)
            val r = pr(b) * 2.5f
            if (dx * dx + dy * dy < r * r) res.add(b.id)
        }
        return res
    }

    private fun hitJoystick(x: Float, y: Float): ButtonDef? {
        for (b in buttons) {
            if (b.type != 1) continue
            val dx = x - px(b)
            val dy = y - py(b)
            val r = pr(b)
            if (dx * dx + dy * dy < r * r) return b
        }
        return null
    }

    private fun updateJoystick(x: Float, y: Float) {
        if (joyMaxR <= 0f) return
        val dx = x - joyCx
        val dy = y - joyCy
        val len = sqrt(dx * dx + dy * dy)
        val nx: Float
        val ny: Float
        if (len > joyMaxR && len > 0.001f) {
            nx = dx * joyMaxR / len
            ny = dy * joyMaxR / len
        } else {
            nx = dx
            ny = dy
        }
        joyDx = nx
        joyDy = ny
        val ax = ((nx / joyMaxR) * 32767f).toInt().coerceIn(-32768, 32767)
        val ay = ((ny / joyMaxR) * 32767f).toInt().coerceIn(-32768, 32767)
        EmulatorBridge.nativeAnalog(ax, ay)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                val pid = event.getPointerId(i)
                val x = event.getX(i)
                val y = event.getY(i)
                val joy = hitJoystick(x, y)
                if (joy != null && joyPid < 0) {
                    joyPid = pid
                    joyRef = joy
                    joyMaxR = pr(joy)
                    joyCx = px(joy)
                    joyCy = py(joy)
                    updateJoystick(x, y)
                } else {
                    val hits = hitAll(x, y)
                    val prev = held[pid] ?: emptyList()
                    for (old in prev) if (old !in hits) EmulatorBridge.nativeKey(0, old, false)
                    for (nw in hits) if (nw !in prev) EmulatorBridge.nativeKey(0, nw, true)
                    held[pid] = hits
                }
                parent?.requestDisallowInterceptTouchEvent(true)
                invalidate()
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) {
                    val pid = event.getPointerId(i)
                    if (pid == joyPid) {
                        updateJoystick(event.getX(i), event.getY(i))
                    } else {
                        val hits = hitAll(event.getX(i), event.getY(i))
                        val prev = held[pid] ?: emptyList()
                        for (old in prev) if (old !in hits) EmulatorBridge.nativeKey(0, old, false)
                        for (nw in hits) if (nw !in prev) EmulatorBridge.nativeKey(0, nw, true)
                        held[pid] = hits
                    }
                }
                invalidate()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val pid = event.getPointerId(event.actionIndex)
                if (pid == joyPid) {
                    joyPid = -1
                    joyRef = null
                    joyDx = 0f
                    joyDy = 0f
                    EmulatorBridge.nativeAnalog(0, 0)
                } else {
                    held.remove(pid)?.forEach { EmulatorBridge.nativeKey(0, it, false) }
                }
                invalidate()
            }
            MotionEvent.ACTION_CANCEL -> {
                held.values.flatten().forEach { EmulatorBridge.nativeKey(0, it, false) }
                held.clear()
                joyPid = -1
                joyRef = null
                joyDx = 0f
                joyDy = 0f
                EmulatorBridge.nativeAnalog(0, 0)
                invalidate()
            }
        }
        return true
    }

    private fun isPressed(id: Int): Boolean {
        for (list in held.values) if (id in list) return true
        return false
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        for (b in buttons) {
            val cx = px(b)
            val cy = py(b)
            val r = pr(b)
            val a = b.alpha.coerceIn(0f, 1f)

            if (b.type == 1) {
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = 4f
                paint.color = Color.argb((170 * a).toInt(), 0x4A, 0x8C, 0xFF)
                canvas.drawCircle(cx, cy, r, paint)

                val isActive = joyRef == b
                val kx = cx + (if (isActive) joyDx else 0f)
                val ky = cy + (if (isActive) joyDy else 0f)

                paint.style = Paint.Style.FILL
                paint.color = if (isActive) Color.argb((220 * a).toInt(), 0xFF, 0xD2, 0x4A)
                              else Color.argb((100 * a).toInt(), 0, 0, 0)
                canvas.drawCircle(kx, ky, r * 0.4f, paint)

                paint.style = Paint.Style.FILL
                paint.color = Color.argb((255 * a).toInt(), 0xFF, 0xFF, 0xFF)
                paint.textSize = r * 0.35f
                val fm = paint.fontMetrics
                canvas.drawText(b.label, cx, cy - (fm.ascent + fm.descent) / 2f, paint)
            } else {
                val pressed = isPressed(b.id)
                paint.style = Paint.Style.FILL
                paint.color = if (pressed) Color.argb((255 * a).toInt(), 0xFF, 0xD2, 0x4A)
                              else Color.argb((100 * a).toInt(), 0, 0, 0)
                canvas.drawCircle(cx, cy, r, paint)

                paint.style = Paint.Style.STROKE
                paint.strokeWidth = 4f
                paint.color = if (pressed) Color.argb((255 * a).toInt(), 0xFF, 0xD2, 0x4A)
                              else Color.argb((170 * a).toInt(), 0x4A, 0x8C, 0xFF)
                canvas.drawCircle(cx, cy, r, paint)

                paint.style = Paint.Style.FILL
                paint.color = Color.argb((255 * a).toInt(), 0xFF, 0xFF, 0xFF)
                paint.textSize = r * 0.9f
                val fm = paint.fontMetrics
                canvas.drawText(b.label, cx, cy - (fm.ascent + fm.descent) / 2f, paint)
            }
        }

        if (cfg.showFps) {
            val text = "FPS " + fpsValue.toInt()
            canvas.drawText(text, 100f, 50f, fpsPaint)
        }
    }
}

package com.pssp

import android.content.Context
import android.util.AttributeSet
import android.util.DisplayMetrics
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView

class EmulatorView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : SurfaceView(context, attrs), SurfaceHolder.Callback {

    @Volatile var isSurfaceValid: Boolean = false
        private set

    private var readyFired = false
    private var pendingCallback: (() -> Unit)? = null
    var onSurfaceRecreated: (() -> Unit)? = null

    var onSurfaceReady: (() -> Unit)?
        get() = pendingCallback
        set(value) {
            pendingCallback = value
            if (readyFired && value != null) value.invoke()
        }

    init {
        holder.addCallback(this)
        setZOrderMediaOverlay(false)
        setWillNotDraw(true)
        holder.setFormat(android.graphics.PixelFormat.RGBA_8888)
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        super.onMeasure(widthMeasureSpec, heightMeasureSpec)
    }

    override fun surfaceCreated(h: SurfaceHolder) {
        Log.i("UltramanFE0", "surfaceCreated ENTER size=${width}x${height} readyFired=$readyFired")
        isSurfaceValid = true
        EmulatorBridge.nativeSetSurface(h.surface)
        if (!readyFired) {
            readyFired = true
            pendingCallback?.invoke()
        } else {
            onSurfaceRecreated?.invoke()
        }
    }

    override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, ht: Int) {
        Log.i("UltramanFE0", "surfaceChanged ${w}x${ht}")
    }

    override fun surfaceDestroyed(h: SurfaceHolder) {
        Log.i("UltramanFE0", "surfaceDestroyed ENTER")
        isSurfaceValid = false
        EmulatorBridge.nativeSetSurface(null)
    }
}

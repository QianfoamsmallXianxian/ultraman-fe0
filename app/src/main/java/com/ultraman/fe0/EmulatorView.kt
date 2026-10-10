package com.ultraman.fe0

import android.content.Context
import android.util.AttributeSet
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView

class EmulatorView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : SurfaceView(context, attrs), SurfaceHolder.Callback {
    init {
        holder.addCallback(this)
        setZOrderMediaOverlay(false)
    }
    override fun surfaceCreated(h: SurfaceHolder) {
        Log.i("UltramanFE0", "surfaceCreated")
        EmulatorBridge.nativeSetSurface(h.surface)
    }
    override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, ht: Int) {
        Log.i("UltramanFE0", "surfaceChanged: ${w}x${ht}")
        EmulatorBridge.nativeSetSurface(h.surface)
    }
    override fun surfaceDestroyed(h: SurfaceHolder) {
        Log.i("UltramanFE0", "surfaceDestroyed")
        EmulatorBridge.nativeSetSurface(null)
    }
}

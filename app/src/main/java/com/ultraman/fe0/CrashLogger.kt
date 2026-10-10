package com.ultraman.fe0

import android.content.Context
import android.os.Build
import android.util.Log
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object CrashLogger {

    private const val TAG = "UltramanFE0"

    fun install(context: Context) {
        val app = context.applicationContext
        val prev = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            try {
                save(app, t, e)
            } catch (_: Throwable) {}
            prev?.uncaughtException(t, e)
        }
    }

    private fun save(context: Context, thread: Thread, e: Throwable) {
        val time = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val sw = StringWriter()
        val pw = PrintWriter(sw)
        pw.println("===== CRASH =====")
        pw.println("time   : $time")
        pw.println("thread : ${thread.name}")
        pw.println("model  : ${Build.MANUFACTURER} ${Build.MODEL}")
        pw.println("android: ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})")
        pw.println("abi    : ${Build.SUPPORTED_ABIS.joinToString()}")
        pw.println("app    : ${context.packageName}")
        pw.println("----- stack -----")
        e.printStackTrace(pw)
        pw.flush()
        val text = sw.toString()

        Log.e(TAG, text)

        val dir = File(context.getExternalFilesDir(null), "logs")
        try {
            if (!dir.exists()) dir.mkdirs()
            val f = File(dir, "crash_$time.log")
            f.writeText(text)
            Log.i(TAG, "crash saved: ${f.absolutePath}")
        } catch (t: Throwable) {
            Log.w(TAG, "save failed", t)
        }
    }
}

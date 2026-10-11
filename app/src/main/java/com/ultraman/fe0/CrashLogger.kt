package com.ultraman.fe0

import android.content.Context
import android.util.Log
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object CrashLogger {

    fun install(ctx: Context) {
        val prev = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            try {
                writeCrash(ctx, t, e)
            } catch (_: Throwable) {}
            prev?.uncaughtException(t, e)
        }
    }

    private fun writeCrash(ctx: Context, thread: Thread, e: Throwable) {
        val dir = File(ctx.getExternalFilesDir(null), "logs")
        dir.mkdirs()
        val f = File(dir, "crash_java.log")
        val sw = StringWriter()
        val pw = PrintWriter(sw)
        pw.println("=== JAVA CRASH ===")
        pw.println("time: " + SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date()))
        pw.println("thread: " + thread.name)
        pw.println("---- stack ----")
        e.printStackTrace(pw)
        pw.flush()
        f.writeText(sw.toString())
        Log.e("UltramanFE0", sw.toString())
    }
}

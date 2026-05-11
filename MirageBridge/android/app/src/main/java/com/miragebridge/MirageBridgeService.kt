package com.miragebridge

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder

class MirageBridgeService : Service() {
    companion object {
        private const val CHANNEL_ID = "miragebridge"
        private const val NOTIF_ID = 1101
    }

    external fun nativeStart(dataPath: String, nativeGvrContext: Long)
    external fun nativeStop()

    override fun onCreate() {
        super.onCreate()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val nm = getSystemService(NotificationManager::class.java)
            val channel = NotificationChannel(CHANNEL_ID, "MirageBridge", NotificationManager.IMPORTANCE_LOW)
            nm.createNotificationChannel(channel)
            val notif = Notification.Builder(this, CHANNEL_ID)
                .setContentTitle("MirageBridge")
                .setContentText("XR bridge active")
                .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
                .build()
            startForeground(NOTIF_ID, notif)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val dataPath = applicationContext.filesDir.absolutePath
        val nativeCtx = intent?.getLongExtra("native_gvr_context", 0L) ?: 0L
        nativeStart(dataPath, nativeCtx)
        return START_STICKY
    }

    override fun onDestroy() {
        nativeStop()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    init {
        System.loadLibrary("miragebridge")
    }
}

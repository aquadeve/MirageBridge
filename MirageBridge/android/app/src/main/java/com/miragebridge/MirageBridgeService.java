package com.miragebridge;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

public final class MirageBridgeService extends Service {
    private static final String CHANNEL_ID = "miragebridge";
    private static final int NOTIF_ID = 1101;

    private PowerManager.WakeLock wakeLock;

    private native void nativeStart(String dataPath, long nativeGvrContext);
    private native void nativeStop();

    @Override
    public void onCreate() {
        super.onCreate();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationManager nm = getSystemService(NotificationManager.class);
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "MirageBridge",
                    NotificationManager.IMPORTANCE_LOW);
            nm.createNotificationChannel(channel);
            Notification notif = new Notification.Builder(this, CHANNEL_ID)
                    .setContentTitle("MirageBridge")
                    .setContentText("XR bridge active")
                    .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
                    .build();
            startForeground(NOTIF_ID, notif);
        }

        PowerManager pm = getSystemService(PowerManager.class);
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "MirageBridge:Tracking");
        wakeLock.setReferenceCounted(false);
        wakeLock.acquire();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String dataPath = getApplicationContext().getFilesDir().getAbsolutePath();
        long nativeCtx = intent != null ? intent.getLongExtra("native_gvr_context", 0L) : 0L;
        nativeStart(dataPath, nativeCtx);
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        nativeStop();
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        wakeLock = null;
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    static {
        System.loadLibrary("miragebridge");
    }
}

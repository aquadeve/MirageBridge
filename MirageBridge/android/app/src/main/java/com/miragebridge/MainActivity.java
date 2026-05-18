package com.miragebridge;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.util.Log;
import android.view.View;

import com.google.vr.ndk.base.AndroidCompat;
import com.google.vr.ndk.base.GvrApi;
import com.google.vr.ndk.base.GvrLayout;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public final class MainActivity extends Activity {
    private static final String TAG = "MirageBridge";
    private static final String ENABLED_VR_LISTENERS = "enabled_vr_listeners";

    private GvrLayout gvrLayout;
    private GLSurfaceView surfaceView;
    private Intent bridgeIntent;

    private native void nativeOnSurfaceCreated();
    private native void nativeOnSurfaceChanged(int width, int height);
    private native void nativeDrawFrame();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setImmersiveSticky();

        // Mirage Solo VRCore builds can reject newer dynamically loaded GVR clients.
        GvrApi.setDynamicLibraryLoadingEnabled(false);
        gvrLayout = new GvrLayout(this);
        surfaceView = new GLSurfaceView(this);
        surfaceView.setEGLContextClientVersion(3);
        surfaceView.setEGLConfigChooser(8, 8, 8, 8, 24, 8);
        surfaceView.setPreserveEGLContextOnPause(true);
        surfaceView.setRenderer(new GLSurfaceView.Renderer() {
            @Override
            public void onSurfaceCreated(GL10 gl, EGLConfig config) {
                nativeOnSurfaceCreated();
            }

            @Override
            public void onSurfaceChanged(GL10 gl, int width, int height) {
                nativeOnSurfaceChanged(width, height);
            }

            @Override
            public void onDrawFrame(GL10 gl) {
                nativeDrawFrame();
            }
        });
        gvrLayout.setPresentationView(surfaceView);
        setContentView(gvrLayout);

        if (gvrLayout.setAsyncReprojectionEnabled(true)) {
            AndroidCompat.setSustainedPerformanceMode(this, true);
        }
        setVrModeEnabledIfAvailable(true);

        long nativeCtx = gvrLayout.getGvrApi().getNativeGvrContext();
        bridgeIntent = new Intent(this, MirageBridgeService.class);
        bridgeIntent.putExtra("native_gvr_context", nativeCtx);
        startForegroundService(bridgeIntent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        gvrLayout.onResume();
        surfaceView.onResume();
    }

    @Override
    protected void onPause() {
        surfaceView.onPause();
        gvrLayout.onPause();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        stopService(bridgeIntent);
        setVrModeEnabledIfAvailable(false);
        gvrLayout.shutdown();
        super.onDestroy();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            setImmersiveSticky();
        }
    }

    private void setImmersiveSticky() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    private boolean setVrModeEnabledIfAvailable(boolean enabled) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
            return false;
        }
        if (!getPackageManager().hasSystemFeature(PackageManager.FEATURE_VR_MODE)) {
            return false;
        }

        ComponentName vrCoreListener = ComponentName.unflattenFromString(
                getString(com.google.vr.cardboard.R.string.gvr_vr_mode_component));
        if (vrCoreListener == null || !isVrCoreListenerPresent(vrCoreListener)) {
            Log.w(TAG, "Google VR listener is unavailable; continuing without Android VR mode");
            return false;
        }
        if (enabled && !isVrCoreListenerEnabled(vrCoreListener)) {
            Log.w(TAG, "Google VR listener is not enabled; continuing without Android VR mode");
            return false;
        }

        try {
            setVrModeEnabled(enabled, vrCoreListener);
            return true;
        } catch (PackageManager.NameNotFoundException | UnsupportedOperationException e) {
            Log.w(TAG, "Unable to change Android VR mode; continuing with embedded GVR", e);
            return false;
        }
    }

    private boolean isVrCoreListenerPresent(ComponentName vrCoreListener) {
        try {
            getPackageManager().getServiceInfo(vrCoreListener, 0);
            return true;
        } catch (PackageManager.NameNotFoundException e) {
            return false;
        }
    }

    private boolean isVrCoreListenerEnabled(ComponentName vrCoreListener) {
        String listeners = Settings.Secure.getString(getContentResolver(), ENABLED_VR_LISTENERS);
        return listeners != null && listeners.contains(vrCoreListener.flattenToString());
    }

    static {
        System.loadLibrary("miragebridge");
    }
}

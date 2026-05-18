package com.miragebridge;

import android.app.Activity;
import android.content.Intent;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.View;

import com.google.vr.ndk.base.AndroidCompat;
import com.google.vr.ndk.base.GvrApi;
import com.google.vr.ndk.base.GvrLayout;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public final class MainActivity extends Activity {
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
        // Keep GVR local-only on Daydream OS 8.0. This prevents VrCoreSdkClient
        // from calling prepareVr(), which shows the newer-Daydream-required dialog
        // on Mirage Solo VRCore 1.23.
        gvrLayout.setStereoModeEnabled(false);
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
        AndroidCompat.setSustainedPerformanceMode(this, true);

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

    static {
        System.loadLibrary("miragebridge");
    }
}

package com.miragebridge

import android.app.Activity
import android.content.Intent
import android.opengl.GLSurfaceView
import android.os.Bundle
import com.google.vr.ndk.base.AndroidCompat
import com.google.vr.ndk.base.GvrLayout

class MainActivity : Activity() {
    private lateinit var gvrLayout: GvrLayout
    private lateinit var surfaceView: GLSurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        gvrLayout = GvrLayout(this)
        surfaceView = GLSurfaceView(this)
        surfaceView.setEGLContextClientVersion(3)
        surfaceView.setEGLConfigChooser(8, 8, 8, 8, 24, 8)
        surfaceView.setPreserveEGLContextOnPause(true)
        gvrLayout.setPresentationView(surfaceView)
        setContentView(gvrLayout)

        gvrLayout.setAsyncReprojectionEnabled(true)
        AndroidCompat.setVrModeEnabled(this, true)
        AndroidCompat.setSustainedPerformanceMode(this, true)

        val nativeCtx = gvrLayout.gvrApi.nativeGvrContext
        val intent = Intent(this, MirageBridgeService::class.java)
        intent.putExtra("native_gvr_context", nativeCtx)
        startForegroundService(intent)
    }

    override fun onResume() {
        super.onResume()
        gvrLayout.onResume()
        surfaceView.onResume()
    }

    override fun onPause() {
        surfaceView.onPause()
        gvrLayout.onPause()
        super.onPause()
    }

    override fun onDestroy() {
        gvrLayout.shutdown()
        super.onDestroy()
    }
}

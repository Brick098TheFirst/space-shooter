package com.brick.spaceshooter

import android.content.pm.ActivityInfo
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.WindowManager
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.epicgames.mobile.eossdk.EOSSDK
import java.io.File
import kotlin.math.abs

class MainActivity : AppCompatActivity() {

    private lateinit var gameView: GameView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setup90HzDisplay()
        hideSystemBars()
        initializeEpicOnlineServices()
        gameView = GameView(this)
        setContentView(gameView)
    }

    /**
     * The EOS Android Java layer must be initialized before loading our game
     * library, because libspacegame links libEOSSDK. Credentials are generated
     * from ignored eos.properties values (or CI environment secrets).
     */
    private fun initializeEpicOnlineServices() {
        try {
            System.loadLibrary("EOSSDK")
            EOSSDK.init(this)

            val eosCache = File(filesDir, "eos-cache").apply { mkdirs() }
            val external = getExternalFilesDir(null)?.absolutePath.orEmpty()
            NativeGame.nativeEosInitialize(
                eosCache.absolutePath,
                external,
                BuildConfig.EOS_PRODUCT_ID,
                BuildConfig.EOS_SANDBOX_ID,
                BuildConfig.EOS_DEPLOYMENT_ID,
                BuildConfig.EOS_CLIENT_ID,
                BuildConfig.EOS_CLIENT_SECRET,
                "Space Pilot"
            )
        } catch (error: Throwable) {
            // Keep the offline game playable even when a local EOS setup is
            // absent or malformed. The online chip reports configuration state.
            Log.e("SpaceUnlimitedEOS", "Could not initialize EOS", error)
        }
    }

    private fun setup90HzDisplay() {
        val targetHz = 90f
        @Suppress("DEPRECATION")
        window.attributes.preferredRefreshRate = targetHz

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val modes = display?.supportedModes ?: emptyArray()
            val mode90 = modes.find { abs(it.refreshRate - targetHz) < 1f }
                ?: modes.filter { it.refreshRate >= 89f }.minByOrNull { it.refreshRate }
            if (mode90 != null) {
                val params = window.attributes
                params.preferredDisplayModeId = mode90.modeId
                window.attributes = params
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            @Suppress("DEPRECATION")
            val modes = windowManager.defaultDisplay?.supportedModes ?: emptyArray()
            val mode90 = modes.find { abs(it.refreshRate - targetHz) < 1f }
                ?: modes.filter { it.refreshRate >= 89f }.minByOrNull { it.refreshRate }
            if (mode90 != null) {
                val params = window.attributes
                params.preferredDisplayModeId = mode90.modeId
                window.attributes = params
            }
        }
    }

    private fun hideSystemBars() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        // Draw edge-to-edge, including under the camera cutout, so the game
        // truly fills the screen with no letterbox bars.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        val controller = WindowCompat.getInsetsController(window, window.decorView)
        controller.hide(WindowInsetsCompat.Type.systemBars())
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
        setup90HzDisplay()
        try { NativeGame.nativeEosSetForeground(true) } catch (_: Throwable) {}
        if (::gameView.isInitialized) gameView.resume()
    }

    override fun onPause() {
        try { NativeGame.nativeEosSetForeground(false) } catch (_: Throwable) {}
        if (::gameView.isInitialized) gameView.pause()
        super.onPause()
    }

    override fun onStop() {
        if (::gameView.isInitialized) gameView.pause()
        super.onStop()
    }

    override fun onDestroy() {
        if (::gameView.isInitialized) gameView.release()
        try { NativeGame.nativeEosShutdown() } catch (_: Throwable) {}
        super.onDestroy()
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (::gameView.isInitialized && gameView.handleKeyEvent(event)) return true
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (::gameView.isInitialized && gameView.handleMotionEvent(event)) return true
        return super.dispatchGenericMotionEvent(event)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemBars()
            setup90HzDisplay()
        }
    }
}

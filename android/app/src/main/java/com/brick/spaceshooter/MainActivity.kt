package com.brick.spaceshooter

import android.content.pm.ActivityInfo
import android.os.Build
import android.os.Bundle
import android.view.WindowManager
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import kotlin.math.abs

class MainActivity : AppCompatActivity() {

    private lateinit var gameView: GameView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setup90HzDisplay()
        hideSystemBars()
        gameView = GameView(this)
        setContentView(gameView)
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
        val controller = WindowCompat.getInsetsController(window, window.decorView)
        controller.hide(WindowInsetsCompat.Type.systemBars())
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
        setup90HzDisplay()
        if (::gameView.isInitialized) gameView.resume()
    }

    override fun onPause() {
        if (::gameView.isInitialized) gameView.pause()
        super.onPause()
    }

    override fun onDestroy() {
        if (::gameView.isInitialized) gameView.release()
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemBars()
            setup90HzDisplay()
        }
    }
}

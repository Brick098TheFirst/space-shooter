package com.brick.spaceshooter

import android.content.pm.ActivityInfo
import android.os.Build
import android.os.Bundle
import android.view.Window
import android.view.WindowManager
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

class MainActivity : AppCompatActivity() {

    private lateinit var gameView: GameView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Force sensor landscape orientation
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        // Setup true immersive fullscreen mode with zero top or bottom system bars
        hideSystemBars()

        // Configure High Refresh Rate (90Hz / 120Hz)
        requestHighRefreshRate()

        gameView = GameView(this)
        setContentView(gameView)
    }

    private fun hideSystemBars() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val controller = WindowCompat.getInsetsController(window, window.decorView)
        controller.hide(WindowInsetsCompat.Type.systemBars())
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }

    private fun requestHighRefreshRate() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                val params = window.attributes
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    val display = display
                    display?.supportedModes?.maxByOrNull { it.refreshRate }?.let { mode ->
                        params.preferredDisplayModeId = mode.modeId
                    }
                }
                window.attributes = params
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                window.setFrameRate(120f, Window.FRAME_RATE_COMPATIBILITY_DEFAULT)
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
        if (::gameView.isInitialized) {
            gameView.resume()
        }
    }

    override fun onPause() {
        super.onPause()
        if (::gameView.isInitialized) {
            gameView.pause()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (::gameView.isInitialized) {
            gameView.release()
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemBars()
        }
    }
}

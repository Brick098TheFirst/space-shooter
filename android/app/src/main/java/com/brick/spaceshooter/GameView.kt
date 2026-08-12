package com.brick.spaceshooter

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.os.Build
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView

class GameView(context: Context) : SurfaceView(context), SurfaceHolder.Callback {

    val saveManager = SaveManager(context)
    val audioManager = AudioManager(context, saveManager)
    val spriteRenderer = SpriteRenderer()
    val touchControls = TouchControls(saveManager)
    val gameEngine: GameEngine

    private var thread: GameThread? = null
    private var isRunning = false

    init {
        holder.addCallback(this)
        isFocusable = true
        isFocusableInTouchMode = true

        GfxData.init(context)
        gameEngine = GameEngine(saveManager, audioManager, spriteRenderer, touchControls)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                holder.surface.setFrameRate(120f, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }
        resume()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        // Handle screen resizing
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        pause()
    }

    fun resume() {
        if (!isRunning) {
            isRunning = true
            thread = GameThread(holder, this).apply {
                start()
            }
            audioManager.resumeMusic()
        }
    }

    fun pause() {
        if (isRunning) {
            isRunning = false
            try {
                thread?.join()
            } catch (e: InterruptedException) {
                e.printStackTrace()
            }
            thread = null
            audioManager.pauseMusic()
            saveManager.save()
        }
    }

    fun updateAndDraw(canvas: Canvas?) {
        if (canvas == null) return

        try {
            val scaleX = canvas.width / VIRTUAL_WIDTH
            val scaleY = canvas.height / VIRTUAL_HEIGHT

            canvas.save()
            canvas.scale(scaleX, scaleY)
            canvas.drawColor(Color.rgb(4, 6, 12)) // Default dark GBA space background
            gameEngine.draw(canvas)
            canvas.restore()

            touchControls.clearFrameTaps()
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val scaleX = width / VIRTUAL_WIDTH
        val scaleY = height / VIRTUAL_HEIGHT

        // First check menu button taps
        if (gameEngine.handleMenuTouch(event, scaleX, scaleY)) {
            return true
        }

        // Then handle in-game virtual joystick & buttons
        if (gameEngine.currentScreen == Screen.PLAYING) {
            return touchControls.handleTouchEvent(event, scaleX, scaleY)
        }

        return true
    }

    fun release() {
        pause()
        gameEngine.release()
    }

    inner class GameThread(
        private val surfaceHolder: SurfaceHolder,
        private val gameView: GameView
    ) : Thread() {

        private var lastTime = System.nanoTime()
        private var accumulator = 0f
        private var fpsTimer = System.currentTimeMillis()
        private var frames = 0

        override fun run() {
            while (isRunning) {
                val now = System.nanoTime()
                val dtMs = ((now - lastTime) / 1_000_000.0).toFloat().coerceAtMost(100f)
                lastTime = now

                accumulator += dtMs
                while (accumulator >= FIXED_TIMESTEP) {
                    gameView.gameEngine.updatePhysics()
                    accumulator -= FIXED_TIMESTEP
                }

                // Measure real-time FPS
                frames++
                val curMs = System.currentTimeMillis()
                if (curMs - fpsTimer >= 1000) {
                    gameView.gameEngine.displayFps = frames
                    frames = 0
                    fpsTimer = curMs
                }

                var canvas: Canvas? = null
                try {
                    canvas = surfaceHolder.lockCanvas()
                    if (canvas != null) {
                        synchronized(surfaceHolder) {
                            gameView.updateAndDraw(canvas)
                        }
                    }
                } catch (e: Exception) {
                    e.printStackTrace()
                } finally {
                    if (canvas != null) {
                        try {
                            surfaceHolder.unlockCanvasAndPost(canvas)
                        } catch (e: Exception) {
                            e.printStackTrace()
                        }
                    }
                }
            }
        }
    }
}

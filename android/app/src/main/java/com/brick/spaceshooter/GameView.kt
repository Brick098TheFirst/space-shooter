package com.brick.spaceshooter

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.view.Choreographer
import android.view.MotionEvent
import android.view.View
import java.io.File
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

class GameView(context: Context) : View(context), Choreographer.FrameCallback {

    private val pixels = IntArray(NativeGame.SCREEN_W * NativeGame.SCREEN_H)
    private val bitmap = Bitmap.createBitmap(NativeGame.SCREEN_W, NativeGame.SCREEN_H, Bitmap.Config.ARGB_8888)
    private val blitPaint = Paint().apply {
        isFilterBitmap = false
        isAntiAlias = false
        isDither = false
    }
    private val dest = Rect()
    private val choreographer = Choreographer.getInstance()
    private var running = false
    private var keys = 0
    private var pulseKeys = 0
    private var uiScreen = NativeGame.SCREEN_MAIN_MENU
    private val saveDir = File(context.filesDir, "saves").apply { mkdirs() }
    private val saveFile = File(saveDir, "save.sav")
    private var persistCounter = 0

    private val audioBuf = ShortArray(NativeGame.AUDIO_SAMPLES_PER_FRAME)
    private val audioTrack: AudioTrack

    private var lastFrameTimeNanos = 0L
    private var timeAccumulatorNanos = 0L
    private val targetFrameNanos = 1_000_000_000L / NativeGame.TARGET_FPS.toLong()

    private val overlay = Paint(Paint.ANTI_ALIAS_FLAG)
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        color = 0xE8FFFFFF.toInt()
    }

    private var stickPointer = -1
    private var firePointer = -1
    private var dashPointer = -1
    private var stickActive = false
    private var stickBaseX = 0f
    private var stickBaseY = 0f
    private var stickKnobX = 0f
    private var stickKnobY = 0f
    private var stickNx = 0f
    private var stickNy = 0f
    private var menuDownX = 0f
    private var menuDownY = 0f
    private var menuScrollAccumY = 0f
    private var menuScrollAccumX = 0f

    init {
        migrateLegacySave()
        NativeGame.nativeInit(saveDir.absolutePath)

        val minBuf = AudioTrack.getMinBufferSize(
            NativeGame.AUDIO_SAMPLE_RATE,
            AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT
        )
        audioTrack = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setSampleRate(NativeGame.AUDIO_SAMPLE_RATE)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                    .build()
            )
            .setBufferSizeInBytes(minBuf.coerceAtLeast(NativeGame.AUDIO_SAMPLES_PER_FRAME * 8))
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()
        audioTrack.play()
    }

    fun resume() {
        running = true
        lastFrameTimeNanos = 0L
        timeAccumulatorNanos = 0L
        choreographer.postFrameCallback(this)
        try { audioTrack.play() } catch (_: Exception) {}
    }

    fun pause() {
        running = false
        choreographer.removeFrameCallback(this)
        persistSave()
        try { audioTrack.pause() } catch (_: Exception) {}
    }

    fun release() {
        pause()
        audioTrack.release()
    }

    private fun migrateLegacySave() {
        if (saveFile.exists() && saveFile.length() > 0L) return
        val candidates = listOf(
            File(context.filesDir, "space_unlimited.sav"),
            File(context.filesDir, "save.sav"),
            File(context.getExternalFilesDir(null), "save.sav"),
            File(context.getExternalFilesDir(null), "saves/save.sav")
        )
        for (src in candidates) {
            if (src.exists() && src.isFile && src.length() > 0L) {
                try { src.copyTo(saveFile, overwrite = false) } catch (_: Exception) {}
                break
            }
        }
    }

    private fun persistSave() {
        try {
            NativeGame.nativeFlushSave()
            saveDir.mkdirs()
            saveFile.writeBytes(NativeGame.nativeGetSave())
        } catch (_: Exception) {}
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        updateDest(w, h)
        resetStickToHome()
    }

    private fun updateDest(vw: Int = width, vh: Int = height) {
        if (vw <= 0 || vh <= 0) return
        val scale = min(vw / NativeGame.SCREEN_W.toFloat(), vh / NativeGame.SCREEN_H.toFloat())
        val dw = max(1, (NativeGame.SCREEN_W * scale).toInt())
        val dh = max(1, (NativeGame.SCREEN_H * scale).toInt())
        val left = (vw - dw) / 2
        val top = (vh - dh) / 2
        dest.set(left, top, left + dw, top + dh)
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!running) return

        val held = if (uiScreen == NativeGame.SCREEN_PLAYING) gameplayKeys() else 0
        keys = held or pulseKeys
        pulseKeys = 0
        NativeGame.nativeSetKeys(keys)

        if (lastFrameTimeNanos == 0L) {
            lastFrameTimeNanos = frameTimeNanos
            NativeGame.nativeTick()
            val n = NativeGame.nativeMixAudio(audioBuf)
            if (n > 0) audioTrack.write(audioBuf, 0, n)
        } else {
            val elapsed = frameTimeNanos - lastFrameTimeNanos
            lastFrameTimeNanos = frameTimeNanos
            val clampedElapsed = elapsed.coerceIn(0L, targetFrameNanos * 4)
            timeAccumulatorNanos += clampedElapsed

            var ticks = 0
            while (timeAccumulatorNanos >= targetFrameNanos && ticks < 4) {
                NativeGame.nativeSetKeys(keys)
                NativeGame.nativeTick()
                val n = NativeGame.nativeMixAudio(audioBuf)
                if (n > 0) audioTrack.write(audioBuf, 0, n)
                timeAccumulatorNanos -= targetFrameNanos
                ticks++
                keys = if (NativeGame.nativeGetScreen() == NativeGame.SCREEN_PLAYING) gameplayKeys() else 0
            }
        }

        val nextScreen = NativeGame.nativeGetScreen()
        if (nextScreen != uiScreen) {
            if (nextScreen != NativeGame.SCREEN_PLAYING) resetStickToHome()
            persistSave()
            uiScreen = nextScreen
        }
        persistCounter++
        if (persistCounter >= NativeGame.TARGET_FPS * 2) {
            persistCounter = 0
            persistSave()
        }

        NativeGame.nativePresent(pixels)
        bitmap.setPixels(pixels, 0, NativeGame.SCREEN_W, 0, 0, NativeGame.SCREEN_W, NativeGame.SCREEN_H)
        invalidate()
        choreographer.postFrameCallback(this)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawColor(0xFF050810.toInt())
        updateDest()
        canvas.drawBitmap(bitmap, null, dest, blitPaint)
        drawOverlay(canvas)
    }

    private val density get() = resources.displayMetrics.density
    private fun dp(v: Float) = v * density

    private fun stickHomeX() = dest.left + dest.width() * 0.18f
    private fun stickHomeY() = dest.bottom - dest.height() * 0.22f
    private fun stickRadius() = min(dest.width(), dest.height()) * 0.16f
    private fun fireCx() = dest.right - dest.width() * 0.14f
    private fun fireCy() = dest.bottom - dest.height() * 0.24f
    private fun fireR() = min(dest.width(), dest.height()) * 0.105f
    private fun dashCx() = dest.right - dest.width() * 0.30f
    private fun dashCy() = dest.bottom - dest.height() * 0.14f
    private fun dashR() = min(dest.width(), dest.height()) * 0.078f

    private fun pauseRect(): RectF {
        val w = dp(64f)
        val h = dp(28f)
        val cx = dest.exactCenterX()
        return RectF(cx - w / 2f, dest.top + dp(8f), cx + w / 2f, dest.top + dp(8f) + h)
    }

    private fun backRect(): RectF {
        val w = dp(72f)
        val h = dp(32f)
        return RectF(dp(12f), height - h - dp(12f), dp(12f) + w, height - dp(12f))
    }

    private fun resetStickToHome() {
        stickPointer = -1
        firePointer = -1
        dashPointer = -1
        stickActive = false
        stickNx = 0f
        stickNy = 0f
        stickBaseX = stickHomeX()
        stickBaseY = stickHomeY()
        stickKnobX = stickBaseX
        stickKnobY = stickBaseY
    }

    private fun gameplayKeys(): Int {
        if (uiScreen != NativeGame.SCREEN_PLAYING) return 0
        var next = 0
        val dead = 0.28f
        if (stickNx < -dead) next = next or NativeGame.KEY_LEFT
        if (stickNx > dead) next = next or NativeGame.KEY_RIGHT
        if (stickNy < -dead) next = next or NativeGame.KEY_UP
        if (stickNy > dead) next = next or NativeGame.KEY_DOWN
        if (firePointer != -1) next = next or NativeGame.KEY_A
        if (dashPointer != -1) next = next or NativeGame.KEY_B
        return next
    }

    private fun drawOverlay(canvas: Canvas) {
        when (uiScreen) {
            NativeGame.SCREEN_PLAYING -> drawGameplayPad(canvas)
            NativeGame.SCREEN_HANGAR,
            NativeGame.SCREEN_SETTINGS,
            NativeGame.SCREEN_CONTROLS,
            NativeGame.SCREEN_CREDITS,
            NativeGame.SCREEN_MODE_SELECT -> drawBackChip(canvas)
        }
    }

    private fun drawGameplayPad(canvas: Canvas) {
        if (!stickActive) {
            stickBaseX = stickHomeX()
            stickBaseY = stickHomeY()
            stickKnobX = stickBaseX
            stickKnobY = stickBaseY
        }
        val r = stickRadius()
        overlay.style = Paint.Style.FILL
        overlay.color = 0x55202738
        canvas.drawCircle(stickBaseX, stickBaseY, r, overlay)
        overlay.style = Paint.Style.STROKE
        overlay.strokeWidth = dp(3f)
        overlay.color = 0x99E8F6FF.toInt()
        canvas.drawCircle(stickBaseX, stickBaseY, r, overlay)
        overlay.color = 0x5523D6FF
        canvas.drawCircle(stickBaseX, stickBaseY, r * 0.62f, overlay)
        overlay.style = Paint.Style.FILL
        overlay.color = if (stickActive) 0xDD23D6FF.toInt() else 0xAA23D6FF.toInt()
        canvas.drawCircle(stickKnobX, stickKnobY, r * 0.38f, overlay)
        overlay.color = 0x66FFFFFF
        canvas.drawCircle(stickKnobX - r * 0.08f, stickKnobY - r * 0.08f, r * 0.12f, overlay)

        drawRoundAction(canvas, fireCx(), fireCy(), fireR(), "FIRE", firePointer != -1, 0xFF23D6FF.toInt())
        drawRoundAction(canvas, dashCx(), dashCy(), dashR(), "DASH", dashPointer != -1, 0xFFFFC84A.toInt())

        val pr = pauseRect()
        overlay.style = Paint.Style.FILL
        overlay.color = 0x66282730
        canvas.drawRoundRect(pr, dp(14f), dp(14f), overlay)
        overlay.style = Paint.Style.STROKE
        overlay.strokeWidth = dp(1.5f)
        overlay.color = 0x99FFFFFF.toInt()
        canvas.drawRoundRect(pr, dp(14f), dp(14f), overlay)
        labelPaint.textSize = dp(12f)
        canvas.drawText("PAUSE", pr.centerX(), pr.centerY() + dp(4f), labelPaint)
    }

    private fun drawRoundAction(canvas: Canvas, cx: Float, cy: Float, r: Float, label: String, pressed: Boolean, accent: Int) {
        overlay.style = Paint.Style.FILL
        overlay.color = if (pressed) 0xAA23D6FF.toInt() else 0x66202738
        canvas.drawCircle(cx, cy, r, overlay)
        overlay.style = Paint.Style.STROKE
        overlay.strokeWidth = dp(3f)
        overlay.color = accent
        canvas.drawCircle(cx, cy, r, overlay)
        labelPaint.textSize = r * 0.38f
        canvas.drawText(label, cx, cy + r * 0.14f, labelPaint)
    }

    private fun drawBackChip(canvas: Canvas) {
        val r = backRect()
        overlay.style = Paint.Style.FILL
        overlay.color = 0xAA1A2438.toInt()
        canvas.drawRoundRect(r, dp(16f), dp(16f), overlay)
        overlay.style = Paint.Style.STROKE
        overlay.strokeWidth = dp(1.5f)
        overlay.color = 0xCC23D6FF.toInt()
        canvas.drawRoundRect(r, dp(16f), dp(16f), overlay)
        labelPaint.textSize = dp(13f)
        canvas.drawText("BACK", r.centerX(), r.centerY() + dp(4.5f), labelPaint)
    }

    private fun mapToGame(x: Float, y: Float): Pair<Int, Int>? {
        if (dest.width() <= 0 || dest.height() <= 0) return null
        if (!dest.contains(x.toInt(), y.toInt())) return null
        val gx = ((x - dest.left) * NativeGame.SCREEN_W / dest.width()).toInt()
        val gy = ((y - dest.top) * NativeGame.SCREEN_H / dest.height()).toInt()
        return gx.coerceIn(0, NativeGame.SCREEN_W - 1) to gy.coerceIn(0, NativeGame.SCREEN_H - 1)
    }

    private fun inCircle(x: Float, y: Float, cx: Float, cy: Float, r: Float) =
        hypot(x - cx, y - cy) <= r

    private fun updateStickFrom(x: Float, y: Float) {
        val r = stickRadius()
        var dx = x - stickBaseX
        var dy = y - stickBaseY
        val len = hypot(dx, dy)
        if (len > r && len > 0.001f) {
            dx = dx / len * r
            dy = dy / len * r
        }
        stickKnobX = stickBaseX + dx
        stickKnobY = stickBaseY + dy
        stickNx = if (r > 0f) dx / r else 0f
        stickNy = if (r > 0f) dy / r else 0f
    }

    private fun needsBackChip() = uiScreen == NativeGame.SCREEN_HANGAR ||
        uiScreen == NativeGame.SCREEN_SETTINGS ||
        uiScreen == NativeGame.SCREEN_CONTROLS ||
        uiScreen == NativeGame.SCREEN_CREDITS

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val action = event.actionMasked
        val index = event.actionIndex
        val id = event.getPointerId(index)
        val x = event.getX(index)
        val y = event.getY(index)

        if (uiScreen != NativeGame.SCREEN_PLAYING) {
            when (action) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    menuDownX = x
                    menuDownY = y
                    menuScrollAccumY = 0f
                    menuScrollAccumX = 0f
                }
                MotionEvent.ACTION_MOVE -> {
                    // Accumulate scroll distance for swipe-to-scroll in shop
                    val dy = y - menuDownY
                    val dx = x - menuDownX
                    menuScrollAccumY += dy
                    menuScrollAccumX += dx
                    menuDownY = y
                    menuDownX = x

                    // Trigger scroll every ~24dp of vertical swipe in hangar/upgrades
                    val threshold = dp(24f)
                    if (uiScreen == NativeGame.SCREEN_HANGAR || uiScreen == NativeGame.SCREEN_SETTINGS) {
                        if (menuScrollAccumY < -threshold) {
                            pulseKeys = pulseKeys or NativeGame.KEY_DOWN
                            menuScrollAccumY = 0f
                        } else if (menuScrollAccumY > threshold) {
                            pulseKeys = pulseKeys or NativeGame.KEY_UP
                            menuScrollAccumY = 0f
                        }
                    }
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                    val dx = x - menuDownX
                    val dy = y - menuDownY
                    if (needsBackChip() && backRect().contains(x, y)) {
                        NativeGame.nativeGoBack()
                    } else if ((uiScreen == NativeGame.SCREEN_HANGAR || uiScreen == NativeGame.SCREEN_SETTINGS) &&
                        kotlin.math.abs(menuScrollAccumX) > dp(36f) &&
                        kotlin.math.abs(menuScrollAccumX) > kotlin.math.abs(menuScrollAccumY) * 1.3f
                    ) {
                        // Horizontal swipe across the hangar tab strip / catalog.
                        pulseKeys = pulseKeys or if (menuScrollAccumX < 0) NativeGame.KEY_R else NativeGame.KEY_L
                    } else {
                        val mapped = mapToGame(x, y)
                        if (mapped != null) NativeGame.nativeQueueTap(mapped.first, mapped.second)
                    }
                }
                MotionEvent.ACTION_CANCEL -> { /* nothing to release on cancel in menu */ }
            }
            return true
        }

        when (action) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                when {
                    pauseRect().contains(x, y) -> pulseKeys = pulseKeys or NativeGame.KEY_START
                    inCircle(x, y, fireCx(), fireCy(), fireR() * 1.15f) -> firePointer = id
                    inCircle(x, y, dashCx(), dashCy(), dashR() * 1.2f) -> dashPointer = id
                    x < width * 0.48f -> {
                        stickPointer = id
                        stickActive = true
                        val r = stickRadius()
                        stickBaseX = x.coerceIn(r + dp(8f), width * 0.48f)
                        stickBaseY = y.coerceIn(r + dp(8f), height - r - dp(8f))
                        updateStickFrom(x, y)
                    }
                }
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) {
                    val pid = event.getPointerId(i)
                    val px = event.getX(i)
                    val py = event.getY(i)
                    if (pid == stickPointer) updateStickFrom(px, py)
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                if (id == stickPointer || action == MotionEvent.ACTION_CANCEL) {
                    stickPointer = -1
                    stickActive = false
                    stickNx = 0f
                    stickNy = 0f
                    stickBaseX = stickHomeX()
                    stickBaseY = stickHomeY()
                    stickKnobX = stickBaseX
                    stickKnobY = stickBaseY
                }
                if (id == firePointer || action == MotionEvent.ACTION_CANCEL) firePointer = -1
                if (id == dashPointer || action == MotionEvent.ACTION_CANCEL) dashPointer = -1
            }
        }
        keys = gameplayKeys() or pulseKeys
        NativeGame.nativeSetKeys(keys)
        return true
    }
}

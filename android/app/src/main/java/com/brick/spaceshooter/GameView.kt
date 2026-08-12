package com.brick.spaceshooter

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.view.Choreographer
import android.view.MotionEvent
import android.view.View
import java.io.File

class GameView(context: Context) : View(context), Choreographer.FrameCallback {

    private val pixels = IntArray(NativeGame.SCREEN_W * NativeGame.SCREEN_H)
    private val bitmap = Bitmap.createBitmap(NativeGame.SCREEN_W, NativeGame.SCREEN_H, Bitmap.Config.ARGB_8888)
    private val paint = Paint().apply {
        isFilterBitmap = false
        isAntiAlias = false
        isDither = false
    }
    private val dest = Rect()
    private val choreographer = Choreographer.getInstance()
    private var running = false
    private var keys = 0
    private val saveFile = File(context.filesDir, "space_unlimited.sav")

    private val audioBuf = ShortArray(304)
    private val audioTrack: AudioTrack

    init {
        NativeGame.nativeInit()
        if (saveFile.exists()) {
            NativeGame.nativeLoadSave(saveFile.readBytes())
        }

        val minBuf = AudioTrack.getMinBufferSize(
            18157,
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
                    .setSampleRate(18157)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                    .build()
            )
            .setBufferSizeInBytes(minBuf.coerceAtLeast(304 * 4))
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()
        audioTrack.play()
    }

    fun resume() {
        running = true
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

    private fun persistSave() {
        try {
            saveFile.writeBytes(NativeGame.nativeGetSave())
        } catch (_: Exception) {}
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!running) return
        NativeGame.nativeSetKeys(keys)
        NativeGame.nativeTick()
        NativeGame.nativePresent(pixels)
        bitmap.setPixels(pixels, 0, NativeGame.SCREEN_W, 0, 0, NativeGame.SCREEN_W, NativeGame.SCREEN_H)
        val n = NativeGame.nativeMixAudio(audioBuf)
        if (n > 0) audioTrack.write(audioBuf, 0, n)
        invalidate()
        choreographer.postFrameCallback(this)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawColor(0xFF050810.toInt())
        val vw = width
        val vh = height
        val scale = minOf(vw / NativeGame.SCREEN_W.toFloat(), vh / NativeGame.SCREEN_H.toFloat())
        val dw = (NativeGame.SCREEN_W * scale).toInt()
        val dh = (NativeGame.SCREEN_H * scale).toInt()
        val left = (vw - dw) / 2
        val top = (vh - dh) / 2
        dest.set(left, top, left + dw, top + dh)
        canvas.drawBitmap(bitmap, null, dest, paint)
        drawOverlay(canvas)
    }

    private fun drawOverlay(canvas: Canvas) {
        val p = Paint().apply { isAntiAlias = true; textAlign = Paint.Align.CENTER; textSize = 22f }
        fun btn(r: Rect, label: String, bit: Int) {
            p.color = if (keys and bit != 0) 0xAA23D6FF.toInt() else 0x66282730.toInt()
            canvas.drawRoundRect(r.left.toFloat(), r.top.toFloat(), r.right.toFloat(), r.bottom.toFloat(), 16f, 16f, p)
            p.color = 0xE0FFFFFF.toInt()
            canvas.drawText(label, r.exactCenterX(), r.exactCenterY() + 8f, p)
        }
        btn(rectUp(), "▲", NativeGame.KEY_UP)
        btn(rectDown(), "▼", NativeGame.KEY_DOWN)
        btn(rectLeft(), "◀", NativeGame.KEY_LEFT)
        btn(rectRight(), "▶", NativeGame.KEY_RIGHT)
        btn(rectA(), "A", NativeGame.KEY_A)
        btn(rectB(), "B", NativeGame.KEY_B)
        btn(rectL(), "L", NativeGame.KEY_L)
        btn(rectR(), "R", NativeGame.KEY_R)
        btn(rectStart(), "ST", NativeGame.KEY_START)
        btn(rectSelect(), "SE", NativeGame.KEY_SELECT)
    }

    private fun dpadSize() = (minOf(width, height) * 0.12f).toInt().coerceAtLeast(48)
    private fun dpadCx() = (width * 0.16f).toInt()
    private fun dpadCy() = (height * 0.72f).toInt()

    private fun rectUp(): Rect {
        val s = dpadSize(); val cx = dpadCx(); val cy = dpadCy()
        return Rect(cx - s / 2, cy - s * 2, cx + s / 2, cy - s / 2)
    }
    private fun rectDown(): Rect {
        val s = dpadSize(); val cx = dpadCx(); val cy = dpadCy()
        return Rect(cx - s / 2, cy + s / 2, cx + s / 2, cy + s * 2)
    }
    private fun rectLeft(): Rect {
        val s = dpadSize(); val cx = dpadCx(); val cy = dpadCy()
        return Rect(cx - s * 2, cy - s / 2, cx - s / 2, cy + s / 2)
    }
    private fun rectRight(): Rect {
        val s = dpadSize(); val cx = dpadCx(); val cy = dpadCy()
        return Rect(cx + s / 2, cy - s / 2, cx + s * 2, cy + s / 2)
    }
    private fun rectA(): Rect {
        val s = dpadSize(); return Rect(width - s * 3, height - s * 4, width - s, height - s * 2)
    }
    private fun rectB(): Rect {
        val s = dpadSize(); return Rect(width - s * 5, height - s * 3, width - s * 3, height - s)
    }
    private fun rectL(): Rect {
        val s = dpadSize(); return Rect(16, 16, 16 + s * 2, 16 + s)
    }
    private fun rectR(): Rect {
        val s = dpadSize(); return Rect(width - 16 - s * 2, 16, width - 16, 16 + s)
    }
    private fun rectStart(): Rect {
        val s = dpadSize(); return Rect(width / 2 + 8, height - s - 12, width / 2 + s + 8, height - 12)
    }
    private fun rectSelect(): Rect {
        val s = dpadSize(); return Rect(width / 2 - s - 8, height - s - 12, width / 2 - 8, height - 12)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        var next = 0
        val count = event.pointerCount
        val action = event.actionMasked
        for (i in 0 until count) {
            if (action == MotionEvent.ACTION_UP ||
                (action == MotionEvent.ACTION_POINTER_UP && i == event.actionIndex)
            ) continue
            val x = event.getX(i).toInt()
            val y = event.getY(i).toInt()
            if (rectUp().contains(x, y)) next = next or NativeGame.KEY_UP
            if (rectDown().contains(x, y)) next = next or NativeGame.KEY_DOWN
            if (rectLeft().contains(x, y)) next = next or NativeGame.KEY_LEFT
            if (rectRight().contains(x, y)) next = next or NativeGame.KEY_RIGHT
            if (rectA().contains(x, y)) next = next or NativeGame.KEY_A
            if (rectB().contains(x, y)) next = next or NativeGame.KEY_B
            if (rectL().contains(x, y)) next = next or NativeGame.KEY_L
            if (rectR().contains(x, y)) next = next or NativeGame.KEY_R
            if (rectStart().contains(x, y)) next = next or NativeGame.KEY_START
            if (rectSelect().contains(x, y)) next = next or NativeGame.KEY_SELECT
        }
        keys = next
        NativeGame.nativeSetKeys(keys)
        return true
    }
}

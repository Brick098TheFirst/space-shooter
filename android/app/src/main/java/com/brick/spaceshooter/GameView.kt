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
import android.os.Build
import android.os.VibrationAttributes
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.text.InputFilter
import android.text.InputType
import android.view.Choreographer
import android.view.HapticFeedbackConstants
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.VelocityTracker
import android.view.View
import android.view.ViewConfiguration
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import java.io.File
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

class GameView(context: Context) : View(context), Choreographer.FrameCallback {

    // Adaptive widescreen: the native frame is always 160 px tall, and its
    // width (frameW) follows this view's aspect ratio so the picture fills
    // the whole screen — no side bars, always landscape.
    private var frameW = NativeGame.DEFAULT_FRAME_W
    private val pixels = IntArray(NativeGame.MAX_FRAME_W * NativeGame.SCREEN_H)
    private var bitmap = Bitmap.createBitmap(NativeGame.DEFAULT_FRAME_W, NativeGame.SCREEN_H, Bitmap.Config.ARGB_8888)
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

    // Epic Online Services status. EOS itself is ticked on this same UI thread
    // because the SDK is not thread-safe and was initialized by MainActivity.
    private var eosStatus = NativeGame.EOS_CONFIG_REQUIRED
    private var eosStatusText = "Online co-op needs setup"
    private var eosStatusPoll = 0

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

    // ── Menu touch gesture state ─────────────────────────────────────────
    private val velocityTracker = VelocityTracker.obtain()
    private val touchSlop = ViewConfiguration.get(context).scaledTouchSlop
    private var menuPointerId = -1
    private var menuDownX = 0f
    private var menuDownY = 0f
    private var menuGesture = GESTURE_NONE
    private var menuAxis = AXIS_NONE

    // ── Smooth scroll model (game pixels; native holds the real value) ──
    private var scrollPx = 0f
    private var scrollMax = 0f
    private var dragging = false
    private var flinging = false
    private var snapping = false
    private var flingVel = 0f
    private var snapTarget = 0f

    // Haptics (Settings -> HAPTICS). Use VibratorManager on API 31+;
    // the old VIBRATOR_SERVICE path is silent on many modern phones.
    private val vibrator: Vibrator? = try {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            context.getSystemService(VibratorManager::class.java)?.defaultVibrator
        } else {
            @Suppress("DEPRECATION")
            context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
        }
    } catch (_: Exception) {
        null
    }
    private val hapticBuf = IntArray(8)
    private var hapticsEnabled = true

    // Cheat-code entry (Settings -> CODES row in the native menu)
    private var cheatDialogShowing = false
    private var eraseDialogShowing = false

    // Physical gamepad / Bluetooth controller. Keys stay latched until
    // KEY_UP so menus and hold-to-beam work the same as the GBA pad.
    private var padKeys = 0
    private var analogPadKeys = 0
    private var padStickNx = 0f
    private var padStickNy = 0f
    private var padTriggerFire = false
    private var padTriggerBeam = false
    private var lastControllerNanos = 0L
    private var controllerLive = false

    private fun controllerBits(): Int {
        var bits = padKeys or analogPadKeys
        if (padTriggerFire) bits = bits or NativeGame.KEY_A
        if (padTriggerBeam) bits = bits or NativeGame.KEY_B
        return bits
    }

    private companion object {
        const val GESTURE_NONE = 0
        const val GESTURE_SCROLL = 1
        const val GESTURE_CANCELLED = 2
        const val AXIS_NONE = 0
        const val AXIS_VERTICAL = 1
        const val AXIS_HORIZONTAL = 2
        const val ROW_H = 21f            // matches LIST_ROW_H in menu.c
        const val FLING_DECEL = 4.0f     // exponential friction (per second)
        const val FLING_MIN_V = 30f      // game px/s: stop fling below this
        const val FLING_START_V = 100f   // game px/s: below this, just snap
        const val SNAP_SPEED = 10f       // snap easing rate (per second)
    }

    init {
        isHapticFeedbackEnabled = true
        migrateLegacySave()
        NativeGame.nativeInit(saveDir.absolutePath)
        try {
            eosStatus = NativeGame.nativeEosGetStatus()
            eosStatusText = NativeGame.nativeEosGetStatusText()
        } catch (_: Throwable) {}

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
        syncViewport(w, h)
        updateDest(w, h)
        resetStickToHome()
    }

    /** Matches the native framebuffer width to this view's aspect ratio. */
    private fun syncViewport(vw: Int = width, vh: Int = height) {
        if (vw <= 0 || vh <= 0) return
        val wanted = ((vw.toLong() * NativeGame.SCREEN_H + vh / 2) / vh)
            .toInt()
            .coerceIn(NativeGame.MIN_FRAME_W, NativeGame.MAX_FRAME_W)
        NativeGame.nativeSetViewport(wanted)
    }

    private fun syncFrameWidth() {
        val fw = NativeGame.nativeGetFrameWidth()
        if (fw == frameW) return
        frameW = fw
        bitmap = Bitmap.createBitmap(fw, NativeGame.SCREEN_H, Bitmap.Config.ARGB_8888)
        updateDest()
    }

    private fun updateDest(vw: Int = width, vh: Int = height) {
        if (vw <= 0 || vh <= 0) return
        // frameW tracks the view aspect, so this normally fills the screen
        // exactly; centering only kicks in for the clamped extremes.
        val scale = min(vw / frameW.toFloat(), vh / NativeGame.SCREEN_H.toFloat())
        val dw = max(1, (frameW * scale).toInt())
        val dh = max(1, (NativeGame.SCREEN_H * scale).toInt())
        val left = (vw - dw) / 2
        val top = (vh - dh) / 2
        dest.set(left, top, left + dw, top + dh)
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!running) return

        try {
            NativeGame.nativeEosTick()
            eosStatusPoll++
            if (eosStatusPoll >= 15) {
                eosStatusPoll = 0
                eosStatus = NativeGame.nativeEosGetStatus()
                eosStatusText = NativeGame.nativeEosGetStatusText()
            }
        } catch (_: Throwable) {}

        val held = if (uiScreen == NativeGame.SCREEN_PLAYING) gameplayKeys() else 0
        keys = held or controllerBits() or pulseKeys
        pulseKeys = 0
        NativeGame.nativeSetKeys(keys)

        var dtSec = 0f
        if (lastFrameTimeNanos == 0L) {
            lastFrameTimeNanos = frameTimeNanos
            NativeGame.nativeTick()
            val n = NativeGame.nativeMixAudio(audioBuf)
            if (n > 0) audioTrack.write(audioBuf, 0, n)
        } else {
            val elapsed = frameTimeNanos - lastFrameTimeNanos
            lastFrameTimeNanos = frameTimeNanos
            val clampedElapsed = elapsed.coerceIn(0L, targetFrameNanos * 4)
            dtSec = clampedElapsed / 1_000_000_000f
            timeAccumulatorNanos += clampedElapsed

            var ticks = 0
            while (timeAccumulatorNanos >= targetFrameNanos && ticks < 4) {
                NativeGame.nativeSetKeys(keys)
                NativeGame.nativeTick()
                val n = NativeGame.nativeMixAudio(audioBuf)
                if (n > 0) audioTrack.write(audioBuf, 0, n)
                timeAccumulatorNanos -= targetFrameNanos
                ticks++
                keys = (if (NativeGame.nativeGetScreen() == NativeGame.SCREEN_PLAYING) gameplayKeys() else 0) or controllerBits()
            }
        }

        val nextScreen = NativeGame.nativeGetScreen()
        if (nextScreen != uiScreen) {
            if (nextScreen != NativeGame.SCREEN_PLAYING) resetStickToHome()
            stopScrollAnim()
            scrollPx = 0f
            scrollMax = NativeGame.nativeScrollMax()
            persistSave()
            uiScreen = nextScreen
        }
        updateScrollAnim(dtSec)
        persistCounter++
        if (persistCounter >= NativeGame.TARGET_FPS * 2) {
            persistCounter = 0
            persistSave()
        }

        // Settings flag + haptic events from the native game core.
        hapticsEnabled = NativeGame.nativeGetHaptics() != 0
        val hapticCount = NativeGame.nativeTakeHaptics(hapticBuf)
        if (hapticsEnabled) {
            for (i in 0 until hapticCount) buzzFor(hapticBuf[i])
        }

        // Settings -> CODES was activated: open the cheat-code dialog.
        if (NativeGame.nativeTakeCodeRequest() != 0) showCheatCodeDialog()
        // Settings -> ERASE DATA: confirm before wiping the save.
        if (NativeGame.nativeTakeEraseRequest() != 0) showEraseDataDialog()

        syncFrameWidth()
        NativeGame.nativePresent(pixels)
        bitmap.setPixels(pixels, 0, frameW, 0, 0, frameW, NativeGame.SCREEN_H)
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

    private fun onlineRect(): RectF {
        val w = dp(154f).coerceAtMost(width * 0.42f)
        val h = dp(38f)
        return RectF(width - w - dp(12f), dp(12f), width - dp(12f), dp(12f) + h)
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

    private fun usingController(): Boolean {
        if (!controllerLive) return false
        return System.nanoTime() - lastControllerNanos < 2_500_000_000L
    }

    private fun markController() {
        controllerLive = true
        lastControllerNanos = System.nanoTime()
    }

    private fun gameplayKeys(): Int {
        if (uiScreen != NativeGame.SCREEN_PLAYING) return 0
        var next = 0
        val dead = 0.28f
        val nx = if (usingController() && abs(padStickNx) > abs(stickNx)) padStickNx else stickNx
        val ny = if (usingController() && abs(padStickNy) > abs(stickNy)) padStickNy else stickNy
        if (nx < -dead) next = next or NativeGame.KEY_LEFT
        if (nx > dead) next = next or NativeGame.KEY_RIGHT
        if (ny < -dead) next = next or NativeGame.KEY_UP
        if (ny > dead) next = next or NativeGame.KEY_DOWN
        if (firePointer != -1) next = next or NativeGame.KEY_A
        if (dashPointer != -1) next = next or NativeGame.KEY_B
        return next
    }

    private fun pulseHaptic(constant: Int) {
        if (!hapticsEnabled) return
        try {
            performHapticFeedback(constant, HapticFeedbackConstants.FLAG_IGNORE_VIEW_SETTING)
        } catch (_: Exception) {}
    }

    private fun buzzFor(type: Int) {
        if (!hapticsEnabled) return
        val tick = when (type) {
            0 -> HapticFeedbackConstants.LONG_PRESS
            1, 2 -> if (Build.VERSION.SDK_INT >= 30)
                HapticFeedbackConstants.CONFIRM else HapticFeedbackConstants.VIRTUAL_KEY
            3 -> HapticFeedbackConstants.CLOCK_TICK
            else -> HapticFeedbackConstants.KEYBOARD_TAP
        }
        pulseHaptic(tick)

        val v = vibrator ?: return
        if (!v.hasVibrator()) return
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                val predefined = when (type) {
                    0 -> VibrationEffect.EFFECT_HEAVY_CLICK
                    1 -> VibrationEffect.EFFECT_CLICK
                    2 -> VibrationEffect.EFFECT_HEAVY_CLICK
                    3 -> VibrationEffect.EFFECT_TICK
                    else -> VibrationEffect.EFFECT_CLICK
                }
                val effect = VibrationEffect.createPredefined(predefined)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    val attrs = VibrationAttributes.Builder()
                        .setUsage(VibrationAttributes.USAGE_TOUCH)
                        .build()
                    v.vibrate(effect, attrs)
                } else {
                    v.vibrate(effect)
                }
            } else {
                val ms = when (type) {
                    0 -> 70L
                    1 -> 40L
                    2 -> 55L
                    3 -> 18L
                    else -> 30L
                }
                val amp = when (type) {
                    0 -> 255
                    1 -> 200
                    2 -> 255
                    3 -> 90
                    else -> 160
                }
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    v.vibrate(VibrationEffect.createOneShot(ms, amp))
                } else {
                    @Suppress("DEPRECATION")
                    v.vibrate(ms)
                }
            }
        } catch (_: Exception) {}
    }

    // ── Cheat codes (Settings -> CODES) ─────────────────────────────────
    // The native settings screen raises a request when the CODES row is
    // tapped; here we show a proper Android dialog with the soft keyboard.

    private fun showCheatCodeDialog() {
        if (cheatDialogShowing) return
        cheatDialogShowing = true

        val input = EditText(context).apply {
            hint = "ENTER CODE"
            setSingleLine()
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            filters = arrayOf(InputFilter.AllCaps(), InputFilter.LengthFilter(20))
            imeOptions = EditorInfo.IME_ACTION_DONE
            setSelectAllOnFocus(false)
        }
        val padH = dp(20f).toInt()
        val box = FrameLayout(context).apply {
            setPadding(padH, dp(6f).toInt(), padH, 0)
        }
        box.addView(
            input,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.WRAP_CONTENT
            )
        )

        val dialog = AlertDialog.Builder(context)
            .setTitle("CHEAT CODE")
            .setMessage("Enter a secret code:")
            .setView(box)
            .setPositiveButton("ENTER", null) // replaced below to keep the dialog open on a bad code
            .setNegativeButton("CANCEL") { d, _ -> d.dismiss() }
            .create()

        dialog.setOnDismissListener { cheatDialogShowing = false }
        dialog.setOnShowListener {
            input.requestFocus()
            dialog.window?.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE)
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                submitCheatCode(dialog, input)
            }
        }
        input.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                submitCheatCode(dialog, input)
                true
            } else {
                false
            }
        }
        dialog.show()
    }

    private fun submitCheatCode(dialog: AlertDialog, input: EditText) {
        val code = input.text.toString().trim().uppercase()
        if (code.isEmpty()) {
            input.error = "Enter a code"
            return
        }
        if (NativeGame.nativeApplyCheatCode(code) == 1) {
            persistSave() // native already wrote SRAM; make sure it hits disk right away
            pulseHaptic(HapticFeedbackConstants.CONFIRM)
            Toast.makeText(
                context,
                "CHEAT ACTIVATED! You now have \$999,000,000,000,000!",
                Toast.LENGTH_LONG
            ).show()
            dialog.dismiss()
        } else {
            pulseHaptic(HapticFeedbackConstants.LONG_PRESS)
            input.error = "Unknown code"
            input.selectAll()
        }
    }

    private fun showEraseDataDialog() {
        if (eraseDialogShowing) return
        eraseDialogShowing = true
        val dialog = AlertDialog.Builder(context)
            .setTitle("ERASE ALL DATA")
            .setMessage("Delete coins, unlocks, upgrades, high score, and settings? This cannot be undone.")
            .setPositiveButton("ERASE") { d, _ ->
                NativeGame.nativeResetAllData()
                persistSave()
                pulseHaptic(HapticFeedbackConstants.LONG_PRESS)
                Toast.makeText(context, "All data deleted.", Toast.LENGTH_SHORT).show()
                d.dismiss()
            }
            .setNegativeButton("CANCEL") { d, _ -> d.dismiss() }
            .create()
        dialog.setOnDismissListener { eraseDialogShowing = false }
        dialog.show()
    }

    private fun drawOverlay(canvas: Canvas) {
        when (uiScreen) {
            NativeGame.SCREEN_MAIN_MENU -> drawOnlineChip(canvas)
            NativeGame.SCREEN_PLAYING -> if (!usingController()) drawGameplayPad(canvas)
            NativeGame.SCREEN_HANGAR,
            NativeGame.SCREEN_SETTINGS,
            NativeGame.SCREEN_CONTROLS,
            NativeGame.SCREEN_OPTIONS,
            NativeGame.SCREEN_MODE_SELECT,
            NativeGame.SCREEN_MULTIPLAYER -> drawBackChip(canvas)
        }
    }

    private fun drawOnlineChip(canvas: Canvas) {
        val r = onlineRect()
        val accent = when (eosStatus) {
            NativeGame.EOS_READY -> 0xFF52E690.toInt()
            NativeGame.EOS_MATCHED -> 0xFF23D6FF.toInt()
            NativeGame.EOS_ERROR -> 0xFFFF665A.toInt()
            NativeGame.EOS_CONFIG_REQUIRED -> 0xFFFFC84A.toInt()
            else -> 0xFF9B8CFF.toInt()
        }
        val title = when (eosStatus) {
            NativeGame.EOS_CONFIG_REQUIRED -> "ONLINE SETUP"
            NativeGame.EOS_INITIALIZING, NativeGame.EOS_SIGNING_IN -> "EPIC CONNECTING..."
            NativeGame.EOS_READY -> "QUICK MATCH"
            NativeGame.EOS_MATCHMAKING -> "SEARCHING..."
            NativeGame.EOS_WAITING_FOR_PLAYER -> "WAITING 1 / 2"
            NativeGame.EOS_MATCHED -> "CO-OP 2 / 2"
            NativeGame.EOS_ERROR -> "ONLINE ERROR"
            else -> "ONLINE CO-OP"
        }
        overlay.style = Paint.Style.FILL
        overlay.color = 0xCC121B2A.toInt()
        canvas.drawRoundRect(r, dp(18f), dp(18f), overlay)
        overlay.style = Paint.Style.STROKE
        overlay.strokeWidth = dp(2f)
        overlay.color = accent
        canvas.drawRoundRect(r, dp(18f), dp(18f), overlay)
        overlay.style = Paint.Style.FILL
        canvas.drawCircle(r.left + dp(15f), r.centerY(), dp(4f), overlay)
        labelPaint.textSize = dp(11.5f)
        labelPaint.color = 0xF2FFFFFF.toInt()
        canvas.drawText(title, r.centerX() + dp(6f), r.centerY() + dp(4f), labelPaint)
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
        drawRoundAction(canvas, dashCx(), dashCy(), dashR(), "BEAM", dashPointer != -1, 0xFFFFC84A.toInt())

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
        val gx = ((x - dest.left) * frameW / dest.width()).toInt()
        val gy = ((y - dest.top) * NativeGame.SCREEN_H / dest.height()).toInt()
        return gx.coerceIn(0, frameW - 1) to gy.coerceIn(0, NativeGame.SCREEN_H - 1)
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
        uiScreen == NativeGame.SCREEN_OPTIONS ||
        uiScreen == NativeGame.SCREEN_MODE_SELECT ||
        uiScreen == NativeGame.SCREEN_MULTIPLAYER

    override fun onTouchEvent(event: MotionEvent): Boolean {
        return if (uiScreen == NativeGame.SCREEN_PLAYING) {
            handleGameplayTouch(event)
        } else {
            handleMenuTouch(event)
        }
    }

    // ── Menu touch: tap vs drag vs fling ─────────────────────────────────
    // A "tap" only fires when the finger goes down AND comes up within touch
    // slop of the same spot — sliding onto a button no longer activates it.

    private fun gameScale() =
        if (dest.width() > 0) dest.width() / frameW.toFloat() else 1f

    private fun isScrollableScreen() =
        uiScreen == NativeGame.SCREEN_HANGAR || uiScreen == NativeGame.SCREEN_SETTINGS

    private fun handleMenuTouch(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                menuPointerId = event.getPointerId(0)
                menuDownX = event.getX(0)
                menuDownY = event.getY(0)
                menuGesture = GESTURE_NONE
                menuAxis = AXIS_NONE
                stopScrollAnim()
                velocityTracker.clear()
                velocityTracker.addMovement(event)
                // Adopt whatever native currently has (handles tab/screen resets).
                scrollPx = NativeGame.nativeScrollGet()
                scrollMax = NativeGame.nativeScrollMax()
                return true
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                // Second finger: cancel the gesture so it can't turn into a tap.
                cancelMenuGesture()
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                val idx = event.findPointerIndex(menuPointerId)
                if (idx < 0) return true
                velocityTracker.addMovement(event)
                val x = event.getX(idx)
                val y = event.getY(idx)
                val dx = x - menuDownX
                val dy = y - menuDownY

                if (menuGesture == GESTURE_NONE && hypot(dx, dy) > touchSlop) {
                    menuAxis = if (abs(dx) > abs(dy)) AXIS_HORIZONTAL else AXIS_VERTICAL
                    if (menuAxis == AXIS_VERTICAL && isScrollableScreen()) {
                        menuGesture = GESTURE_SCROLL
                        dragging = true
                    } else if (menuAxis == AXIS_HORIZONTAL && uiScreen == NativeGame.SCREEN_HANGAR) {
                        menuGesture = GESTURE_SCROLL
                    } else {
                        // Axis isn't scrollable here — treat as a cancelled tap.
                        menuGesture = GESTURE_CANCELLED
                        menuAxis = AXIS_NONE
                    }
                }

                if (dragging) {
                    // 1:1 finger tracking: finger up = scroll down (offset grows).
                    scrollPx = NativeGame.nativeScrollTo(scrollPx - dy / gameScale())
                }
                return true
            }
            MotionEvent.ACTION_POINTER_UP -> {
                if (event.getPointerId(event.actionIndex) == menuPointerId) {
                    velocityTracker.addMovement(event)
                    finishMenuGesture(event, event.actionIndex)
                    menuPointerId = -1
                }
                return true
            }
            MotionEvent.ACTION_UP -> {
                val idx = event.findPointerIndex(menuPointerId)
                if (idx >= 0) velocityTracker.addMovement(event)
                finishMenuGesture(event, idx)
                menuPointerId = -1
                return true
            }
            MotionEvent.ACTION_CANCEL -> {
                cancelMenuGesture()
                return true
            }
        }
        return true
    }

    private fun finishMenuGesture(event: MotionEvent, idx: Int) {
        if (idx < 0) { cancelMenuGesture(); return }
        val x = event.getX(idx)
        val y = event.getY(idx)

        if (menuGesture == GESTURE_SCROLL) {
            if (dragging) {
                dragging = false
                velocityTracker.computeCurrentVelocity(1000)
                val v = -velocityTracker.yVelocity / gameScale() // game px/s, + = scroll down
                if (abs(v) > FLING_START_V) {
                    flinging = true
                    flingVel = v
                    snapTarget = nearestScrollBoundary()
                } else {
                    startSnap()
                }
            } else if (menuAxis == AXIS_HORIZONTAL) {
                // Horizontal swipe across the hangar tab strip.
                if (x - menuDownX < -touchSlop * 2) pulseKeys = pulseKeys or NativeGame.KEY_R
                else if (x - menuDownX > touchSlop * 2) pulseKeys = pulseKeys or NativeGame.KEY_L
            }
        } else {
            // Undecided = never left slop → a real tap (at the DOWN position).
            if (menuGesture == GESTURE_NONE) performMenuTap(menuDownX, menuDownY)
        }
    }

    private fun cancelMenuGesture() {
        dragging = false
        flinging = false
        snapping = false
        flingVel = 0f
        menuGesture = GESTURE_CANCELLED
        menuAxis = AXIS_NONE
    }

    private fun performMenuTap(x: Float, y: Float) {
        if (uiScreen == NativeGame.SCREEN_MAIN_MENU && onlineRect().contains(x, y)) {
            pulseHaptic(HapticFeedbackConstants.VIRTUAL_KEY)
            handleOnlineChip()
            return
        }
        if (needsBackChip() && backRect().contains(x, y)) {
            pulseHaptic(HapticFeedbackConstants.VIRTUAL_KEY)
            NativeGame.nativeGoBack()
            return
        }
        val mapped = mapToGame(x, y) ?: return
        pulseHaptic(HapticFeedbackConstants.VIRTUAL_KEY)
        NativeGame.nativeQueueTap(mapped.first, mapped.second)
    }

    /**
     * The main-menu online chip now routes to the native Multiplayer tab,
     * which owns all Quick Match controls (find / cancel / launch) plus the
     * live lobby status. No more dialog popups for ordinary flow.
     */
    private fun handleOnlineChip() {
        try {
            eosStatus = NativeGame.nativeEosGetStatus()
            eosStatusText = NativeGame.nativeEosGetStatusText()
            NativeGame.nativeOpenMultiplayer()
        } catch (error: Throwable) {
            Toast.makeText(context, "EOS unavailable: ${error.message}", Toast.LENGTH_LONG).show()
        }
    }

    // ── Smooth scroll animation (run from doFrame) ──────────────────────

    private fun nearestScrollBoundary(): Float =
        (scrollPx / ROW_H).roundToInt().coerceIn(0, (scrollMax / ROW_H).roundToInt()) * ROW_H

    private fun startSnap() {
        flinging = false
        snapping = true
        snapTarget = nearestScrollBoundary()
    }

    private fun stopScrollAnim() {
        flinging = false
        snapping = false
        flingVel = 0f
    }

    private fun updateScrollAnim(dtSec: Float) {
        if (flinging) {
            scrollPx = NativeGame.nativeScrollTo(scrollPx + flingVel * dtSec)
            flingVel *= exp((-FLING_DECEL * dtSec).toDouble()).toFloat()
            if (scrollPx <= 0f || scrollPx >= scrollMax || abs(flingVel) < FLING_MIN_V) {
                startSnap()
            }
        } else if (snapping) {
            scrollPx += (snapTarget - scrollPx) * min(1f, SNAP_SPEED * dtSec)
            if (abs(snapTarget - scrollPx) < 0.5f) {
                scrollPx = NativeGame.nativeScrollTo(snapTarget)
                snapping = false
            } else {
                NativeGame.nativeScrollTo(scrollPx)
            }
        }
    }

    // ── Gameplay touch: pointer-tracked buttons (down must start on them) ─

    private fun handleGameplayTouch(event: MotionEvent): Boolean {
        controllerLive = false
        val action = event.actionMasked
        val index = event.actionIndex
        val id = event.getPointerId(index)
        val x = event.getX(index)
        val y = event.getY(index)

        when (action) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                when {
                    pauseRect().contains(x, y) -> {
                        pulseKeys = pulseKeys or NativeGame.KEY_START
                        pulseHaptic(HapticFeedbackConstants.VIRTUAL_KEY)
                    }
                    inCircle(x, y, fireCx(), fireCy(), fireR() * 1.15f) -> {
                        firePointer = id
                        pulseHaptic(HapticFeedbackConstants.VIRTUAL_KEY)
                    }
                    inCircle(x, y, dashCx(), dashCy(), dashR() * 1.2f) -> {
                        dashPointer = id
                        pulseHaptic(HapticFeedbackConstants.VIRTUAL_KEY)
                    }
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
                    if (pid == stickPointer) updateStickFrom(event.getX(i), event.getY(i))
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
        keys = gameplayKeys() or controllerBits() or pulseKeys
        NativeGame.nativeSetKeys(keys)
        return true
    }

    // ── Physical controller / gamepad ─────────────────────────────────

    private fun isGamepadSource(source: Int): Boolean {
        return source and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
            source and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK ||
            source and InputDevice.SOURCE_DPAD == InputDevice.SOURCE_DPAD
    }

    private fun setPadBit(mask: Int, down: Boolean) {
        padKeys = if (down) padKeys or mask else padKeys and mask.inv()
    }

    fun handleKeyEvent(event: KeyEvent): Boolean {
        val code = event.keyCode
        if (code == KeyEvent.KEYCODE_VOLUME_UP ||
            code == KeyEvent.KEYCODE_VOLUME_DOWN ||
            code == KeyEvent.KEYCODE_VOLUME_MUTE
        ) return false

        val fromPad = isGamepadSource(event.source) ||
            (code in KeyEvent.KEYCODE_BUTTON_A..KeyEvent.KEYCODE_BUTTON_MODE) ||
            code == KeyEvent.KEYCODE_DPAD_UP ||
            code == KeyEvent.KEYCODE_DPAD_DOWN ||
            code == KeyEvent.KEYCODE_DPAD_LEFT ||
            code == KeyEvent.KEYCODE_DPAD_RIGHT ||
            code == KeyEvent.KEYCODE_DPAD_CENTER ||
            code == KeyEvent.KEYCODE_BUTTON_START ||
            code == KeyEvent.KEYCODE_BUTTON_SELECT

        if (code == KeyEvent.KEYCODE_BACK) {
            if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                if (uiScreen == NativeGame.SCREEN_PLAYING) pulseKeys = pulseKeys or NativeGame.KEY_START
                else NativeGame.nativeGoBack()
            }
            return true
        }

        if (!fromPad) return false
        markController()
        val down = event.action == KeyEvent.ACTION_DOWN
        if (event.action != KeyEvent.ACTION_DOWN && event.action != KeyEvent.ACTION_UP) return true
        if (down && event.repeatCount > 0) return true

        when (code) {
            KeyEvent.KEYCODE_BUTTON_A,
            KeyEvent.KEYCODE_DPAD_CENTER -> setPadBit(NativeGame.KEY_A, down)
            KeyEvent.KEYCODE_BUTTON_R1,
            KeyEvent.KEYCODE_BUTTON_R2 -> setPadBit(NativeGame.KEY_A, down)
            KeyEvent.KEYCODE_BUTTON_B,
            KeyEvent.KEYCODE_BUTTON_X,
            KeyEvent.KEYCODE_BUTTON_L1,
            KeyEvent.KEYCODE_BUTTON_L2 -> setPadBit(NativeGame.KEY_B, down)
            KeyEvent.KEYCODE_BUTTON_START,
            KeyEvent.KEYCODE_BUTTON_MODE -> setPadBit(NativeGame.KEY_START, down)
            KeyEvent.KEYCODE_BUTTON_SELECT,
            KeyEvent.KEYCODE_BUTTON_Y -> setPadBit(NativeGame.KEY_SELECT, down)
            KeyEvent.KEYCODE_DPAD_UP -> setPadBit(NativeGame.KEY_UP, down)
            KeyEvent.KEYCODE_DPAD_DOWN -> setPadBit(NativeGame.KEY_DOWN, down)
            KeyEvent.KEYCODE_DPAD_LEFT -> setPadBit(NativeGame.KEY_LEFT, down)
            KeyEvent.KEYCODE_DPAD_RIGHT -> setPadBit(NativeGame.KEY_RIGHT, down)
            KeyEvent.KEYCODE_BUTTON_THUMBL -> { /* ignore click */ }
            else -> return false
        }
        keys = (if (uiScreen == NativeGame.SCREEN_PLAYING) gameplayKeys() else 0) or controllerBits() or pulseKeys
        NativeGame.nativeSetKeys(keys)
        return true
    }

    fun handleMotionEvent(event: MotionEvent): Boolean {
        if (!isGamepadSource(event.source)) return false
        markController()
        val dead = 0.35f
        val sx = event.getAxisValue(MotionEvent.AXIS_X)
        val sy = event.getAxisValue(MotionEvent.AXIS_Y)
        val hx = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hy = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        padStickNx = sx
        padStickNy = sy

        var analog = 0
        if (hx < -dead || sx < -dead) analog = analog or NativeGame.KEY_LEFT
        if (hx > dead || sx > dead) analog = analog or NativeGame.KEY_RIGHT
        if (hy < -dead || sy < -dead) analog = analog or NativeGame.KEY_UP
        if (hy > dead || sy > dead) analog = analog or NativeGame.KEY_DOWN
        analogPadKeys = analog

        val ltrig = event.getAxisValue(MotionEvent.AXIS_LTRIGGER)
            .coerceAtLeast(event.getAxisValue(MotionEvent.AXIS_BRAKE))
        val rtrig = event.getAxisValue(MotionEvent.AXIS_RTRIGGER)
            .coerceAtLeast(event.getAxisValue(MotionEvent.AXIS_GAS))
        padTriggerFire = rtrig > 0.4f
        padTriggerBeam = ltrig > 0.4f

        keys = (if (uiScreen == NativeGame.SCREEN_PLAYING) gameplayKeys() else 0) or controllerBits() or pulseKeys
        NativeGame.nativeSetKeys(keys)
        return true
    }
}

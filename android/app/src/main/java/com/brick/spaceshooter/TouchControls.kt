package com.brick.spaceshooter

import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.view.MotionEvent
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt

class TouchControls(private val saveManager: SaveManager) {

    // Virtual Analog Joystick
    var joyX: Float = 0f // -1f..+1f
    var joyY: Float = 0f // -1f..+1f
    var isJoystickActive = false

    private var joyTouchId = -1
    private var baseTouchX = 65f
    private var baseTouchY = VIRTUAL_HEIGHT - 45f
    private var knobX = 65f
    private var knobY = VIRTUAL_HEIGHT - 45f
    private val maxRadius = 28f
    private val deadzone = 0.12f

    // Action buttons
    val fireButtonRect = RectF(VIRTUAL_WIDTH - 65f, VIRTUAL_HEIGHT - 60f, VIRTUAL_WIDTH - 15f, VIRTUAL_HEIGHT - 10f)
    val dashButtonRect = RectF(VIRTUAL_WIDTH - 115f, VIRTUAL_HEIGHT - 45f, VIRTUAL_WIDTH - 75f, VIRTUAL_HEIGHT - 5f)
    val pauseButtonRect = RectF(VIRTUAL_WIDTH - 32f, 4f, VIRTUAL_WIDTH - 4f, 26f)

    var isFirePressed = false
    var isDashPressed = false
    var isPausePressed = false

    var fireTapped = false
    var dashTapped = false
    var pauseTapped = false

    // Paints for controls HUD
    private val basePaint = Paint().apply {
        color = Color.argb(80, 255, 255, 255)
        style = Paint.Style.STROKE
        strokeWidth = 2f
        isAntiAlias = false
    }

    private val knobPaint = Paint().apply {
        color = Color.argb(140, 42, 214, 255)
        style = Paint.Style.FILL
        isAntiAlias = false
    }

    private val btnPaint = Paint().apply {
        color = Color.argb(100, 255, 70, 70)
        style = Paint.Style.FILL
        isAntiAlias = false
    }

    private val btnDashPaint = Paint().apply {
        color = Color.argb(100, 102, 255, 184)
        style = Paint.Style.FILL
        isAntiAlias = false
    }

    private val btnPausePaint = Paint().apply {
        color = Color.argb(100, 255, 210, 74)
        style = Paint.Style.FILL
        isAntiAlias = false
    }

    private val textPaint = Paint().apply {
        color = Color.WHITE
        style = Paint.Style.FILL
        isAntiAlias = false
    }

    fun clearFrameTaps() {
        fireTapped = false
        dashTapped = false
        pauseTapped = false
    }

    fun reset() {
        joyX = 0f
        joyY = 0f
        isJoystickActive = false
        joyTouchId = -1
        isFirePressed = false
        isDashPressed = false
        isPausePressed = false
        clearFrameTaps()
    }

    fun handleTouchEvent(event: MotionEvent, scaleX: Float, scaleY: Float): Boolean {
        val action = event.actionMasked
        val ptrIndex = event.actionIndex
        val ptrId = event.getPointerId(ptrIndex)

        val tx = event.getX(ptrIndex) / scaleX
        val ty = event.getY(ptrIndex) / scaleY

        when (action) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                if (pauseButtonRect.contains(tx, ty)) {
                    isPausePressed = true
                    pauseTapped = true
                    return true
                }
                if (fireButtonRect.contains(tx, ty)) {
                    isFirePressed = true
                    fireTapped = true
                    return true
                }
                if (dashButtonRect.contains(tx, ty)) {
                    isDashPressed = true
                    dashTapped = true
                    return true
                }
                if (tx < VIRTUAL_WIDTH * 0.5f && joyTouchId == -1) {
                    joyTouchId = ptrId
                    isJoystickActive = true
                    if (saveManager.joystickMode == "dynamic") {
                        baseTouchX = tx
                        baseTouchY = ty
                    } else {
                        baseTouchX = 65f
                        baseTouchY = VIRTUAL_HEIGHT - 45f
                    }
                    updateJoystick(tx, ty)
                    return true
                }
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) {
                    val id = event.getPointerId(i)
                    val px = event.getX(i) / scaleX
                    val py = event.getY(i) / scaleY
                    if (id == joyTouchId) {
                        updateJoystick(px, py)
                    } else {
                        // Update button holding state
                        if (fireButtonRect.contains(px, py)) {
                            isFirePressed = true
                        }
                    }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                if (ptrId == joyTouchId) {
                    joyTouchId = -1
                    isJoystickActive = false
                    joyX = 0f
                    joyY = 0f
                    knobX = baseTouchX
                    knobY = baseTouchY
                }
                // Check if button released
                var anyFire = false
                var anyDash = false
                var anyPause = false
                for (i in 0 until event.pointerCount) {
                    if (i == ptrIndex && action != MotionEvent.ACTION_MOVE) continue
                    val px = event.getX(i) / scaleX
                    val py = event.getY(i) / scaleY
                    if (fireButtonRect.contains(px, py)) anyFire = true
                    if (dashButtonRect.contains(px, py)) anyDash = true
                    if (pauseButtonRect.contains(px, py)) anyPause = true
                }
                isFirePressed = anyFire
                isDashPressed = anyDash
                isPausePressed = anyPause
            }
        }
        return true
    }

    private fun updateJoystick(tx: Float, ty: Float) {
        val dx = tx - baseTouchX
        val dy = ty - baseTouchY
        val dist = sqrt(dx * dx + dy * dy)
        if (dist <= 1f) {
            joyX = 0f
            joyY = 0f
            knobX = baseTouchX
            knobY = baseTouchY
            return
        }

        val angle = atan2(dy, dx)
        val clampedDist = dist.coerceAtMost(maxRadius)
        knobX = baseTouchX + cos(angle) * clampedDist
        knobY = baseTouchY + sin(angle) * clampedDist

        val norm = (clampedDist / maxRadius)
        if (norm < deadzone) {
            joyX = 0f
            joyY = 0f
        } else {
            val scaled = (norm - deadzone) / (1f - deadzone)
            joyX = cos(angle) * scaled
            joyY = sin(angle) * scaled
        }
    }

    fun draw(canvas: Canvas) {
        // Draw Joystick
        if (isJoystickActive || saveManager.joystickMode == "fixed") {
            canvas.drawCircle(baseTouchX, baseTouchY, maxRadius, basePaint)
            canvas.drawCircle(knobX, knobY, 12f, knobPaint)
        }

        // Draw FIRE button
        canvas.drawCircle(fireButtonRect.centerX(), fireButtonRect.centerY(), 22f, btnPaint)
        GfxData.drawText(canvas, "FIRE", fireButtonRect.centerX(), fireButtonRect.centerY() - 3f, Color.WHITE, 1f, Paint.Align.CENTER)

        // Draw DASH / SHIELD button
        canvas.drawCircle(dashButtonRect.centerX(), dashButtonRect.centerY(), 18f, btnDashPaint)
        GfxData.drawText(canvas, "DASH", dashButtonRect.centerX(), dashButtonRect.centerY() - 3f, Color.WHITE, 1f, Paint.Align.CENTER)

        // Draw PAUSE button
        canvas.drawRect(pauseButtonRect, btnPausePaint)
        GfxData.drawText(canvas, "||", pauseButtonRect.centerX(), pauseButtonRect.centerY() - 3f, Color.WHITE, 1f, Paint.Align.CENTER)
    }

    // Helper for Menu Screen Touch Hit Testing
    fun checkHit(event: MotionEvent, scaleX: Float, scaleY: Float, rect: RectF): Boolean {
        if (event.actionMasked == MotionEvent.ACTION_DOWN) {
            val tx = event.x / scaleX
            val ty = event.y / scaleY
            return rect.contains(tx, ty)
        }
        return false
    }

    fun getTouchCoord(event: MotionEvent, scaleX: Float, scaleY: Float): Pair<Float, Float>? {
        if (event.actionMasked == MotionEvent.ACTION_DOWN) {
            return Pair(event.x / scaleX, event.y / scaleY)
        }
        return null
    }
}

package com.brick.spaceshooter

import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import kotlin.math.abs

enum class AstType {
    LARGE,
    MED_A,
    MED_B,
    SMALL,
    TINY
}

enum class PowerupType {
    SHIELD,
    RAPID,
    REPAIR
}

class SpriteRenderer {

    private val pixelPaint = Paint().apply {
        isFilterBitmap = false
        isAntiAlias = false
        style = Paint.Style.FILL
    }

    private val dstRect = RectF()
    private val srcRect = Rect()

    fun drawShip(
        canvas: Canvas,
        x: Float,
        y: Float,
        accentIdx: Int,
        animFrame: Int = 0,
        invulnerable: Boolean = false
    ) {
        if (invulnerable && ((animFrame / 4) % 2 == 1)) {
            return // flash when invulnerable
        }
        val idx = accentIdx.coerceIn(0, 8)
        val w = 20f
        val h = 16f
        val startX = x - w / 2f
        val startY = y - h / 2f

        if (idx < 8) {
            val bmp = GfxData.bmpShip[idx]
            dstRect.set(startX, startY, startX + w, startY + h)
            canvas.drawBitmap(bmp, null, dstRect, pixelPaint)
        } else {
            // Animated dynamic Rainbow Prism paint (idx 8)
            val pixels = GfxData.sprShip[8]
            for (sy in 0 until 16) {
                for (sx in 0 until 20) {
                    val pix = pixels[sy * 20 + sx]
                    if (pix == 0) continue

                    val rgb: IntArray
                    if (pix in 240..243) {
                        val phase = (animFrame / 2 + sx * 2 + sy) % 28
                        val step = (phase / 4) % 7
                        rgb = RAINBOW_COLORS_RGB[abs(step) % 7]
                    } else {
                        val c = GfxData.getColor(pix)
                        pixelPaint.color = c
                        canvas.drawRect(
                            startX + sx, startY + sy, startX + sx + 1f, startY + sy + 1f, pixelPaint
                        )
                        continue
                    }
                    pixelPaint.color = Color.rgb(rgb[0], rgb[1], rgb[2])
                    canvas.drawRect(
                        startX + sx, startY + sy, startX + sx + 1f, startY + sy + 1f, pixelPaint
                    )
                }
            }
        }
    }

    fun drawTrail(
        canvas: Canvas,
        x: Float,
        y: Float,
        trailIdx: Int,
        animFrame: Int = 0
    ) {
        val idx = trailIdx.coerceIn(0, 7)
        val phase = animFrame % 4
        val length = 6f + (if (phase == 0 || phase == 2) 2f else 0f)
        val width = 4f
        val startX = x - width / 2f
        val startY = y + 8f // below ship tail

        val rgb = if (idx == 7) {
            // Animated Rainbow Trail
            val step = (animFrame / 3) % 7
            RAINBOW_COLORS_RGB[abs(step) % 7]
        } else {
            ENGINE_TRAILS[idx].rgb
        }

        val colorMain = Color.rgb(rgb[0], rgb[1], rgb[2])
        val colorCore = Color.WHITE

        // Main flame
        pixelPaint.color = colorMain
        canvas.drawRect(startX, startY, startX + width, startY + length, pixelPaint)

        // Bright core
        pixelPaint.color = colorCore
        canvas.drawRect(startX + 1f, startY, startX + width - 1f, startY + length * 0.5f, pixelPaint)
    }

    fun drawLaser(
        canvas: Canvas,
        x: Float,
        y: Float,
        laserIdx: Int,
        heavy: Boolean,
        animFrame: Int = 0,
        downward: Boolean = false
    ) {
        val w = if (heavy) 6 else 4
        val h = if (heavy) 14 else 10
        val startX = x - w / 2f
        val startY = y - h / 2f
        val rawPixels = if (heavy) GfxData.sprLaserHeavy else GfxData.sprLaserStandard

        val laserColors = intArrayOf(21, 24, 28, 27, 26, 62, 116, 120, 70, 54, 66, 78)
        val baseColorIdx = laserColors.getOrElse(laserIdx) { 21 }
        val isRainbow = (laserIdx == 7)
        val isOmega = (laserIdx == 11)

        for (drawY in 0 until h) {
            val sy = if (downward) (h - 1 - drawY) else drawY
            for (sx in 0 until w) {
                val pix = rawPixels[sy * w + sx]
                if (pix == 0) continue

                if (isRainbow) {
                    if (pix == 16 || pix == 13) {
                        pixelPaint.color = Color.WHITE
                    } else {
                        val phase = (animFrame / 2) + sy * 2 + sx
                        val rgb = RAINBOW_COLORS_RGB[abs(phase) % 7]
                        pixelPaint.color = Color.rgb(rgb[0], rgb[1], rgb[2])
                    }
                } else if (isOmega) {
                    if (pix == 16 || pix == 13) {
                        pixelPaint.color = Color.WHITE
                    } else {
                        // Pulsing pink-magenta god beam
                        val pulse = (animFrame % 8) < 4
                        pixelPaint.color = if (pulse) Color.rgb(255, 90, 230) else Color.rgb(230, 40, 200)
                    }
                } else {
                    if (pix == 16 || pix == 13) {
                        pixelPaint.color = Color.WHITE
                    } else {
                        pixelPaint.color = GfxData.getColor(baseColorIdx)
                    }
                }

                canvas.drawRect(
                    startX + sx, startY + drawY, startX + sx + 1f, startY + drawY + 1f, pixelPaint
                )
            }
        }
    }

    fun drawAsteroid(canvas: Canvas, x: Float, y: Float, type: AstType) {
        val (w, h, bmp) = when (type) {
            AstType.LARGE -> Triple(24f, 24f, GfxData.bmpAstLarge)
            AstType.MED_A -> Triple(16f, 16f, GfxData.bmpAstMedA)
            AstType.MED_B -> Triple(16f, 16f, GfxData.bmpAstMedB)
            AstType.SMALL -> Triple(10f, 10f, GfxData.bmpAstSmall)
            AstType.TINY -> Triple(6f, 6f, GfxData.bmpAstTiny)
        }
        val startX = x - w / 2f
        val startY = y - h / 2f
        dstRect.set(startX, startY, startX + w, startY + h)
        canvas.drawBitmap(bmp, null, dstRect, pixelPaint)
    }

    fun drawDrone(canvas: Canvas, x: Float, y: Float) {
        val w = 18f
        val h = 14f
        val startX = x - w / 2f
        val startY = y - h / 2f
        dstRect.set(startX, startY, startX + w, startY + h)
        canvas.drawBitmap(GfxData.bmpDrone, null, dstRect, pixelPaint)
    }

    fun drawShield(canvas: Canvas, x: Float, y: Float) {
        val w = 24f
        val h = 24f
        val startX = x - w / 2f
        val startY = y - h / 2f
        dstRect.set(startX, startY, startX + w, startY + h)
        canvas.drawBitmap(GfxData.bmpShieldBubble, null, dstRect, pixelPaint)
    }

    fun drawPowerup(canvas: Canvas, x: Float, y: Float, type: PowerupType) {
        val w = 10f
        val h = 10f
        val startX = x - w / 2f
        val startY = y - h / 2f
        val bmp = when (type) {
            PowerupType.SHIELD -> GfxData.bmpPwrShield
            PowerupType.RAPID -> GfxData.bmpPwrRapid
            PowerupType.REPAIR -> GfxData.bmpPwrRepair
        }
        dstRect.set(startX, startY, startX + w, startY + h)
        canvas.drawBitmap(bmp, null, dstRect, pixelPaint)
    }

    fun drawExplosion(canvas: Canvas, x: Float, y: Float, frame: Int) {
        val safeFrame = frame.coerceIn(0, 8)
        val w = 24f
        val h = 24f
        val startX = x - w / 2f
        val startY = y - h / 2f
        dstRect.set(startX, startY, startX + w, startY + h)
        canvas.drawBitmap(GfxData.bmpExplosion[safeFrame], null, dstRect, pixelPaint)
    }

    fun drawText(
        canvas: Canvas,
        text: String,
        x: Float,
        y: Float,
        color: Int,
        scale: Float = 1f,
        align: Paint.Align = Paint.Align.LEFT
    ) {
        GfxData.drawText(canvas, text, x, y, color, scale, align)
    }

    fun measureTextWidth(text: String, scale: Float = 1f): Float {
        return GfxData.measureTextWidth(text, scale)
    }
}

package com.brick.spaceshooter

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import kotlin.math.abs

object GfxData {
    var loaded = false
    lateinit var palette: IntArray

    lateinit var sprShip: Array<IntArray>
    lateinit var sprAstLarge: IntArray
    lateinit var sprAstMedA: IntArray
    lateinit var sprAstMedB: IntArray
    lateinit var sprAstSmall: IntArray
    lateinit var sprAstTiny: IntArray
    lateinit var sprDrone: IntArray
    lateinit var sprLaserStandard: IntArray
    lateinit var sprLaserHeavy: IntArray
    lateinit var sprShieldBubble: IntArray
    lateinit var sprPwrShield: IntArray
    lateinit var sprPwrRapid: IntArray
    lateinit var sprPwrRepair: IntArray
    lateinit var sprExplosion: Array<IntArray>
    lateinit var font5x7: Array<IntArray>

    // Precomputed Bitmaps for instant Canvas rendering
    lateinit var bmpShip: Array<Bitmap>
    lateinit var bmpAstLarge: Bitmap
    lateinit var bmpAstMedA: Bitmap
    lateinit var bmpAstMedB: Bitmap
    lateinit var bmpAstSmall: Bitmap
    lateinit var bmpAstTiny: Bitmap
    lateinit var bmpDrone: Bitmap
    lateinit var bmpShieldBubble: Bitmap
    lateinit var bmpPwrShield: Bitmap
    lateinit var bmpPwrRapid: Bitmap
    lateinit var bmpPwrRepair: Bitmap
    lateinit var bmpExplosion: Array<Bitmap>

    private val textPaint = Paint().apply {
        isFilterBitmap = false
        isAntiAlias = false
        style = Paint.Style.FILL
    }

    fun init(context: Context) {
        if (loaded) return
        try {
            val jsonString = context.assets.open("gfx_data.json").bufferedReader().use { it.readText() }
            val root = JSONObject(jsonString)

            // Palette
            val palArr = root.getJSONArray("palette")
            palette = IntArray(palArr.length())
            for (i in 0 until palArr.length()) {
                val c = palArr.getJSONArray(i)
                palette[i] = Color.rgb(c.getInt(0), c.getInt(1), c.getInt(2))
            }

            // Ship (9 paints)
            val shipArr = root.getJSONArray("spr_ship")
            sprShip = Array(shipArr.length()) { i -> jsonArrayToIntArray(shipArr.getJSONArray(i)) }

            sprAstLarge = jsonArrayToIntArray(root.getJSONArray("spr_ast_large"))
            sprAstMedA = jsonArrayToIntArray(root.getJSONArray("spr_ast_med_a"))
            sprAstMedB = jsonArrayToIntArray(root.getJSONArray("spr_ast_med_b"))
            sprAstSmall = jsonArrayToIntArray(root.getJSONArray("spr_ast_small"))
            sprAstTiny = jsonArrayToIntArray(root.getJSONArray("spr_ast_tiny"))
            sprDrone = jsonArrayToIntArray(root.getJSONArray("spr_drone"))
            sprLaserStandard = jsonArrayToIntArray(root.getJSONArray("spr_laser_standard"))
            sprLaserHeavy = jsonArrayToIntArray(root.getJSONArray("spr_laser_heavy"))
            sprShieldBubble = jsonArrayToIntArray(root.getJSONArray("spr_shield_bubble"))
            sprPwrShield = jsonArrayToIntArray(root.getJSONArray("spr_pwr_shield"))
            sprPwrRapid = jsonArrayToIntArray(root.getJSONArray("spr_pwr_rapid"))
            sprPwrRepair = jsonArrayToIntArray(root.getJSONArray("spr_pwr_repair"))

            // Explosion (9 frames)
            val expArr = root.getJSONArray("spr_explosion")
            sprExplosion = Array(expArr.length()) { i -> jsonArrayToIntArray(expArr.getJSONArray(i)) }

            // Font 5x7
            val fontArr = root.getJSONArray("font_5x7")
            font5x7 = Array(fontArr.length()) { i -> jsonArrayToIntArray(fontArr.getJSONArray(i)) }

            // Build static Bitmaps
            bmpShip = Array(9) { i ->
                if (i == 8) createBitmapFromIndexed(20, 16, sprShip[0]) // default placeholder for dynamic 8
                else createBitmapFromIndexed(20, 16, sprShip[i])
            }
            bmpAstLarge = createBitmapFromIndexed(24, 24, sprAstLarge)
            bmpAstMedA = createBitmapFromIndexed(16, 16, sprAstMedA)
            bmpAstMedB = createBitmapFromIndexed(16, 16, sprAstMedB)
            bmpAstSmall = createBitmapFromIndexed(10, 10, sprAstSmall)
            bmpAstTiny = createBitmapFromIndexed(6, 6, sprAstTiny)
            bmpDrone = createBitmapFromIndexed(20, 16, sprDrone)
            bmpShieldBubble = createBitmapFromIndexed(24, 24, sprShieldBubble)
            bmpPwrShield = createBitmapFromIndexed(12, 12, sprPwrShield)
            bmpPwrRapid = createBitmapFromIndexed(12, 12, sprPwrRapid)
            bmpPwrRepair = createBitmapFromIndexed(12, 12, sprPwrRepair)
            bmpExplosion = Array(9) { i -> createBitmapFromIndexed(24, 24, sprExplosion[i]) }

            loaded = true
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    private fun jsonArrayToIntArray(arr: JSONArray): IntArray {
        val out = IntArray(arr.length())
        for (i in 0 until arr.length()) {
            out[i] = arr.getInt(i)
        }
        return out
    }

    fun getColor(idx: Int): Int {
        if (idx < 0 || idx >= palette.size) return Color.TRANSPARENT
        return palette[idx]
    }

    fun createBitmapFromIndexed(w: Int, h: Int, indices: IntArray): Bitmap {
        val colors = IntArray(w * h)
        for (i in indices.indices) {
            val idx = indices[i]
            if (idx == 0) {
                colors[i] = Color.TRANSPARENT
            } else {
                colors[i] = getColor(idx)
            }
        }
        return Bitmap.createBitmap(colors, w, h, Bitmap.Config.ARGB_8888)
    }

    // Draw retro GBA 5x7 font string
    fun drawText(
        canvas: Canvas,
        text: String,
        x: Float,
        y: Float,
        color: Int,
        scale: Float = 1f,
        align: Paint.Align = Paint.Align.LEFT
    ) {
        val charWidth = 6f * scale
        val strWidth = text.length * charWidth
        var startX = x
        if (align == Paint.Align.CENTER) {
            startX = x - strWidth / 2f
        } else if (align == Paint.Align.RIGHT) {
            startX = x - strWidth
        }

        textPaint.color = color
        for (i in text.indices) {
            val ch = text[i]
            val ascii = ch.code
            val idx = (ascii - 32).coerceIn(0, font5x7.size - 1)
            val charRows = font5x7[idx]
            val cx = startX + i * charWidth
            for (r in 0 until 7) {
                val rowBits = charRows[r]
                for (col in 0 until 5) {
                    if ((rowBits and (1 shl col)) != 0) {
                        canvas.drawRect(
                            cx + col * scale,
                            y + r * scale,
                            cx + (col + 1) * scale,
                            y + (r + 1) * scale,
                            textPaint
                        )
                    }
                }
            }
        }
    }

    fun measureTextWidth(text: String, scale: Float = 1f): Float {
        return text.length * 6f * scale
    }
}

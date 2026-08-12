package com.brick.spaceshooter

import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import kotlin.random.Random

data class Star(
    var x: Float,
    var y: Float,
    val speed: Float,
    val size: Float,
    val color: Int
)

class Starfield(private val width: Float, private val height: Float) {

    private val stars = mutableListOf<Star>()
    private val paint = Paint().apply {
        isAntiAlias = false
        style = Paint.Style.FILL
    }
    private val bgPaint = Paint().apply {
        color = Color.rgb(4, 6, 12)
        style = Paint.Style.FILL
    }

    init {
        initStars()
    }

    private fun initStars() {
        stars.clear()
        val r = Random(1337)

        // Layer 0: Slow faint distant stars
        for (i in 0 until 40) {
            val sx = r.nextFloat() * width
            val sy = r.nextFloat() * height
            val color = Color.rgb(42, 60, 105)
            stars.add(Star(sx, sy, 0.3f, 1f, color))
        }

        // Layer 1: Medium cyan/white stars
        for (i in 0 until 25) {
            val sx = r.nextFloat() * width
            val sy = r.nextFloat() * height
            val color = Color.rgb(110, 145, 215)
            stars.add(Star(sx, sy, 0.7f, 1f, color))
        }

        // Layer 2: Fast bright foreground stars
        for (i in 0 until 15) {
            val sx = r.nextFloat() * width
            val sy = r.nextFloat() * height
            val color = Color.rgb(220, 235, 255)
            stars.add(Star(sx, sy, 1.4f, 2f, color))
        }
    }

    fun update(dtMult: Float = 1f) {
        for (s in stars) {
            s.y += s.speed * dtMult
            if (s.y >= height) {
                s.y = 0f
                s.x = Random.nextFloat() * width
            }
        }
    }

    fun draw(canvas: Canvas) {
        canvas.drawRect(0f, 0f, width, height, bgPaint)
        for (s in stars) {
            paint.color = s.color
            canvas.drawRect(s.x, s.y, s.x + s.size, s.y + s.size, paint)
        }
    }
}

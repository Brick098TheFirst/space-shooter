package com.brick.spaceshooter

import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.view.MotionEvent
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.random.Random

data class Bullet(
    var x: Float,
    var y: Float,
    var vx: Float,
    var vy: Float,
    val damage: Int,
    val heavy: Boolean,
    val enemy: Boolean = false
)

data class Asteroid(
    var x: Float,
    var y: Float,
    var vx: Float,
    var vy: Float,
    val type: AstType,
    var hp: Int,
    val maxHp: Int
)

data class Drone(
    var x: Float,
    var y: Float,
    var vx: Float,
    var vy: Float,
    var hp: Int,
    var shootTimer: Int = 60,
    var burstShots: Int = 0,
    var burstTimer: Int = 0
)

data class Powerup(
    var x: Float,
    var y: Float,
    var vy: Float,
    val type: PowerupType
)

data class Explosion(
    var x: Float,
    var y: Float,
    var frame: Int = 0,
    var timer: Int = 0
)

data class DamageText(
    var x: Float,
    var y: Float,
    val text: String,
    val color: Int,
    var timer: Int = 40
)

class GameEngine(
    private val saveManager: SaveManager,
    private val audioManager: AudioManager,
    private val spriteRenderer: SpriteRenderer,
    private val touchControls: TouchControls
) {

    var currentScreen: Screen = Screen.MAIN_MENU

    private val starfield = Starfield(VIRTUAL_WIDTH, VIRTUAL_HEIGHT)

    // Player State
    var playerX = VIRTUAL_WIDTH / 2f
    var playerY = VIRTUAL_HEIGHT - 32f
    var playerLives = 3
    var playerShields = 0
    var fireCooldown = 0
    var dashCooldown = 0
    var invulnerableTimer = 0
    var rapidFireTimer = 0

    // Entities
    val bullets = mutableListOf<Bullet>()
    val asteroids = mutableListOf<Asteroid>()
    val drones = mutableListOf<Drone>()
    val powerups = mutableListOf<Powerup>()
    val explosions = mutableListOf<Explosion>()
    val damageTexts = mutableListOf<DamageText>()

    // Game stats
    var score = 0
    var wave = 1
    var combo = 1
    var comboTimer = 0
    var waveBannerTimer = 0

    // Animation & Timing
    var gameFrame = 0
    private var spawnTimer = 0
    private var droneSpawnTimer = 0

    // High refresh rate FPS counter
    var displayFps = 120
    private var fpsCounter = 0
    private var fpsTimer = 0L

    // Hangar / Shop UI State
    var shopCategory = 0 // 0: PAINTS, 1: TRAILS, 2: WEAPONS, 3: LASERS
    val shopSelected = intArrayOf(1, 1, 0, 0)
    var shopMessage = ""
    var shopMessageColor = Color.YELLOW
    var shopMessageTimer = 0

    // Upgrades UI State
    var upgSelected = 0
    var upgScroll = 0

    // UI Paints
    private val hudPaint = Paint().apply {
        isAntiAlias = false
        style = Paint.Style.FILL
    }
    private val bgOverlayPaint = Paint().apply {
        color = Color.argb(190, 4, 6, 12)
        style = Paint.Style.FILL
    }
    private val cardPaint = Paint().apply {
        color = Color.rgb(20, 30, 56)
        style = Paint.Style.FILL
    }
    private val cardBorderPaint = Paint().apply {
        color = Color.rgb(60, 85, 140)
        style = Paint.Style.STROKE
        strokeWidth = 2f
    }
    private val highlightBorderPaint = Paint().apply {
        color = Color.rgb(255, 210, 74)
        style = Paint.Style.STROKE
        strokeWidth = 2f
    }

    init {
        audioManager.playMusic(Bgm.MENU)
    }

    fun startNewGame() {
        score = 0
        wave = 1
        combo = 1
        comboTimer = 0
        waveBannerTimer = 90
        gameFrame = 0

        playerX = VIRTUAL_WIDTH / 2f
        playerY = VIRTUAL_HEIGHT - 32f

        val upgHull = saveManager.getUpgradeLevel(4)
        val hullLivesTable = intArrayOf(2, 3, 4, 5, 6, 7)
        playerLives = hullLivesTable[upgHull.coerceIn(0, 5)] + saveManager.difficulty.lifeBonus

        val upgShield = saveManager.getUpgradeLevel(3)
        val startShieldsTable = intArrayOf(0, 1, 1, 2, 2, 3)
        playerShields = startShieldsTable[upgShield.coerceIn(0, 5)]

        fireCooldown = 0
        dashCooldown = 0
        invulnerableTimer = 60
        rapidFireTimer = 0

        bullets.clear()
        asteroids.clear()
        drones.clear()
        powerups.clear()
        explosions.clear()
        damageTexts.clear()
        touchControls.reset()

        spawnWaveAsteroids()
        currentScreen = Screen.PLAYING
        audioManager.playMusic(Bgm.GAME)
    }

    private fun spawnWaveAsteroids() {
        val count = (5 + wave * 2).coerceAtMost(24)
        for (i in 0 until count) {
            val sx = Random.nextFloat() * (VIRTUAL_WIDTH - 40f) + 20f
            val sy = -Random.nextFloat() * 150f - 20f
            val vx = (Random.nextFloat() - 0.5f) * 1.5f
            val vy = Random.nextFloat() * 1.2f + 0.6f * saveManager.difficulty.speedMult
            asteroids.add(Asteroid(sx, sy, vx, vy, AstType.LARGE, 10, 10))
        }
    }

    // Fixed timestep simulation step (called at 120Hz physics rate)
    fun updatePhysics() {
        gameFrame++
        starfield.update(1f)

        when (currentScreen) {
            Screen.MAIN_MENU, Screen.HANGAR, Screen.UPGRADES, Screen.CONTROLS, Screen.CREDITS -> {
                if (shopMessageTimer > 0) shopMessageTimer--
                return
            }
            Screen.PAUSED, Screen.GAME_OVER -> {
                return
            }
            Screen.PLAYING -> {
                updateGameplay()
            }
        }
    }

    private fun updateGameplay() {
        // Timers
        if (fireCooldown > 0) fireCooldown--
        if (dashCooldown > 0) dashCooldown--
        if (invulnerableTimer > 0) invulnerableTimer--
        if (rapidFireTimer > 0) rapidFireTimer--
        if (waveBannerTimer > 0) waveBannerTimer--
        if (comboTimer > 0) {
            comboTimer--
            if (comboTimer == 0) {
                combo = 1
            }
        }

        // --- Player Movement via Virtual Joystick ---
        val upgEngine = saveManager.getUpgradeLevel(0)
        val speedMultTable = floatArrayOf(180f/256f, 220f/256f, 1.0f, 320f/256f, 410f/256f, 2.0f)
        val moveSpeed = 2.4f * speedMultTable[upgEngine.coerceIn(0, 5)]

        playerX = (playerX + touchControls.joyX * moveSpeed).coerceIn(16f, VIRTUAL_WIDTH - 16f)
        playerY = (playerY + touchControls.joyY * moveSpeed).coerceIn(16f, VIRTUAL_HEIGHT - 16f)

        // --- Handle Afterburner DASH ---
        if (touchControls.dashTapped && dashCooldown == 0) {
            val upgDash = saveManager.getUpgradeLevel(5)
            val dashCdTable = intArrayOf(84, 66, 52, 40, 30, 24)
            val invulnTable = intArrayOf(16, 19, 22, 25, 28, 31)

            dashCooldown = dashCdTable[upgDash.coerceIn(0, 5)]
            invulnerableTimer = invulnTable[upgDash.coerceIn(0, 5)]
            audioManager.playSfx(Sfx.PICKUP, 1.2f)
            audioManager.triggerHaptic(20L)
            touchControls.dashTapped = false
        }

        // --- Handle Firing ---
        if (touchControls.isFirePressed && fireCooldown == 0) {
            firePlayerWeapon()
        }

        // --- Update Bullets ---
        val bulletIter = bullets.iterator()
        while (bulletIter.hasNext()) {
            val b = bulletIter.next()
            b.x += b.vx
            b.y += b.vy
            if (b.y < -20f || b.y > VIRTUAL_HEIGHT + 20f || b.x < -20f || b.x > VIRTUAL_WIDTH + 20f) {
                bulletIter.remove()
            }
        }

        // --- Update Asteroids & Collision ---
        val astIter = asteroids.iterator()
        val newAsteroids = mutableListOf<Asteroid>()
        while (astIter.hasNext()) {
            val a = astIter.next()
            a.x += a.vx
            a.y += a.vy
            if (a.x < 10f || a.x > VIRTUAL_WIDTH - 10f) a.vx = -a.vx
            if (a.y > VIRTUAL_HEIGHT + 24f) {
                a.y = -20f
                a.x = Random.nextFloat() * (VIRTUAL_WIDTH - 40f) + 20f
            }

            // Bullet vs Asteroid
            val radA = getAsteroidRadius(a.type)
            val bulletHitIter = bullets.iterator()
            var destroyed = false
            while (bulletHitIter.hasNext()) {
                val b = bulletHitIter.next()
                if (b.enemy) continue
                val dx = a.x - b.x
                val dy = a.y - b.y
                if (dx * dx + dy * dy < (radA + 6f) * (radA + 6f)) {
                    a.hp -= b.damage
                    damageTexts.add(DamageText(a.x, a.y, "-${b.damage}", Color.WHITE))
                    bulletHitIter.remove()
                    audioManager.playSfx(Sfx.LASER, 0.4f)
                    if (a.hp <= 0) {
                        destroyed = true
                        break
                    }
                }
            }

            if (destroyed) {
                astIter.remove()
                handleAsteroidDestroyed(a, newAsteroids)
                continue
            }

            // Player vs Asteroid
            if (invulnerableTimer == 0) {
                val dx = a.x - playerX
                val dy = a.y - playerY
                if (dx * dx + dy * dy < (radA + 8f) * (radA + 8f)) {
                    damagePlayer(1)
                    a.hp -= 5
                    if (a.hp <= 0) {
                        astIter.remove()
                        handleAsteroidDestroyed(a, newAsteroids)
                    }
                }
            }
        }
        asteroids.addAll(newAsteroids)

        // --- Update Enemy Hunter Drones ---
        spawnTimer++
        if (spawnTimer >= 360 && drones.size < 3) {
            spawnTimer = 0
            drones.add(Drone(Random.nextFloat() * (VIRTUAL_WIDTH - 40f) + 20f, -20f, 0f, 0.8f, 15))
        }

        val droneIter = drones.iterator()
        while (droneIter.hasNext()) {
            val d = droneIter.next()
            d.y += d.vy
            val dx = playerX - d.x
            d.vx = (dx * 0.02f).coerceIn(-1.5f, 1.5f)
            d.x += d.vx
            if (d.y > VIRTUAL_HEIGHT + 30f) {
                droneIter.remove()
                continue
            }

            // Hunter shoot burst
            d.shootTimer--
            if (d.shootTimer <= 0) {
                d.shootTimer = 90
                d.burstShots = Random.nextInt(2, 5)
                d.burstTimer = 5
            }
            if (d.burstShots > 0) {
                d.burstTimer--
                if (d.burstTimer <= 0) {
                    d.burstTimer = 6
                    d.burstShots--
                    bullets.add(Bullet(d.x, d.y + 10f, 0f, 3.2f, 1, false, true))
                    audioManager.playSfx(Sfx.LASER, 0.5f)
                }
            }

            // Bullet vs Drone
            val bIter = bullets.iterator()
            while (bIter.hasNext()) {
                val b = bIter.next()
                if (b.enemy) continue
                val ddx = d.x - b.x
                val ddy = d.y - b.y
                if (ddx * ddx + ddy * ddy < 14f * 14f) {
                    d.hp -= b.damage
                    damageTexts.add(DamageText(d.x, d.y, "-${b.damage}", Color.YELLOW))
                    bIter.remove()
                    if (d.hp <= 0) {
                        droneIter.remove()
                        explosions.add(Explosion(d.x, d.y))
                        audioManager.playSfx(Sfx.EXPLOSION)
                        addScore(150 * combo)
                        awardCoins(25)
                        increaseCombo()
                        break
                    }
                }
            }

            // Enemy bullet vs Player
        }

        // Enemy bullet vs Player
        val bIter = bullets.iterator()
        while (bIter.hasNext()) {
            val b = bIter.next()
            if (!b.enemy) continue
            val dx = b.x - playerX
            val dy = b.y - playerY
            if (dx * dx + dy * dy < 10f * 10f) {
                bIter.remove()
                if (invulnerableTimer == 0) {
                    damagePlayer(b.damage)
                }
            }
        }

        // --- Update Powerups ---
        val pwrIter = powerups.iterator()
        while (pwrIter.hasNext()) {
            val p = pwrIter.next()
            p.y += p.vy

            // Magnetic pull if Scavenger upgraded
            val upgScav = saveManager.getUpgradeLevel(6)
            if (upgScav > 0) {
                val dx = playerX - p.x
                val dy = playerY - p.y
                val dist = sqrt(dx * dx + dy * dy)
                if (dist < 80f + upgScav * 20f) {
                    p.x += (dx / dist) * (1.5f + upgScav * 0.5f)
                    p.y += (dy / dist) * (1.5f + upgScav * 0.5f)
                }
            }

            if (p.y > VIRTUAL_HEIGHT + 20f) {
                pwrIter.remove()
                continue
            }

            val dx = p.x - playerX
            val dy = p.y - playerY
            if (dx * dx + dy * dy < 16f * 16f) {
                pwrIter.remove()
                collectPowerup(p.type)
            }
        }

        // --- Update Explosions ---
        val expIter = explosions.iterator()
        while (expIter.hasNext()) {
            val e = expIter.next()
            e.timer++
            if (e.timer >= 4) {
                e.timer = 0
                e.frame++
                if (e.frame >= 9) {
                    expIter.remove()
                }
            }
        }

        // --- Update Damage / Coin popup text ---
        val textIter = damageTexts.iterator()
        while (textIter.hasNext()) {
            val t = textIter.next()
            t.y -= 0.5f
            t.timer--
            if (t.timer <= 0) {
                textIter.remove()
            }
        }

        // --- Wave Completion Check ---
        if (asteroids.isEmpty() && drones.isEmpty()) {
            wave++
            waveBannerTimer = 90
            spawnWaveAsteroids()
        }
    }

    private fun firePlayerWeapon() {
        val rig = WEAPON_RIGS[saveManager.weaponRig.coerceIn(0, NUM_RIGS - 1)]
        val cdMultTable = floatArrayOf(1.0f, 210f/256f, 165f/256f, 125f/256f, 95f/256f, 75f/256f)
        val upgRate = saveManager.getUpgradeLevel(1)
        val baseCd = (rig.baseCd * cdMultTable[upgRate.coerceIn(0, 5)]).toInt().coerceAtLeast(3)

        fireCooldown = if (rapidFireTimer > 0) (baseCd / 2).coerceAtLeast(2) else baseCd

        val crystal = LASER_CRYSTALS[saveManager.laserIndex.coerceIn(0, NUM_LASERS - 1)]
        val upgDmg = saveManager.getUpgradeLevel(2)
        val totalDamage = 1 + crystal.bonusDmg + upgDmg

        val speedY = -6.5f
        when (rig.id) {
            0 -> bullets.add(Bullet(playerX, playerY - 12f, 0f, speedY, totalDamage, rig.heavy))
            1 -> {
                bullets.add(Bullet(playerX - 6f, playerY - 10f, 0f, speedY, totalDamage, rig.heavy))
                bullets.add(Bullet(playerX + 6f, playerY - 10f, 0f, speedY, totalDamage, rig.heavy))
            }
            2 -> {
                bullets.add(Bullet(playerX, playerY - 12f, 0f, speedY, totalDamage, rig.heavy))
                bullets.add(Bullet(playerX - 6f, playerY - 10f, -1.2f, speedY, totalDamage, rig.heavy))
                bullets.add(Bullet(playerX + 6f, playerY - 10f, 1.2f, speedY, totalDamage, rig.heavy))
            }
            3 -> {
                bullets.add(Bullet(playerX, playerY - 14f, 0f, -8f, totalDamage * 2, true))
            }
            4 -> {
                bullets.add(Bullet(playerX, playerY - 14f, 0f, speedY, totalDamage, true))
                bullets.add(Bullet(playerX - 8f, playerY - 10f, 0f, speedY, totalDamage, true))
                bullets.add(Bullet(playerX + 8f, playerY - 10f, 0f, speedY, totalDamage, true))
            }
            5 -> {
                bullets.add(Bullet(playerX - 6f, playerY - 12f, -0.6f, -7f, totalDamage + 1, true))
                bullets.add(Bullet(playerX + 6f, playerY - 12f, 0.6f, -7f, totalDamage + 1, true))
            }
            6 -> {
                bullets.add(Bullet(playerX, playerY - 14f, 0f, -8f, totalDamage, true))
                bullets.add(Bullet(playerX - 5f, playerY - 12f, 0f, -7.5f, totalDamage, true))
                bullets.add(Bullet(playerX + 5f, playerY - 12f, 0f, -7.5f, totalDamage, true))
            }
            7 -> {
                // Nova Annihilator: 5 god bolts!
                bullets.add(Bullet(playerX, playerY - 15f, 0f, -8.5f, totalDamage * 2, true))
                bullets.add(Bullet(playerX - 7f, playerY - 12f, -0.8f, -8f, totalDamage, true))
                bullets.add(Bullet(playerX + 7f, playerY - 12f, 0.8f, -8f, totalDamage, true))
                bullets.add(Bullet(playerX - 14f, playerY - 10f, -1.6f, -7.5f, totalDamage, true))
                bullets.add(Bullet(playerX + 14f, playerY - 10f, 1.6f, -7.5f, totalDamage, true))
            }
        }
        audioManager.playSfx(Sfx.LASER, 0.6f)
    }

    private fun handleAsteroidDestroyed(a: Asteroid, newAsteroids: MutableList<Asteroid>) {
        explosions.add(Explosion(a.x, a.y))
        audioManager.playSfx(Sfx.EXPLOSION)

        val pts = when (a.type) {
            AstType.LARGE -> 50
            AstType.MED_A, AstType.MED_B -> 100
            AstType.SMALL -> 150
            AstType.TINY -> 250
        }
        addScore(pts * combo)
        awardCoins(10)
        increaseCombo()

        // Split asteroid
        when (a.type) {
            AstType.LARGE -> {
                newAsteroids.add(Asteroid(a.x - 6f, a.y, -1.2f, a.vy * 1.1f, AstType.MED_A, 5, 5))
                newAsteroids.add(Asteroid(a.x + 6f, a.y, 1.2f, a.vy * 1.1f, AstType.MED_B, 5, 5))
            }
            AstType.MED_A, AstType.MED_B -> {
                newAsteroids.add(Asteroid(a.x - 4f, a.y, -1.4f, a.vy * 1.2f, AstType.SMALL, 3, 3))
                newAsteroids.add(Asteroid(a.x + 4f, a.y, 1.4f, a.vy * 1.2f, AstType.SMALL, 3, 3))
            }
            AstType.SMALL -> {
                newAsteroids.add(Asteroid(a.x - 3f, a.y, -1.5f, a.vy * 1.3f, AstType.TINY, 1, 1))
                newAsteroids.add(Asteroid(a.x + 3f, a.y, 1.5f, a.vy * 1.3f, AstType.TINY, 1, 1))
            }
            AstType.TINY -> {}
        }

        // Random powerup drop (8% chance)
        if (Random.nextFloat() < 0.08f) {
            val type = PowerupType.values().random()
            powerups.add(Powerup(a.x, a.y, 1f, type))
        }
    }

    private fun collectPowerup(type: PowerupType) {
        audioManager.playSfx(Sfx.PICKUP)
        audioManager.triggerHaptic(20L)
        when (type) {
            PowerupType.SHIELD -> {
                val maxShieldsTable = intArrayOf(2, 3, 4, 5, 6, 6)
                val maxS = maxShieldsTable[saveManager.getUpgradeLevel(3).coerceIn(0, 5)]
                if (playerShields < maxS) {
                    playerShields++
                    damageTexts.add(DamageText(playerX, playerY - 16f, "+SHIELD", Color.CYAN))
                }
            }
            PowerupType.RAPID -> {
                val durTable = intArrayOf(480, 660, 840, 1080, 1320, 1560)
                rapidFireTimer = durTable[saveManager.getUpgradeLevel(7).coerceIn(0, 5)]
                damageTexts.add(DamageText(playerX, playerY - 16f, "RAPID FIRE!", Color.YELLOW))
            }
            PowerupType.REPAIR -> {
                val hullLivesTable = intArrayOf(2, 3, 4, 5, 6, 7)
                val maxL = hullLivesTable[saveManager.getUpgradeLevel(4).coerceIn(0, 5)]
                if (playerLives < maxL) {
                    playerLives++
                    damageTexts.add(DamageText(playerX, playerY - 16f, "+1 LIFE", Color.GREEN))
                }
            }
        }
    }

    private fun damagePlayer(dmg: Int) {
        audioManager.triggerHaptic(40L)
        if (playerShields > 0) {
            playerShields--
            invulnerableTimer = 45
            damageTexts.add(DamageText(playerX, playerY - 16f, "SHIELD HIT", Color.CYAN))
            return
        }
        playerLives -= dmg
        invulnerableTimer = 90
        explosions.add(Explosion(playerX, playerY))
        audioManager.playSfx(Sfx.EXPLOSION)
        damageTexts.add(DamageText(playerX, playerY - 16f, "-$dmg LIFE", Color.RED))

        if (playerLives <= 0) {
            playerLives = 0
            currentScreen = Screen.GAME_OVER
            saveManager.updateHighScore(score)
            audioManager.playMusic(Bgm.MENU)
        }
    }

    private fun addScore(pts: Int) {
        score += pts
    }

    private fun awardCoins(baseAmt: Int) {
        val upgScav = saveManager.getUpgradeLevel(6)
        val multTable = floatArrayOf(1.0f, 1.35f, 1.70f, 2.05f, 2.40f, 2.75f)
        val amt = (baseAmt * multTable[upgScav.coerceIn(0, 5)]).toInt()
        saveManager.addCoins(amt)
        damageTexts.add(DamageText(playerX, playerY - 24f, "+${amt}c", Color.YELLOW))
    }

    private fun increaseCombo() {
        if (combo < 20) combo++
        comboTimer = 180 // 1.5 seconds combo window
    }

    private fun getAsteroidRadius(t: AstType): Float = when (t) {
        AstType.LARGE -> 12f
        AstType.MED_A, AstType.MED_B -> 8f
        AstType.SMALL -> 5f
        AstType.TINY -> 3f
    }

    // ── Touch Handling for Menus ──────────────────────────────────────────
    fun handleMenuTouch(event: MotionEvent, scaleX: Float, scaleY: Float): Boolean {
        val action = event.actionMasked
        if (action != MotionEvent.ACTION_DOWN) return false
        val tx = event.x / scaleX
        val ty = event.y / scaleY

        when (currentScreen) {
            Screen.MAIN_MENU -> {
                // Check 5 menu buttons
                val btnWidth = 160f
                val btnHeight = 22f
                val startY = 60f
                val gap = 28f
                val cx = VIRTUAL_WIDTH / 2f

                for (i in 0 until 5) {
                    val rect = RectF(cx - btnWidth / 2f, startY + i * gap, cx + btnWidth / 2f, startY + i * gap + btnHeight)
                    if (rect.contains(tx, ty)) {
                        audioManager.playSfx(Sfx.PICKUP)
                        when (i) {
                            0 -> startNewGame()
                            1 -> currentScreen = Screen.HANGAR
                            2 -> currentScreen = Screen.UPGRADES
                            3 -> currentScreen = Screen.CONTROLS
                            4 -> currentScreen = Screen.CREDITS
                        }
                        return true
                    }
                }
            }
            Screen.HANGAR -> {
                // 4 Category tabs at top
                val tabW = 86f
                val tabH = 20f
                for (cat in 0 until 4) {
                    val rect = RectF(8f + cat * (tabW + 4f), 10f, 8f + cat * (tabW + 4f) + tabW, 10f + tabH)
                    if (rect.contains(tx, ty)) {
                        shopCategory = cat
                        audioManager.playSfx(Sfx.PICKUP)
                        return true
                    }
                }

                // Scrolling item list on left (items in category)
                val catCount = when (shopCategory) {
                    0 -> NUM_ACCENTS
                    1 -> NUM_TRAILS
                    2 -> NUM_RIGS
                    3 -> NUM_LASERS
                    else -> 1
                }
                val listStartY = 38f
                val itemH = 22f
                val listW = 190f
                for (idx in 0 until catCount) {
                    val rect = RectF(8f, listStartY + idx * (itemH + 4f), 8f + listW, listStartY + idx * (itemH + 4f) + itemH)
                    if (rect.contains(tx, ty)) {
                        shopSelected[shopCategory] = idx
                        audioManager.playSfx(Sfx.PICKUP)
                        return true
                    }
                }

                // EQUIP / PURCHASE Button on bottom right
                val actionRect = RectF(210f, 168f, 370f, 196f)
                if (actionRect.contains(tx, ty)) {
                    val sel = shopSelected[shopCategory]
                    var ok = false
                    when (shopCategory) {
                        0 -> {
                            if (saveManager.isAccentOwned(sel)) {
                                saveManager.equipAccent(sel)
                                setShopMsg("EQUIPPED PAINT!", Color.GREEN)
                            } else {
                                ok = saveManager.tryPurchaseAccent(sel, SHIP_PAINTS[sel].price)
                                setShopMsg(if (ok) "PURCHASED PAINT!" else "NEED MORE COINS!", if (ok) Color.GREEN else Color.RED)
                            }
                        }
                        1 -> {
                            if (saveManager.isTrailOwned(sel)) {
                                saveManager.equipTrail(sel)
                                setShopMsg("EQUIPPED TRAIL!", Color.GREEN)
                            } else {
                                ok = saveManager.tryPurchaseTrail(sel, ENGINE_TRAILS[sel].price)
                                setShopMsg(if (ok) "PURCHASED TRAIL!" else "NEED MORE COINS!", if (ok) Color.GREEN else Color.RED)
                            }
                        }
                        2 -> {
                            if (saveManager.isRigOwned(sel)) {
                                saveManager.equipRig(sel)
                                setShopMsg("EQUIPPED WEAPON!", Color.GREEN)
                            } else {
                                ok = saveManager.tryPurchaseRig(sel, WEAPON_RIGS[sel].price)
                                setShopMsg(if (ok) "PURCHASED WEAPON!" else "NEED MORE COINS!", if (ok) Color.GREEN else Color.RED)
                            }
                        }
                        3 -> {
                            if (saveManager.isLaserOwned(sel)) {
                                saveManager.equipLaser(sel)
                                setShopMsg("EQUIPPED LASER!", Color.GREEN)
                            } else {
                                ok = saveManager.tryPurchaseLaser(sel, LASER_CRYSTALS[sel].price)
                                setShopMsg(if (ok) "PURCHASED LASER!" else "NEED MORE COINS!", if (ok) Color.GREEN else Color.RED)
                            }
                        }
                    }
                    if (ok) audioManager.playSfx(Sfx.PICKUP)
                    return true
                }

                // BACK Button on bottom left
                val backRect = RectF(8f, 185f, 100f, 206f)
                if (backRect.contains(tx, ty)) {
                    audioManager.playSfx(Sfx.PICKUP)
                    currentScreen = Screen.MAIN_MENU
                    return true
                }
            }
            Screen.UPGRADES -> {
                // Check 8 Upgrade cards (2 columns x 4 rows)
                val cardW = 175f
                val cardH = 38f
                for (i in 0 until NUM_UPGRADES) {
                    val col = i % 2
                    val row = i / 2
                    val rect = RectF(10f + col * (cardW + 10f), 30f + row * (cardH + 6f), 10f + col * (cardW + 10f) + cardW, 30f + row * (cardH + 6f) + cardH)
                    if (rect.contains(tx, ty)) {
                        upgSelected = i
                        audioManager.playSfx(Sfx.PICKUP)
                        // Trigger level up purchase on card tap
                        val upg = TECH_UPGRADES[i]
                        val curLv = saveManager.getUpgradeLevel(i)
                        if (curLv < UPG_MAX_LEVEL) {
                            val ok = saveManager.purchaseUpgrade(i, upg.prices[curLv])
                            if (ok) {
                                setShopMsg("UPGRADED ${upg.name}!", Color.GREEN)
                                audioManager.playSfx(Sfx.PICKUP)
                            } else {
                                setShopMsg("NEED ${formatPrice(upg.prices[curLv])} COINS!", Color.RED)
                            }
                        } else {
                            setShopMsg("MAX LEVEL REACHED!", Color.YELLOW)
                        }
                        return true
                    }
                }

                // BACK Button
                val backRect = RectF(8f, 185f, 100f, 206f)
                if (backRect.contains(tx, ty)) {
                    audioManager.playSfx(Sfx.PICKUP)
                    currentScreen = Screen.MAIN_MENU
                    return true
                }
            }
            Screen.CONTROLS -> {
                // Toggle joystick mode button
                val toggleRect = RectF(120f, 130f, 264f, 160f)
                if (toggleRect.contains(tx, ty)) {
                    val nextMode = if (saveManager.joystickMode == "dynamic") "fixed" else "dynamic"
                    saveManager.joystickMode = nextMode
                    saveManager.save()
                    audioManager.playSfx(Sfx.PICKUP)
                    return true
                }
                // BACK Button
                val backRect = RectF(8f, 185f, 100f, 206f)
                if (backRect.contains(tx, ty)) {
                    audioManager.playSfx(Sfx.PICKUP)
                    currentScreen = Screen.MAIN_MENU
                    return true
                }
            }
            Screen.CREDITS -> {
                // BACK Button
                val backRect = RectF(8f, 185f, 100f, 206f)
                if (backRect.contains(tx, ty)) {
                    audioManager.playSfx(Sfx.PICKUP)
                    currentScreen = Screen.MAIN_MENU
                    return true
                }
            }
            Screen.PAUSED -> {
                val btnW = 160f
                val btnH = 26f
                val startY = 70f
                val gap = 32f
                val cx = VIRTUAL_WIDTH / 2f

                for (i in 0 until 3) {
                    val rect = RectF(cx - btnW / 2f, startY + i * gap, cx + btnW / 2f, startY + i * gap + btnH)
                    if (rect.contains(tx, ty)) {
                        audioManager.playSfx(Sfx.PICKUP)
                        when (i) {
                            0 -> currentScreen = Screen.PLAYING // RESUME
                            1 -> { // HANGAR
                                currentScreen = Screen.HANGAR
                                audioManager.playMusic(Bgm.MENU)
                            }
                            2 -> { // QUIT TO MENU
                                currentScreen = Screen.MAIN_MENU
                                audioManager.playMusic(Bgm.MENU)
                            }
                        }
                        return true
                    }
                }
            }
            Screen.GAME_OVER -> {
                val btnW = 160f
                val btnH = 26f
                val startY = 85f
                val gap = 32f
                val cx = VIRTUAL_WIDTH / 2f

                for (i in 0 until 3) {
                    val rect = RectF(cx - btnW / 2f, startY + i * gap, cx + btnW / 2f, startY + i * gap + btnH)
                    if (rect.contains(tx, ty)) {
                        audioManager.playSfx(Sfx.PICKUP)
                        when (i) {
                            0 -> startNewGame() // PLAY AGAIN
                            1 -> currentScreen = Screen.HANGAR
                            2 -> currentScreen = Screen.MAIN_MENU
                        }
                        return true
                    }
                }
            }
            Screen.PLAYING -> return false
        }
        return false
    }

    private fun setShopMsg(msg: String, color: Int) {
        shopMessage = msg
        shopMessageColor = color
        shopMessageTimer = 80
    }

    // ── Rendering Engine ──────────────────────────────────────────────────
    fun draw(canvas: Canvas) {
        // Draw Starfield Background
        starfield.draw(canvas)

        when (currentScreen) {
            Screen.MAIN_MENU -> drawMainMenu(canvas)
            Screen.HANGAR -> drawHangar(canvas)
            Screen.UPGRADES -> drawUpgrades(canvas)
            Screen.CONTROLS -> drawControlsScreen(canvas)
            Screen.CREDITS -> drawCreditsScreen(canvas)
            Screen.PLAYING -> drawGameplay(canvas)
            Screen.PAUSED -> {
                drawGameplay(canvas)
                drawPausedModal(canvas)
            }
            Screen.GAME_OVER -> {
                drawGameplay(canvas)
                drawGameOverModal(canvas)
            }
        }
    }

    private fun drawMainMenu(canvas: Canvas) {
        // Title Banner
        GfxData.drawText(canvas, "SPACE UNLIMITED", VIRTUAL_WIDTH / 2f, 24f, Color.YELLOW, 2f, Paint.Align.CENTER)
        GfxData.drawText(canvas, "RECHARGED - ANDROID EDITION", VIRTUAL_WIDTH / 2f, 44f, Color.CYAN, 1f, Paint.Align.CENTER)

        // Menu Buttons
        val btnWidth = 160f
        val btnHeight = 22f
        val startY = 60f
        val gap = 28f
        val cx = VIRTUAL_WIDTH / 2f
        val titles = arrayOf("START MISSION", "HANGAR / SHOP", "TECH UPGRADES", "CONTROLS", "CREDITS")

        for (i in 0 until 5) {
            val rect = RectF(cx - btnWidth / 2f, startY + i * gap, cx + btnWidth / 2f, startY + i * gap + btnHeight)
            canvas.drawRect(rect, cardPaint)
            canvas.drawRect(rect, cardBorderPaint)
            GfxData.drawText(canvas, titles[i], cx, startY + i * gap + 7f, Color.WHITE, 1f, Paint.Align.CENTER)
        }

        // Stats Footer
        GfxData.drawText(canvas, "COINS: ${formatPrice(saveManager.coins)}", 12f, VIRTUAL_HEIGHT - 16f, Color.YELLOW, 1f)
        GfxData.drawText(canvas, "HI: ${saveManager.highScore}", VIRTUAL_WIDTH - 12f, VIRTUAL_HEIGHT - 16f, Color.CYAN, 1f, Paint.Align.RIGHT)
    }

    private fun drawHangar(canvas: Canvas) {
        // Top tabs
        val tabW = 86f
        val tabH = 20f
        val tabNames = arrayOf("PAINTS", "TRAILS", "WEAPONS", "LASERS")
        for (cat in 0 until 4) {
            val rect = RectF(8f + cat * (tabW + 4f), 10f, 8f + cat * (tabW + 4f) + tabW, 10f + tabH)
            canvas.drawRect(rect, cardPaint)
            canvas.drawRect(rect, if (cat == shopCategory) highlightBorderPaint else cardBorderPaint)
            GfxData.drawText(canvas, tabNames[cat], rect.centerX(), rect.centerY() - 3f, if (cat == shopCategory) Color.YELLOW else Color.WHITE, 1f, Paint.Align.CENTER)
        }

        // Category list
        val listStartY = 38f
        val itemH = 22f
        val listW = 190f
        val catCount = when (shopCategory) {
            0 -> NUM_ACCENTS
            1 -> NUM_TRAILS
            2 -> NUM_RIGS
            3 -> NUM_LASERS
            else -> 0
        }
        val selIdx = shopSelected[shopCategory]
        for (i in 0 until catCount) {
            val rect = RectF(8f, listStartY + i * (itemH + 4f), 8f + listW, listStartY + i * (itemH + 4f) + itemH)
            if (rect.bottom > 180f) break
            canvas.drawRect(rect, cardPaint)
            canvas.drawRect(rect, if (i == selIdx) highlightBorderPaint else cardBorderPaint)

            val name: String
            val badge: String
            when (shopCategory) {
                0 -> {
                    val item = SHIP_PAINTS[i]
                    name = item.name
                    badge = if (saveManager.accentIndex == i) "[EQ]" else if (saveManager.isAccentOwned(i)) "OWN" else formatPrice(item.price)
                }
                1 -> {
                    val item = ENGINE_TRAILS[i]
                    name = item.name
                    badge = if (saveManager.trailIndex == i) "[EQ]" else if (saveManager.isTrailOwned(i)) "OWN" else formatPrice(item.price)
                }
                2 -> {
                    val item = WEAPON_RIGS[i]
                    name = item.name
                    badge = if (saveManager.weaponRig == i) "[EQ]" else if (saveManager.isRigOwned(i)) "OWN" else formatPrice(item.price)
                }
                else -> {
                    val item = LASER_CRYSTALS[i]
                    name = item.name
                    badge = if (saveManager.laserIndex == i) "[EQ]" else if (saveManager.isLaserOwned(i)) "OWN" else formatPrice(item.price)
                }
            }
            GfxData.drawText(canvas, name, 14f, rect.centerY() - 3f, Color.WHITE, 1f)
            GfxData.drawText(canvas, badge, rect.right - 8f, rect.centerY() - 3f, Color.YELLOW, 1f, Paint.Align.RIGHT)
        }

        // Right Preview & Detail Panel
        val panelRect = RectF(206f, 38f, 376f, 160f)
        canvas.drawRect(panelRect, cardPaint)
        canvas.drawRect(panelRect, cardBorderPaint)

        // Live Ship Preview in center of panel
        val prevX = panelRect.centerX()
        val prevY = panelRect.top + 35f
        val paintIdx = if (shopCategory == 0) selIdx else saveManager.accentIndex
        val trailIdx = if (shopCategory == 1) selIdx else saveManager.trailIndex
        val laserIdx = if (shopCategory == 3) selIdx else saveManager.laserIndex

        spriteRenderer.drawTrail(canvas, prevX, prevY, trailIdx, gameFrame)
        spriteRenderer.drawShip(canvas, prevX, prevY, paintIdx, gameFrame)
        spriteRenderer.drawLaser(canvas, prevX, prevY - 18f, laserIdx, true, gameFrame)

        // Item description
        val desc = when (shopCategory) {
            0 -> SHIP_PAINTS[selIdx].desc
            1 -> ENGINE_TRAILS[selIdx].desc
            2 -> WEAPON_RIGS[selIdx].desc
            else -> LASER_CRYSTALS[selIdx].desc
        }
        GfxData.drawText(canvas, desc, panelRect.left + 8f, prevY + 30f, Color.CYAN, 0.9f)

        // EQUIP / BUY Button
        val actionRect = RectF(210f, 168f, 370f, 196f)
        canvas.drawRect(actionRect, cardPaint)
        canvas.drawRect(actionRect, highlightBorderPaint)

        val btnText = when (shopCategory) {
            0 -> if (saveManager.accentIndex == selIdx) "EQUIPPED" else if (saveManager.isAccentOwned(selIdx)) "EQUIP" else "BUY ${formatPrice(SHIP_PAINTS[selIdx].price)}"
            1 -> if (saveManager.trailIndex == selIdx) "EQUIPPED" else if (saveManager.isTrailOwned(selIdx)) "EQUIP" else "BUY ${formatPrice(ENGINE_TRAILS[selIdx].price)}"
            2 -> if (saveManager.weaponRig == selIdx) "EQUIPPED" else if (saveManager.isRigOwned(selIdx)) "EQUIP" else "BUY ${formatPrice(WEAPON_RIGS[selIdx].price)}"
            else -> if (saveManager.laserIndex == selIdx) "EQUIPPED" else if (saveManager.isLaserOwned(selIdx)) "EQUIP" else "BUY ${formatPrice(LASER_CRYSTALS[selIdx].price)}"
        }
        GfxData.drawText(canvas, btnText, actionRect.centerX(), actionRect.centerY() - 3f, Color.YELLOW, 1f, Paint.Align.CENTER)

        // Status message
        if (shopMessageTimer > 0) {
            GfxData.drawText(canvas, shopMessage, VIRTUAL_WIDTH / 2f, 158f, shopMessageColor, 1f, Paint.Align.CENTER)
        }

        // Back button
        val backRect = RectF(8f, 185f, 100f, 206f)
        canvas.drawRect(backRect, cardPaint)
        canvas.drawRect(backRect, cardBorderPaint)
        GfxData.drawText(canvas, "< MENU", backRect.centerX(), backRect.centerY() - 3f, Color.WHITE, 1f, Paint.Align.CENTER)

        // Coins display
        GfxData.drawText(canvas, "COINS: ${formatPrice(saveManager.coins)}", VIRTUAL_WIDTH - 12f, 200f, Color.YELLOW, 1f, Paint.Align.RIGHT)
    }

    private fun drawUpgrades(canvas: Canvas) {
        GfxData.drawText(canvas, "TECH TREE UPGRADES (TAP TO LEVEL UP)", VIRTUAL_WIDTH / 2f, 16f, Color.YELLOW, 1f, Paint.Align.CENTER)

        val cardW = 175f
        val cardH = 38f
        for (i in 0 until NUM_UPGRADES) {
            val col = i % 2
            val row = i / 2
            val rect = RectF(10f + col * (cardW + 10f), 30f + row * (cardH + 6f), 10f + col * (cardW + 10f) + cardW, 30f + row * (cardH + 6f) + cardH)
            canvas.drawRect(rect, cardPaint)
            canvas.drawRect(rect, if (i == upgSelected) highlightBorderPaint else cardBorderPaint)

            val upg = TECH_UPGRADES[i]
            val lv = saveManager.getUpgradeLevel(i)
            val lvStr = if (lv >= UPG_MAX_LEVEL) "MAX" else "LV $lv/$UPG_MAX_LEVEL"

            GfxData.drawText(canvas, "${upg.name} [$lvStr]", rect.left + 8f, rect.top + 12f, Color.YELLOW, 1f)
            val desc = upg.levelDescs[lv.coerceIn(0, UPG_MAX_LEVEL)]
            GfxData.drawText(canvas, desc, rect.left + 8f, rect.top + 26f, Color.WHITE, 0.85f)
        }

        if (shopMessageTimer > 0) {
            GfxData.drawText(canvas, shopMessage, VIRTUAL_WIDTH / 2f, 180f, shopMessageColor, 1f, Paint.Align.CENTER)
        }

        // Back button
        val backRect = RectF(8f, 185f, 100f, 206f)
        canvas.drawRect(backRect, cardPaint)
        canvas.drawRect(backRect, cardBorderPaint)
        GfxData.drawText(canvas, "< MENU", backRect.centerX(), backRect.centerY() - 3f, Color.WHITE, 1f, Paint.Align.CENTER)

        GfxData.drawText(canvas, "COINS: ${formatPrice(saveManager.coins)}", VIRTUAL_WIDTH - 12f, 200f, Color.YELLOW, 1f, Paint.Align.RIGHT)
    }

    private fun drawControlsScreen(canvas: Canvas) {
        GfxData.drawText(canvas, "TOUCH & JOYSTICK CONTROLS", VIRTUAL_WIDTH / 2f, 24f, Color.YELLOW, 1.5f, Paint.Align.CENTER)
        val info = arrayOf(
            "- VIRTUAL JOYSTICK: Touch left screen to steer 360 degrees",
            "- ACTION BUTTONS: FIRE, DASH (afterburner evasion), PAUSE",
            "- DIRECT TOUCH NAV: Tap menu tabs, cards & upgrade items",
            "- 120HZ / 90HZ SUPPORT: Full native refresh rate physics loop",
            "- TRUE WIDESCREEN: No status bar or navigation bar borders!"
        )
        for (i in info.indices) {
            GfxData.drawText(canvas, info[i], 30f, 60f + i * 20f, Color.WHITE, 1f)
        }

        // Toggle Joystick Mode button
        val toggleRect = RectF(120f, 166f, 264f, 190f)
        canvas.drawRect(toggleRect, cardPaint)
        canvas.drawRect(toggleRect, highlightBorderPaint)
        GfxData.drawText(canvas, "JOYSTICK: ${saveManager.joystickMode.uppercase()}", toggleRect.centerX(), toggleRect.centerY() - 3f, Color.YELLOW, 1f, Paint.Align.CENTER)

        // Back button
        val backRect = RectF(8f, 185f, 100f, 206f)
        canvas.drawRect(backRect, cardPaint)
        canvas.drawRect(backRect, cardBorderPaint)
        GfxData.drawText(canvas, "< MENU", backRect.centerX(), backRect.centerY() - 3f, Color.WHITE, 1f, Paint.Align.CENTER)
    }

    private fun drawCreditsScreen(canvas: Canvas) {
        GfxData.drawText(canvas, "SPACE UNLIMITED - CREDITS", VIRTUAL_WIDTH / 2f, 28f, Color.YELLOW, 1.5f, Paint.Align.CENTER)
        val credits = arrayOf(
            "ORIGINAL GAME BOY ADVANCE (GBA) EDITION",
            "EXACT 1-1 NATIVE ANDROID KOTLIN REMAKE",
            "HIGH-REFRESH 90HZ / 120HZ VARIABLE TIMESTEP LOOP",
            "TOUCHSCREEN VIRTUAL ANALOG JOYSTICK & HUD",
            "THANKS FOR PLAYING SPACE UNLIMITED!"
        )
        for (i in credits.indices) {
            GfxData.drawText(canvas, credits[i], VIRTUAL_WIDTH / 2f, 65f + i * 22f, Color.CYAN, 1f, Paint.Align.CENTER)
        }

        val backRect = RectF(8f, 185f, 100f, 206f)
        canvas.drawRect(backRect, cardPaint)
        canvas.drawRect(backRect, cardBorderPaint)
        GfxData.drawText(canvas, "< MENU", backRect.centerX(), backRect.centerY() - 3f, Color.WHITE, 1f, Paint.Align.CENTER)
    }

    private fun drawGameplay(canvas: Canvas) {
        // Draw Asteroids
        for (a in asteroids) {
            spriteRenderer.drawAsteroid(canvas, a.x, a.y, a.type)
        }

        // Draw Drones
        for (d in drones) {
            spriteRenderer.drawDrone(canvas, d.x, d.y)
        }

        // Draw Powerups
        for (p in powerups) {
            spriteRenderer.drawPowerup(canvas, p.x, p.y, p.type)
        }

        // Draw Bullets
        for (b in bullets) {
            spriteRenderer.drawLaser(canvas, b.x, b.y, saveManager.laserIndex, b.heavy, gameFrame, b.enemy)
        }

        // Draw Player Ship & Trail
        spriteRenderer.drawTrail(canvas, playerX, playerY, saveManager.trailIndex, gameFrame)
        spriteRenderer.drawShip(canvas, playerX, playerY, saveManager.accentIndex, gameFrame, invulnerableTimer > 0)

        // Draw Player Shield Bubble if shields active
        if (playerShields > 0) {
            spriteRenderer.drawShield(canvas, playerX, playerY)
        }

        // Draw Explosions
        for (e in explosions) {
            spriteRenderer.drawExplosion(canvas, e.x, e.y, e.frame)
        }

        // Draw Floating Damage Texts
        for (t in damageTexts) {
            GfxData.drawText(canvas, t.text, t.x, t.y, t.color, 1f, Paint.Align.CENTER)
        }

        // --- HUD Overlay ---
        drawHud(canvas)

        // --- Virtual Joystick & Touch Controls ---
        touchControls.draw(canvas)
    }

    private fun drawHud(canvas: Canvas) {
        // Top HUD line: Score | Wave | Coins | Multiplier
        GfxData.drawText(canvas, "SCORE: $score", 12f, 16f, Color.WHITE, 1f)
        GfxData.drawText(canvas, "WAVE $wave", VIRTUAL_WIDTH / 2f, 16f, Color.YELLOW, 1f, Paint.Align.CENTER)
        GfxData.drawText(canvas, "COINS: ${formatPrice(saveManager.coins)}", VIRTUAL_WIDTH - 60f, 16f, Color.YELLOW, 1f, Paint.Align.RIGHT)

        // Lives (Hearts)
        var hx = 12f
        for (i in 0 until playerLives) {
            GfxData.drawText(canvas, "<3", hx, 30f, Color.RED, 1f)
            hx += 18f
        }

        // Shields (Blue Squares)
        var sx = 12f
        for (i in 0 until playerShields) {
            GfxData.drawText(canvas, "[]", sx, 44f, Color.CYAN, 1f)
            sx += 14f
        }

        // Combo Multiplier
        if (combo > 1) {
            GfxData.drawText(canvas, "COMBO x$combo", VIRTUAL_WIDTH - 60f, 30f, Color.CYAN, 1f, Paint.Align.RIGHT)
        }

        // Wave Banner
        if (waveBannerTimer > 0) {
            GfxData.drawText(canvas, "WAVE $wave START", VIRTUAL_WIDTH / 2f, 80f, Color.YELLOW, 1.5f, Paint.Align.CENTER)
        }

        // FPS Display
        if (saveManager.fpsDisplay) {
            GfxData.drawText(canvas, "${displayFps}FPS", VIRTUAL_WIDTH - 4f, VIRTUAL_HEIGHT - 6f, Color.argb(180, 255, 255, 255), 0.8f, Paint.Align.RIGHT)
        }
    }

    private fun drawPausedModal(canvas: Canvas) {
        canvas.drawRect(0f, 0f, VIRTUAL_WIDTH, VIRTUAL_HEIGHT, bgOverlayPaint)
        GfxData.drawText(canvas, "GAME PAUSED", VIRTUAL_WIDTH / 2f, 40f, Color.YELLOW, 2f, Paint.Align.CENTER)

        val btnW = 160f
        val btnH = 26f
        val startY = 70f
        val gap = 32f
        val cx = VIRTUAL_WIDTH / 2f
        val titles = arrayOf("RESUME GAME", "HANGAR / SHOP", "QUIT TO MENU")

        for (i in 0 until 3) {
            val rect = RectF(cx - btnW / 2f, startY + i * gap, cx + btnW / 2f, startY + i * gap + btnH)
            canvas.drawRect(rect, cardPaint)
            canvas.drawRect(rect, cardBorderPaint)
            GfxData.drawText(canvas, titles[i], cx, startY + i * gap + 9f, Color.WHITE, 1f, Paint.Align.CENTER)
        }
    }

    private fun drawGameOverModal(canvas: Canvas) {
        canvas.drawRect(0f, 0f, VIRTUAL_WIDTH, VIRTUAL_HEIGHT, bgOverlayPaint)
        GfxData.drawText(canvas, "MISSION FAILED", VIRTUAL_WIDTH / 2f, 35f, Color.RED, 2f, Paint.Align.CENTER)
        GfxData.drawText(canvas, "FINAL SCORE: $score", VIRTUAL_WIDTH / 2f, 58f, Color.WHITE, 1f, Paint.Align.CENTER)

        val btnW = 160f
        val btnH = 26f
        val startY = 85f
        val gap = 32f
        val cx = VIRTUAL_WIDTH / 2f
        val titles = arrayOf("PLAY AGAIN", "HANGAR / SHOP", "MAIN MENU")

        for (i in 0 until 3) {
            val rect = RectF(cx - btnW / 2f, startY + i * gap, cx + btnW / 2f, startY + i * gap + btnH)
            canvas.drawRect(rect, cardPaint)
            canvas.drawRect(rect, cardBorderPaint)
            GfxData.drawText(canvas, titles[i], cx, startY + i * gap + 9f, Color.WHITE, 1f, Paint.Align.CENTER)
        }
    }

    fun release() {
        audioManager.release()
    }
}

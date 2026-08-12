package com.brick.spaceshooter

import android.content.Context
import android.content.SharedPreferences

class SaveManager(private val context: Context) {

    private val prefs: SharedPreferences =
        context.getSharedPreferences("space_shooter_android_save_v1", Context.MODE_PRIVATE)

    var coins: Int = 0
        private set
    var highScore: Int = 0
        private set

    var difficulty: Difficulty = Difficulty.PILOT
    var musicVolume: Int = 80
    var sfxVolume: Int = 80
    var screenShake: Boolean = true
    var haptics: Boolean = true
    var fpsDisplay: Boolean = true
    var joystickMode: String = "dynamic" // "dynamic" or "fixed"

    var accentIndex: Int = 1
        private set
    var trailIndex: Int = 1
        private set
    var weaponRig: Int = 0
        private set
    var laserIndex: Int = 0
        private set

    private var ownedAccents: Int = (1 shl 1)
    private var ownedTrails: Int = (1 shl 1)
    private var ownedRigs: Int = (1 shl 0)
    private var ownedLasers: Int = (1 shl 0)

    private val upgradeLevels = IntArray(NUM_UPGRADES) { 0 }

    init {
        load()
    }

    fun load() {
        coins = prefs.getInt("coins", 0)
        highScore = prefs.getInt("highScore", 0)
        val diffId = prefs.getInt("difficulty", 1)
        difficulty = Difficulty.values().find { it.id == diffId } ?: Difficulty.PILOT
        musicVolume = prefs.getInt("musicVolume", 80).coerceIn(0, 100)
        sfxVolume = prefs.getInt("sfxVolume", 80).coerceIn(0, 100)
        screenShake = prefs.getBoolean("screenShake", true)
        haptics = prefs.getBoolean("haptics", true)
        fpsDisplay = prefs.getBoolean("fpsDisplay", true)
        joystickMode = prefs.getString("joystickMode", "dynamic") ?: "dynamic"

        accentIndex = prefs.getInt("accentIndex", 1).coerceIn(0, NUM_ACCENTS - 1)
        trailIndex = prefs.getInt("trailIndex", 1).coerceIn(0, NUM_TRAILS - 1)
        weaponRig = prefs.getInt("weaponRig", 0).coerceIn(0, NUM_RIGS - 1)
        laserIndex = prefs.getInt("laserIndex", 0).coerceIn(0, NUM_LASERS - 1)

        ownedAccents = prefs.getInt("ownedAccents", 1 shl 1) or (1 shl 1)
        ownedTrails = prefs.getInt("ownedTrails", 1 shl 1) or (1 shl 1)
        ownedRigs = prefs.getInt("ownedRigs", 1 shl 0) or (1 shl 0)
        ownedLasers = prefs.getInt("ownedLasers", 1 shl 0) or (1 shl 0)

        for (i in 0 until NUM_UPGRADES) {
            upgradeLevels[i] = prefs.getInt("upg_$i", 0).coerceIn(0, UPG_MAX_LEVEL)
        }
    }

    fun save() {
        prefs.edit().apply {
            putInt("coins", coins)
            putInt("highScore", highScore)
            putInt("difficulty", difficulty.id)
            putInt("musicVolume", musicVolume)
            putInt("sfxVolume", sfxVolume)
            putBoolean("screenShake", screenShake)
            putBoolean("haptics", haptics)
            putBoolean("fpsDisplay", fpsDisplay)
            putString("joystickMode", joystickMode)
            putInt("accentIndex", accentIndex)
            putInt("trailIndex", trailIndex)
            putInt("weaponRig", weaponRig)
            putInt("laserIndex", laserIndex)
            putInt("ownedAccents", ownedAccents)
            putInt("ownedTrails", ownedTrails)
            putInt("ownedRigs", ownedRigs)
            putInt("ownedLasers", ownedLasers)
            for (i in 0 until NUM_UPGRADES) {
                putInt("upg_$i", upgradeLevels[i])
            }
            apply()
        }
    }

    fun addCoins(amount: Int) {
        coins += amount
        save()
    }

    fun updateHighScore(newScore: Int): Boolean {
        if (newScore > highScore) {
            highScore = newScore
            save()
            return true
        }
        return false
    }

    // --- Upgrade Management ---
    fun getUpgradeLevel(idx: Int): Int {
        if (idx !in 0 until NUM_UPGRADES) return 0
        return upgradeLevels[idx]
    }

    fun purchaseUpgrade(idx: Int, price: Int): Boolean {
        if (idx !in 0 until NUM_UPGRADES) return false
        val current = upgradeLevels[idx]
        if (current >= UPG_MAX_LEVEL) return false
        if (coins >= price) {
            coins -= price
            upgradeLevels[idx] = current + 1
            save()
            return true
        }
        return false
    }

    // --- Paint (Accent) Management ---
    fun isAccentOwned(idx: Int): Boolean {
        return (ownedAccents and (1 shl idx)) != 0
    }

    fun tryPurchaseAccent(idx: Int, price: Int): Boolean {
        if (isAccentOwned(idx)) return true
        if (coins >= price) {
            coins -= price
            ownedAccents = ownedAccents or (1 shl idx)
            accentIndex = idx
            save()
            return true
        }
        return false
    }

    fun equipAccent(idx: Int) {
        if (isAccentOwned(idx)) {
            accentIndex = idx
            save()
        }
    }

    // --- Trail Management ---
    fun isTrailOwned(idx: Int): Boolean {
        return (ownedTrails and (1 shl idx)) != 0
    }

    fun tryPurchaseTrail(idx: Int, price: Int): Boolean {
        if (isTrailOwned(idx)) return true
        if (coins >= price) {
            coins -= price
            ownedTrails = ownedTrails or (1 shl idx)
            trailIndex = idx
            save()
            return true
        }
        return false
    }

    fun equipTrail(idx: Int) {
        if (isTrailOwned(idx)) {
            trailIndex = idx
            save()
        }
    }

    // --- Weapon Rig Management ---
    fun isRigOwned(idx: Int): Boolean {
        return (ownedRigs and (1 shl idx)) != 0
    }

    fun tryPurchaseRig(idx: Int, price: Int): Boolean {
        if (isRigOwned(idx)) return true
        if (coins >= price) {
            coins -= price
            ownedRigs = ownedRigs or (1 shl idx)
            weaponRig = idx
            save()
            return true
        }
        return false
    }

    fun equipRig(idx: Int) {
        if (isRigOwned(idx)) {
            weaponRig = idx
            save()
        }
    }

    // --- Laser Crystal Management ---
    fun isLaserOwned(idx: Int): Boolean {
        return (ownedLasers and (1 shl idx)) != 0
    }

    fun tryPurchaseLaser(idx: Int, price: Int): Boolean {
        if (isLaserOwned(idx)) return true
        if (coins >= price) {
            coins -= price
            ownedLasers = ownedLasers or (1 shl idx)
            laserIndex = idx
            save()
            return true
        }
        return false
    }

    fun equipLaser(idx: Int) {
        if (isLaserOwned(idx)) {
            laserIndex = idx
            save()
        }
    }
}

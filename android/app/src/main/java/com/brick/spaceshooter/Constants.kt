package com.brick.spaceshooter

import android.graphics.Color

const val VIRTUAL_WIDTH = 384f
const val VIRTUAL_HEIGHT = 216f

const val TARGET_PHYSICS_HZ = 120
const val FIXED_TIMESTEP = 1000f / TARGET_PHYSICS_HZ // 8.333 ms

const val NUM_ACCENTS = 9
const val NUM_TRAILS = 8
const val NUM_RIGS = 8
const val NUM_LASERS = 12
const val NUM_UPGRADES = 8
const val UPG_MAX_LEVEL = 5

enum class Screen {
    MAIN_MENU,
    HANGAR,
    UPGRADES,
    CONTROLS,
    CREDITS,
    PLAYING,
    PAUSED,
    GAME_OVER
}

enum class Difficulty(val id: Int, val title: String, val speedMult: Float, val lifeBonus: Int, val desc: String) {
    CADET(0, "Cadet", 0.86f, 1, "Casual flight, slower hostiles"),
    PILOT(1, "Pilot", 1.00f, 0, "Standard arcade combat"),
    ACE(2, "Ace", 1.21f, -1, "Tough, aggressive swarm")
}

data class WeaponRigInfo(
    val id: Int,
    val name: String,
    val price: Int,
    val baseCd: Int,
    val desc: String,
    val heavy: Boolean,
    val bolts: Int
)

val WEAPON_RIGS = listOf(
    WeaponRigInfo(0, "Single Blaster", 0, 30, "Starter solo shot - 1 rock at a time", false, 1),
    WeaponRigInfo(1, "Twin Cannons", 500, 26, "Dual balanced parallel cannons", false, 2),
    WeaponRigInfo(2, "Spread Cannon", 2500, 28, "3-way broad angle salvo", false, 3),
    WeaponRigInfo(3, "Focused Beam", 6500, 22, "Heavy piercing concentrated beam", true, 1),
    WeaponRigInfo(4, "Triple Blaster", 14000, 20, "Tri-barrel heavy assault barrage", true, 3),
    WeaponRigInfo(5, "Plasma Flak", 30000, 18, "Twin heavy explosive plasma flak", true, 2),
    WeaponRigInfo(6, "Quantum Core", 65000, 14, "Rapid high-energy drill array", true, 3),
    WeaponRigInfo(7, "Nova Annihilator", 150000, 10, "5x piercing devastation god weapon", true, 5)
)

data class LaserCrystalInfo(
    val id: Int,
    val name: String,
    val price: Int,
    val bonusDmg: Int,
    val colorIdx: Int,
    val hex: String,
    val desc: String,
    val special: String
)

val LASER_CRYSTALS = listOf(
    LaserCrystalInfo(0, "Ion Basic", 0, 0, 21, "#23d6ff", "Starter weak crystal +0 dmg", "basic"),
    LaserCrystalInfo(1, "Solar Gold", 1200, 1, 24, "#ffd24a", "Amber photon focus +1 dmg", "none"),
    LaserCrystalInfo(2, "Nebula Violet", 3500, 1, 28, "#b46eff", "Purple ion bolt +1 dmg", "none"),
    LaserCrystalInfo(3, "Toxic Mint", 7500, 2, 27, "#50f08c", "Toxic bio-plasma +2 dmg", "none"),
    LaserCrystalInfo(4, "Crimson Fury", 15000, 2, 26, "#ff3c3c", "Thermal overload +2 dmg", "none"),
    LaserCrystalInfo(5, "Emerald Surge", 30000, 3, 62, "#66ffb8", "High-frequency gamma +3 dmg", "none"),
    LaserCrystalInfo(6, "Void Shadow", 60000, 3, 116, "#af5ff5", "Dark matter distortion +3 dmg", "none"),
    LaserCrystalInfo(7, "Rainbow Laser", 100000, 3, 120, "#ff78be", "Chromatic wave + piercing beam", "rainbow"),
    LaserCrystalInfo(8, "Inferno Red", 40000, 2, 70, "#ff4646", "Incendiary scorch +2 dmg", "none"),
    LaserCrystalInfo(9, "Frost Blue", 70000, 3, 54, "#2ad6ff", "Sub-zero cryo shock +3 dmg", "none"),
    LaserCrystalInfo(10, "Photon Gold", 110000, 4, 66, "#ffd24a", "Concentrated solar blast +4 dmg", "none"),
    LaserCrystalInfo(11, "Omega Prism", 250000, 5, 78, "#ff5ae6", "GOD! Heavy pierce +5 dmg & cd boost", "omega")
)

data class ShipPaintInfo(
    val id: Int,
    val name: String,
    val price: Int,
    val hex: String,
    val desc: String,
    val animated: Boolean = false
)

val SHIP_PAINTS = listOf(
    ShipPaintInfo(0, "Solar Orange", 800, "#ff7838", "Fleet amber heat shield coating"),
    ShipPaintInfo(1, "Ion Cyan", 0, "#2ad6ff", "Cadet issue cobalt armor"),
    ShipPaintInfo(2, "Nova Violet", 2500, "#bc5cff", "Nebula violet stealth finish"),
    ShipPaintInfo(3, "Plasma Mint", 5500, "#66ffb8", "Bio-polymer mint nanite composite"),
    ShipPaintInfo(4, "Pulsar Gold", 14000, "#ffd24a", "Gilded royal flagship armor"),
    ShipPaintInfo(5, "Crimson Void", 30000, "#ff4646", "Raider crimson battle plate"),
    ShipPaintInfo(6, "Obsidian Dark", 65000, "#707d91", "Radar-absorbing titanium shadow"),
    ShipPaintInfo(7, "Quantum Neon", 120000, "#ff5ae6", "Zero-point fluorescent overdrive"),
    ShipPaintInfo(8, "Rainbow Prism", 1000000, "#ffffff", "Animated dynamic chromatic spectrum wave", animated = true)
)

data class EngineTrailInfo(
    val id: Int,
    val name: String,
    val price: Int,
    val hex: String,
    val rgb: IntArray,
    val desc: String,
    val animated: Boolean = false
)

val ENGINE_TRAILS = listOf(
    EngineTrailInfo(0, "Ember Fire", 1000, "#ff7838", intArrayOf(255, 120, 56), "Combustion afterburner flame"),
    EngineTrailInfo(1, "Ion Cyan", 0, "#2ad6ff", intArrayOf(42, 214, 255), "Sub-light ion drive wake"),
    EngineTrailInfo(2, "Nova Purple", 3200, "#bc5cff", intArrayOf(188, 92, 255), "Exotic tachyon particle plume"),
    EngineTrailInfo(3, "Aurora Mint", 7000, "#66ffb8", intArrayOf(102, 255, 184), "Plasma jet stream exhaust"),
    EngineTrailInfo(4, "Solar Gold", 16000, "#ffd24a", intArrayOf(255, 210, 50), "Photon flare thrust wake"),
    EngineTrailInfo(5, "Crimson Flame", 35000, "#ff3c3c", intArrayOf(255, 60, 60), "Heavy raider afterburner"),
    EngineTrailInfo(6, "Void Shadow", 70000, "#826eaa", intArrayOf(130, 110, 170), "Dark energy drive exhaust"),
    EngineTrailInfo(7, "Rainbow Trail", 130000, "#ffffff", intArrayOf(255, 255, 255), "Animated spectrum rainbow wake", animated = true)
)

data class TechUpgradeInfo(
    val id: Int,
    val name: String,
    val shortDesc: String,
    val prices: IntArray,
    val levelDescs: Array<String>,
    val mults: FloatArray? = null,
    val cdMults: FloatArray? = null,
    val startShields: IntArray? = null,
    val maxShields: IntArray? = null,
    val maxLives: IntArray? = null,
    val cooldowns: IntArray? = null,
    val invulnTicks: IntArray? = null,
    val coinMults: FloatArray? = null,
    val durations: IntArray? = null
)

val TECH_UPGRADES = listOf(
    TechUpgradeInfo(
        0, "Ion Engine", "Ship speed 0.7x -> 2.0x",
        intArrayOf(800, 2500, 7000, 18000, 40000),
        arrayOf("Lv0 Slow 0.70x", "Lv1 Agility 0.86x", "Lv2 Normal 1.00x", "Lv3 Fast 1.25x", "Lv4 Turbo 1.60x", "MAX Hyper 2.00x!"),
        mults = floatArrayOf(180f/256f, 220f/256f, 1.0f, 320f/256f, 410f/256f, 2.0f)
    ),
    TechUpgradeInfo(
        1, "Fire Rate", "2 shots/sec -> 10+/sec",
        intArrayOf(1000, 3000, 8000, 20000, 45000),
        arrayOf("Lv0 2 shots/sec", "Lv1 ~3.2 shots/sec", "Lv2 ~4.5 shots/sec", "Lv3 ~6.0 shots/sec", "Lv4 ~8.2 shots/sec", "MAX 10+ shots/sec!"),
        cdMults = floatArrayOf(1.0f, 210f/256f, 165f/256f, 125f/256f, 95f/256f, 75f/256f)
    ),
    TechUpgradeInfo(
        2, "Plasma Core", "+1..+5 damage to all guns",
        intArrayOf(1500, 4000, 10000, 25000, 55000),
        arrayOf("Lv0 Base damage", "Lv1 +1 damage", "Lv2 +2 heavy damage", "Lv3 +3 god power", "Lv4 +4 devastation", "MAX +5 destruction!")
    ),
    TechUpgradeInfo(
        3, "Shield Battery", "Higher capacity & start shields",
        intArrayOf(1200, 3500, 9000, 22000, 50000),
        arrayOf("Lv0: 0 start, 2 max", "Lv1: 1 start, 3 max", "Lv2: 1 start, 4 max", "Lv3: 2 start, 5 max", "Lv4: 2 start, 6 max", "MAX: 3 start, 6 cap!"),
        startShields = intArrayOf(0, 1, 1, 2, 2, 3),
        maxShields = intArrayOf(2, 3, 4, 5, 6, 6)
    ),
    TechUpgradeInfo(
        4, "Hull Plating", "Extra ship lives (2 -> 7)",
        intArrayOf(1200, 3500, 9000, 22000, 50000),
        arrayOf("Lv0: 2 lives (tough!)", "Lv1: 3 lives normal", "Lv2: 4 lives sturdy", "Lv3: 5 lives tank", "Lv4: 6 lives titan", "MAX: 7 lives beast!"),
        maxLives = intArrayOf(2, 3, 4, 5, 6, 7)
    ),
    TechUpgradeInfo(
        5, "Afterburner", "Lower Dash cd & +invuln",
        intArrayOf(800, 2500, 7000, 18000, 40000),
        arrayOf("Lv0: 1.4s cooldown", "Lv1: 1.1s (-20%)", "Lv2: 0.86s (-35%)", "Lv3: 0.66s (-50%)", "Lv4: 0.50s (-65%)", "MAX: 0.40s instant!"),
        cooldowns = intArrayOf(84, 66, 52, 40, 30, 24),
        invulnTicks = intArrayOf(16, 19, 22, 25, 28, 31)
    ),
    TechUpgradeInfo(
        6, "Graviton Magnet", "+Coins & magnetic pull",
        intArrayOf(1000, 3000, 8000, 20000, 45000),
        arrayOf("Lv0: 1x coins, no magnet", "Lv1: +35% $ & small pull", "Lv2: +70% $ & med pull", "Lv3: +105% $ & big pull", "Lv4: +140% $ & huge pull", "MAX: +175% $ & vortex!"),
        coinMults = floatArrayOf(1.0f, 1.35f, 1.70f, 2.05f, 2.40f, 2.75f)
    ),
    TechUpgradeInfo(
        7, "Overdrive Unit", "Rapid Fire powerup duration",
        intArrayOf(1000, 3000, 7500, 18000, 40000),
        arrayOf("Lv0: Rapid fire 8 sec", "Lv1: Rapid fire 11 sec", "Lv2: Rapid fire 14 sec", "Lv3: Rapid fire 18 sec", "Lv4: Rapid fire 22 sec", "MAX: Rapid fire 26 sec!"),
        durations = intArrayOf(480, 660, 840, 1080, 1320, 1560)
    )
)

val RAINBOW_COLORS_RGB = arrayOf(
    intArrayOf(255, 70, 70),   // Red
    intArrayOf(255, 120, 56),  // Orange
    intArrayOf(255, 210, 74),  // Gold
    intArrayOf(102, 255, 184), // Mint
    intArrayOf(42, 214, 255),  // Cyan
    intArrayOf(188, 92, 255),  // Violet
    intArrayOf(255, 90, 230)   // Pink
)

fun formatPrice(price: Int): String {
    if (price >= 1000000) {
        val m = price / 1000000
        val remainder = (price % 1000000) / 100000
        return if (remainder == 0) "${m}M" else "${m}.${remainder}M"
    }
    if (price >= 1000) {
        if (price % 1000 == 0) return "${price / 1000}k"
        val k = price / 1000
        val r = (price % 1000) / 100
        return "${k}.${r}k"
    }
    return "${price}c"
}

fun parseColorHex(hex: String, defaultColor: Int = Color.WHITE): Int {
    return try {
        Color.parseColor(hex)
    } catch (e: Exception) {
        defaultColor
    }
}

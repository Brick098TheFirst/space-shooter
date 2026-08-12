package com.brick.spaceshooter

import android.content.Context
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.media.SoundPool
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager

enum class Sfx {
    LASER,
    EXPLOSION,
    PICKUP
}

enum class Bgm {
    NONE,
    MENU,
    GAME
}

class AudioManager(private val context: Context, private val saveManager: SaveManager) {

    private val soundPool: SoundPool
    private var laserSoundId = 0
    private var explosionSoundId = 0
    private var pickupSoundId = 0

    private var mediaPlayer: MediaPlayer? = null
    private var currentBgm: Bgm = Bgm.NONE
    private var isPaused = false

    init {
        val audioAttributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
        soundPool = SoundPool.Builder()
            .setMaxStreams(16)
            .setAudioAttributes(audioAttributes)
            .build()

        loadSounds()
    }

    private fun loadSounds() {
        try {
            laserSoundId = soundPool.load(context, R.raw.laser, 1)
            explosionSoundId = soundPool.load(context, R.raw.explosion, 1)
            pickupSoundId = soundPool.load(context, R.raw.pickup, 1)
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    fun playSfx(sfx: Sfx, volMult: Float = 1f) {
        val vol = (saveManager.sfxVolume / 100f) * volMult
        if (vol <= 0f) return
        val id = when (sfx) {
            Sfx.LASER -> laserSoundId
            Sfx.EXPLOSION -> explosionSoundId
            Sfx.PICKUP -> pickupSoundId
        }
        if (id != 0) {
            soundPool.play(id, vol, vol, 1, 0, 1.0f)
        }
    }

    fun playMusic(bgm: Bgm) {
        if (bgm == currentBgm && mediaPlayer?.isPlaying == true) {
            updateMusicVolume()
            return
        }
        stopMusic()
        currentBgm = bgm
        val resId = when (bgm) {
            Bgm.MENU -> R.raw.menu
            Bgm.GAME -> R.raw.game
            Bgm.NONE -> return
        }
        try {
            mediaPlayer = MediaPlayer.create(context, resId)?.apply {
                isLooping = true
                val vol = saveManager.musicVolume / 100f
                setVolume(vol, vol)
                start()
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    fun updateMusicVolume() {
        val vol = saveManager.musicVolume / 100f
        mediaPlayer?.setVolume(vol, vol)
    }

    fun stopMusic() {
        try {
            mediaPlayer?.stop()
            mediaPlayer?.release()
        } catch (e: Exception) {
            e.printStackTrace()
        }
        mediaPlayer = null
        currentBgm = Bgm.NONE
    }

    fun pauseMusic() {
        if (mediaPlayer?.isPlaying == true) {
            mediaPlayer?.pause()
            isPaused = true
        }
    }

    fun resumeMusic() {
        if (isPaused && mediaPlayer != null) {
            mediaPlayer?.start()
            isPaused = false
        }
    }

    fun triggerHaptic(durationMs: Long = 15L) {
        if (!saveManager.haptics) return
        try {
            val vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vm = context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as VibratorManager
                vm.defaultVibrator
            } else {
                @Suppress("DEPRECATION")
                context.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                vibrator.vibrate(VibrationEffect.createOneShot(durationMs, VibrationEffect.DEFAULT_AMPLITUDE))
            } else {
                @Suppress("DEPRECATION")
                vibrator.vibrate(durationMs)
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    fun release() {
        stopMusic()
        soundPool.release()
    }
}

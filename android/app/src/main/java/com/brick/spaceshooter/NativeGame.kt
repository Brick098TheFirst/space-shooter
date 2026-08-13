package com.brick.spaceshooter

object NativeGame {
    init {
        System.loadLibrary("spacegame")
    }

    const val SCREEN_W = 284
    const val SCREEN_H = 160
    const val MIX_SAMPLES = 202
    const val AUDIO_SAMPLE_RATE = 44100
    const val AUDIO_SAMPLES_PER_FRAME = 490
    const val TARGET_FPS = 90

    const val KEY_A = 0x0001
    const val KEY_B = 0x0002
    const val KEY_SELECT = 0x0004
    const val KEY_START = 0x0008
    const val KEY_RIGHT = 0x0010
    const val KEY_LEFT = 0x0020
    const val KEY_UP = 0x0040
    const val KEY_DOWN = 0x0080
    const val KEY_R = 0x0100
    const val KEY_L = 0x0200

    const val SCREEN_MAIN_MENU = 0
    const val SCREEN_HANGAR = 1
    const val SCREEN_SETTINGS = 2
    const val SCREEN_CONTROLS = 3
    const val SCREEN_PLAYING = 4
    const val SCREEN_PAUSED = 5
    const val SCREEN_GAME_OVER = 6
    const val SCREEN_OPTIONS = 7
    const val SCREEN_MODE_SELECT = 8

    external fun nativeInit(saveDir: String)
    external fun nativeSetKeys(keys: Int)
    external fun nativeTick()
    external fun nativePresent(pixels: IntArray)
    external fun nativeMixAudio(out: ShortArray): Int
    external fun nativeGetScreen(): Int
    external fun nativeQueueTap(x: Int, y: Int)
    external fun nativeGoBack()
    external fun nativeLoadSave(data: ByteArray)
    external fun nativeGetSave(): ByteArray
    external fun nativeFlushSave()
    external fun nativeGetTilt(): Int
    external fun nativeGetHaptics(): Int
    external fun nativeTakeHaptics(out: IntArray): Int
}

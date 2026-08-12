package com.brick.spaceshooter

object NativeGame {
    init {
        System.loadLibrary("spacegame")
    }

    const val SCREEN_W = 240
    const val SCREEN_H = 160
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

    external fun nativeInit()
    external fun nativeSetKeys(keys: Int)
    external fun nativeTick()
    external fun nativePresent(pixels: IntArray)
    external fun nativeMixAudio(out: ShortArray): Int
    external fun nativeLoadSave(data: ByteArray)
    external fun nativeGetSave(): ByteArray
}

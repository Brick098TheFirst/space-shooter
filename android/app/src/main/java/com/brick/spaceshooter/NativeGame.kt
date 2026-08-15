package com.brick.spaceshooter

object NativeGame {
    init {
        System.loadLibrary("spacegame")
    }

    /** Framebuffer is always this tall; the width adapts to the device. */
    const val SCREEN_H = 160

    /** Widescreen frame width range (host side clamps to the same bounds). */
    const val MIN_FRAME_W = 284
    const val MAX_FRAME_W = 480
    const val DEFAULT_FRAME_W = 284
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
    const val SCREEN_MULTIPLAYER = 9
    const val SCREEN_STORY_INTRO = 10
    const val SCREEN_STORY_MAP = 11
    const val SCREEN_STORY_SHOP = 12
    const val SCREEN_STORY_RESULT = 13

    const val EOS_CONFIG_REQUIRED = 0
    const val EOS_INITIALIZING = 1
    const val EOS_SIGNING_IN = 2
    const val EOS_READY = 3
    const val EOS_MATCHMAKING = 4
    const val EOS_WAITING_FOR_PLAYER = 5
    const val EOS_MATCHED = 6
    const val EOS_ERROR = 7

    external fun nativeInit(saveDir: String)
    external fun nativeSetKeys(keys: Int)
    external fun nativeTick()
    external fun nativePresent(pixels: IntArray)
    external fun nativeMixAudio(out: ShortArray): Int
    external fun nativeGetScreen(): Int
    /** True while a Story level is frozen on its tap-to-continue brief. */
    external fun nativeStoryWaitingForStart(): Int
    external fun nativeQueueTap(x: Int, y: Int)
    external fun nativeGoBack()
    external fun nativeScrollTo(px: Float): Float
    external fun nativeScrollGet(): Float
    external fun nativeScrollMax(): Float
    external fun nativeLoadSave(data: ByteArray)
    external fun nativeGetSave(): ByteArray
    external fun nativeFlushSave()
    external fun nativeGetHaptics(): Int
    external fun nativeTakeHaptics(out: IntArray): Int
    external fun nativeSetViewport(width: Int)
    external fun nativeGetFrameWidth(): Int
    /** 1 once per Settings -> CODES activation; opens the cheat dialog. */
    external fun nativeTakeCodeRequest(): Int
    /** 1 if the cheat code was accepted, 0 if unknown. */
    external fun nativeApplyCheatCode(code: String): Int
    /** 1 once per Settings -> ERASE DATA activation. */
    external fun nativeTakeEraseRequest(): Int
    /** Wipe coins, unlocks, upgrades, high score, and settings. */
    external fun nativeResetAllData()

    // Epic Online Services: Device-ID login, public two-player lobby, and P2P.
    external fun nativeEosInitialize(
        internalDir: String,
        externalDir: String,
        productId: String,
        sandboxId: String,
        deploymentId: String,
        clientId: String,
        clientSecret: String,
        displayName: String
    ): Int
    external fun nativeEosTick()
    external fun nativeEosSetForeground(foreground: Boolean)
    external fun nativeEosShutdown()
    external fun nativeEosGetStatus(): Int
    external fun nativeEosGetStatusText(): String
    external fun nativeEosQuickMatch(): Int
    external fun nativeEosCancelMatch()
    external fun nativeEosIsHost(): Int
    external fun nativeEosMemberCount(): Int
    external fun nativeEosSendPacket(packet: ByteArray, channel: Int, reliable: Boolean): Int
    /** Return value packs channel in bits 24..31 and byte count in bits 0..23. */
    external fun nativeEosReceivePacket(out: ByteArray): Int
    /** Opens the native co-op Multiplayer tab (Quick Match controls + status). */
    external fun nativeOpenMultiplayer()
}

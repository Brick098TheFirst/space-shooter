#include <jni.h>
#include <string.h>
#include "platform.h"
#include "types.h"
#include "renderer.h"
#include "audio.h"
#include "save.h"
#include "starfield.h"
#include "game.h"
#include "menu.h"
#include "audio_data.h"
#include "eos_online.h"
#include "coop.h"

#ifndef HOST_OUT_RATE
#define HOST_OUT_RATE 44100
#endif

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeInit(JNIEnv* env, jclass clazz, jstring saveDir) {
    (void)clazz;
    platform_host_init();
    if (saveDir) {
        const char* dir = (*env)->GetStringUTFChars(env, saveDir, NULL);
        if (dir) {
            platform_set_save_dir(dir);
            (*env)->ReleaseStringUTFChars(env, saveDir, dir);
        }
    }
    gfx_init();
    audio_init();
    save_load();
    starfield_init();
    game_init();
    menu_init();
    coop_init();
    audio_start();
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeSetKeys(JNIEnv* env, jclass clazz, jint keys) {
    (void)env; (void)clazz;
    platform_set_keys((u16)keys);
}

/* Tracks the last EOS match state so the co-op layer is told exactly once
 * when a lobby is joined / left. */
static int s_coop_prev_matched = 0;

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeTick(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    menu_update();
    /* Co-op glue: runs after the local keys are polled and before the frame
     * is drawn, so the guest renders the freshest host snapshot and the host
     * broadcasts its current simulation state. */
    coop_tick();
    menu_draw();
    audio_update();
    gfx_flip();

    /* Keep the co-op session lifecycle tied to the EOS match state. */
    int matched = (eos_online_status() == EOS_ONLINE_MATCHED);
    if (matched && !s_coop_prev_matched) {
        coop_on_matched(eos_online_is_host());
    } else if (!matched && s_coop_prev_matched) {
        coop_on_unmatched();
    }
    s_coop_prev_matched = matched;
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativePresent(JNIEnv* env, jclass clazz, jintArray pixels) {
    (void)clazz;
    jint* dst = (*env)->GetIntArrayElements(env, pixels, NULL);
    if (!dst) return;
    gfx_present_argb8888((u32*)dst);
    (*env)->ReleaseIntArrayElements(env, pixels, dst, 0);
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeMixAudio(JNIEnv* env, jclass clazz, jshortArray out) {
    (void)clazz;
    const s8* src = audio_host_mix_buffer();
    if (!src) return 0;
    jsize n = (*env)->GetArrayLength(env, out);
    if (n <= 0) return 0;
    jshort* dst = (*env)->GetShortArrayElements(env, out, NULL);
    if (!dst) return 0;

    /* Linear-resample the 18.157 kHz mix buffer up to the host output rate
     * (44100). Playing the unusual GBA rate directly makes many devices
     * pitch-shift and boom the menu track. */
    const int in_n = AUDIO_SAMPLES_PER_FRAME;
    for (jsize i = 0; i < n; i++) {
        int pos = (int)(((long)i * (in_n - 1) * 256) / (n > 1 ? (n - 1) : 1));
        int idx = pos >> 8;
        int frac = pos & 255;
        if (idx >= in_n - 1) {
            idx = in_n - 1;
            frac = 0;
        }
        int s0 = (int)src[idx];
        int s1 = (int)src[(idx + 1 < in_n) ? idx + 1 : idx];
        int mixed = s0 + (((s1 - s0) * frac) >> 8);
        int sample = mixed * 200; /* ~78% of <<8, leaves headroom */
        if (sample > 32767) sample = 32767;
        else if (sample < -32768) sample = -32768;
        dst[i] = (jshort)sample;
    }

    (*env)->ReleaseShortArrayElements(env, out, dst, 0);
    return n;
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeGetScreen(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return (jint)menu_current_screen();
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeStoryWaitingForStart(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return (jint)game_story_waiting_for_start();
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeQueueTap(JNIEnv* env, jclass clazz, jint x, jint y) {
    (void)env; (void)clazz;
    menu_queue_tap((int)x, (int)y);
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeGoBack(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    menu_go_back();
}

JNIEXPORT jfloat JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeScrollTo(JNIEnv* env, jclass clazz, jfloat px) {
    (void)env; (void)clazz;
    menu_scroll_to((float)px);
    return (jfloat)menu_scroll_get();
}

JNIEXPORT jfloat JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeScrollGet(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return (jfloat)menu_scroll_get();
}

JNIEXPORT jfloat JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeScrollMax(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return (jfloat)menu_scroll_max();
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeLoadSave(JNIEnv* env, jclass clazz, jbyteArray data) {
    (void)clazz;
    if (!data) return;
    jsize n = (*env)->GetArrayLength(env, data);
    if (n > PLATFORM_SRAM_SIZE) n = PLATFORM_SRAM_SIZE;
    jbyte* src = (*env)->GetByteArrayElements(env, data, NULL);
    if (!src) return;
    memcpy((void*)platform_sram, src, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, data, src, JNI_ABORT);
    save_load();
}

JNIEXPORT jbyteArray JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeGetSave(JNIEnv* env, jclass clazz) {
    (void)clazz;
    jbyteArray arr = (*env)->NewByteArray(env, PLATFORM_SRAM_SIZE);
    if (!arr) return NULL;
    (*env)->SetByteArrayRegion(env, arr, 0, PLATFORM_SRAM_SIZE, (const jbyte*)platform_sram);
    return arr;
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeFlushSave(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    save_write();
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeGetHaptics(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return g_settings.haptics ? 1 : 0;
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeTakeHaptics(JNIEnv* env, jclass clazz, jintArray out) {
    (void)clazz;
    if (!out) return 0;
    jsize n = (*env)->GetArrayLength(env, out);
    if (n <= 0) return 0;
    jint* dst = (*env)->GetIntArrayElements(env, out, NULL);
    if (!dst) return 0;
    int got = platform_take_haptics((int*)dst, (int)n);
    (*env)->ReleaseIntArrayElements(env, out, dst, 0);
    return got;
}

/* ── Adaptive widescreen ─────────────────────────────────────────────── */
/* Kotlin measures its view on layout, derives the framebuffer width that
 * matches the device aspect ratio (160 px tall always), and pushes it here.
 * The game then renders edge-to-edge: no side bars on any phone. */
JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeSetViewport(JNIEnv* env, jclass clazz, jint w) {
    (void)env; (void)clazz;
    if (host_set_screen_width((int)w)) {
        /* Star positions and the cached static layers were built for the old
         * width — rebuild them so nothing renders misplaced or garbage. */
        starfield_init();
        menu_request_full_redraw();
        game_request_full_redraw();
    }
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeGetFrameWidth(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return (jint)host_screen_width();
}

/* ── Cheat codes (Settings -> CODES) ───────────────────────────────────── */
JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeTakeCodeRequest(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return (jint)menu_take_code_request();
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeApplyCheatCode(JNIEnv* env, jclass clazz, jstring code) {
    (void)clazz;
    if (!code) return 0;
    const char* s = (*env)->GetStringUTFChars(env, code, NULL);
    if (!s) return 0;
    int applied = save_apply_cheat(s);
    (*env)->ReleaseStringUTFChars(env, code, s);
    return (jint)applied;
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeTakeEraseRequest(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return (jint)menu_take_erase_request();
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeResetAllData(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    save_reset_all();
    menu_request_full_redraw();
}

/* ── Epic Online Services (Android only) ─────────────────────────────── */
JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeEosInitialize(
        JNIEnv* env, jclass clazz,
        jstring internalDir, jstring externalDir, jstring productId,
        jstring sandboxId, jstring deploymentId, jstring clientId,
        jstring clientSecret, jstring displayName) {
    (void)clazz;
    const char* internal_dir = internalDir ? (*env)->GetStringUTFChars(env, internalDir, NULL) : NULL;
    const char* external_dir = externalDir ? (*env)->GetStringUTFChars(env, externalDir, NULL) : NULL;
    const char* product_id = productId ? (*env)->GetStringUTFChars(env, productId, NULL) : NULL;
    const char* sandbox_id = sandboxId ? (*env)->GetStringUTFChars(env, sandboxId, NULL) : NULL;
    const char* deployment_id = deploymentId ? (*env)->GetStringUTFChars(env, deploymentId, NULL) : NULL;
    const char* client_id = clientId ? (*env)->GetStringUTFChars(env, clientId, NULL) : NULL;
    const char* client_secret = clientSecret ? (*env)->GetStringUTFChars(env, clientSecret, NULL) : NULL;
    const char* display_name = displayName ? (*env)->GetStringUTFChars(env, displayName, NULL) : NULL;

    EosOnlineConfig config = {
        internal_dir, external_dir, product_id, sandbox_id, deployment_id,
        client_id, client_secret, display_name
    };
    int result = eos_online_initialize(&config);

    if (internal_dir) (*env)->ReleaseStringUTFChars(env, internalDir, internal_dir);
    if (external_dir) (*env)->ReleaseStringUTFChars(env, externalDir, external_dir);
    if (product_id) (*env)->ReleaseStringUTFChars(env, productId, product_id);
    if (sandbox_id) (*env)->ReleaseStringUTFChars(env, sandboxId, sandbox_id);
    if (deployment_id) (*env)->ReleaseStringUTFChars(env, deploymentId, deployment_id);
    if (client_id) (*env)->ReleaseStringUTFChars(env, clientId, client_id);
    if (client_secret) (*env)->ReleaseStringUTFChars(env, clientSecret, client_secret);
    if (display_name) (*env)->ReleaseStringUTFChars(env, displayName, display_name);
    return (jint)result;
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeEosTick(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    eos_online_tick();
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeEosSetForeground(JNIEnv* env, jclass clazz, jboolean foreground) {
    (void)env; (void)clazz;
    eos_online_set_foreground(foreground ? 1 : 0);
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeEosShutdown(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    eos_online_shutdown();
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeEosGetStatus(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return (jint)eos_online_status();
}

JNIEXPORT jstring JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeEosGetStatusText(JNIEnv* env, jclass clazz) {
    (void)clazz;
    return (*env)->NewStringUTF(env, eos_online_status_text());
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeEosQuickMatch(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return (jint)eos_online_quick_match();
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeEosCancelMatch(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    eos_online_cancel_match();
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeEosIsHost(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return (jint)eos_online_is_host();
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeEosMemberCount(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return (jint)eos_online_member_count();
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeOpenMultiplayer(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    /* Surface area managed fully by menu.c (Quick Match buttons, live lobby
     * status, launch). The Kotlin online chip routes here. */
    menu_open(SCREEN_MULTIPLAYER);
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeEosSendPacket(
        JNIEnv* env, jclass clazz, jbyteArray packet, jint channel, jboolean reliable) {
    (void)clazz;
    if (!packet) return 0;
    jsize size = (*env)->GetArrayLength(env, packet);
    if (size <= 0 || size > 1170) return 0;
    jbyte* bytes = (*env)->GetByteArrayElements(env, packet, NULL);
    if (!bytes) return 0;
    int sent = eos_online_send_packet(bytes, (uint32_t)size, (uint8_t)channel, reliable ? 1 : 0);
    (*env)->ReleaseByteArrayElements(env, packet, bytes, JNI_ABORT);
    return (jint)sent;
}

JNIEXPORT jint JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeEosReceivePacket(
        JNIEnv* env, jclass clazz, jbyteArray out) {
    (void)clazz;
    if (!out) return 0;
    jsize capacity = (*env)->GetArrayLength(env, out);
    if (capacity <= 0) return 0;
    jbyte* bytes = (*env)->GetByteArrayElements(env, out, NULL);
    if (!bytes) return 0;
    uint8_t channel = 0;
    int received = eos_online_receive_packet(bytes, (uint32_t)capacity, &channel);
    (*env)->ReleaseByteArrayElements(env, out, bytes, 0);
    if (received <= 0) return 0;
    return (jint)(((uint32_t)channel << 24) | (uint32_t)received);
}

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
    audio_start();
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeSetKeys(JNIEnv* env, jclass clazz, jint keys) {
    (void)env; (void)clazz;
    platform_set_keys((u16)keys);
}

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeTick(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    menu_update();
    menu_draw();
    audio_update();
    gfx_flip();
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
Java_com_brick_spaceshooter_NativeGame_nativeGetTilt(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return g_settings.tilt_steer ? 1 : 0;
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

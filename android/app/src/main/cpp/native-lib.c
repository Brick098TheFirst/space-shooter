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

JNIEXPORT void JNICALL
Java_com_brick_spaceshooter_NativeGame_nativeInit(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    platform_host_init();
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
    if (n > AUDIO_SAMPLES_PER_FRAME) n = AUDIO_SAMPLES_PER_FRAME;
    jshort* dst = (*env)->GetShortArrayElements(env, out, NULL);
    if (!dst) return 0;
    for (jsize i = 0; i < n; i++) {
        dst[i] = (jshort)(src[i] << 8);
    }
    (*env)->ReleaseShortArrayElements(env, out, dst, 0);
    return n;
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

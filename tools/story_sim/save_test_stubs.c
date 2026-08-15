#include "platform_host.h"
#include "types.h"
volatile u8 platform_sram[PLATFORM_SRAM_SIZE];
volatile u16 REG_VCOUNT;
static int s_w = HOST_SCREEN_W_DEFAULT;
int host_screen_width(void){return s_w;}
int host_set_screen_width(int w){ if(w==s_w) return 0; s_w=w; return 1;}
void platform_host_init(void){}
void platform_set_save_dir(const char* d){(void)d;}
void platform_persist_save(void){}
bool platform_restore_save(void){return false;}
void platform_queue_haptic(int t){(void)t;}
int platform_take_haptics(int* o,int m){(void)o;(void)m;return 0;}
void key_poll(void){}
u32 key_hit(u32 k){(void)k;return 0;}
u32 key_is_down(u32 k){(void)k;return 0;}
void platform_set_keys(u16 k){(void)k;}
const char* gfx_get_weapon_name(WeaponRig r){(void)r;return "RIG";}
const char* gfx_get_weapon_desc(WeaponRig r){(void)r;return "rig desc";}
const char* gfx_get_laser_name(int i){(void)i;return "LASER";}
const char* gfx_get_laser_desc(int i){(void)i;return "laser desc";}
const char* gfx_get_accent_name(int i){(void)i;return "PAINT";}
const char* gfx_get_accent_desc(int i){(void)i;return "paint desc";}

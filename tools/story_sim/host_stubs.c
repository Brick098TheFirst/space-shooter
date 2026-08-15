/* Host stubs so the real game core can be simulated headlessly. */
#include "platform_host.h"
#include "types.h"
#include "renderer.h"
#include "audio.h"
#include "starfield.h"
#include "coop.h"
#include <string.h>

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
static u16 s_keys=0, s_prev=0;
void key_poll(void){ s_prev=s_keys; }
u32 key_hit(u32 k){ return (s_keys & ~s_prev & k); }
u32 key_is_down(u32 k){ return s_keys & k; }
void platform_set_keys(u16 k){ s_prev=s_keys; s_keys=k; }

/* rendering: no-ops */
u8 gfx_static_layer[FB_PIXELS];
void gfx_init(void){} void gfx_flip(void){}
const u8* gfx_get_framebuffer(void){return gfx_static_layer;}
void gfx_present_argb8888(u32* d){(void)d;}
void gfx_clear(u8 c){(void)c;}
void gfx_set_target(u8* b){(void)b;} void gfx_apply_static(void){}
void gfx_set_clip(int a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;}
void gfx_clear_clip(void){}
void gfx_draw_pixel(int a,int b,u8 c){(void)a;(void)b;(void)c;}
void gfx_fill_rect(int a,int b,int c,int d,u8 e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void gfx_draw_rect(int a,int b,int c,int d,u8 e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void gfx_draw_glass_card(int a,int b,int c,int d,u8 e,u8 f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
void gfx_draw_sprite(int a,int b,int c,int d,const u8* e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void gfx_draw_sprite_rainbow(int a,int b,int c,int d,const u8* e,int f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
void gfx_draw_ship(int a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;}
void gfx_draw_ship_styled(int a,int b,int c,int d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void gfx_draw_enemy_ship(int a,int b){(void)a;(void)b;}
void gfx_draw_boss_drone(int a,int b,bool c,bool d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void gfx_draw_laser(int a,int b,bool c,int d,int e,bool f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
bool gfx_laser_is_animated(int i){(void)i;return false;}
void gfx_draw_sprite_rotated(int a,int b,int c,int d,const u8* e,int f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
void gfx_draw_sprite_clipped(int a,int b,int c,int d,const u8* e,int f,int g,int h,int i){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;}
void gfx_draw_char(int a,int b,char c,u8 d){(void)a;(void)b;(void)c;(void)d;}
void gfx_draw_text(int a,int b,const char* c,u8 d){(void)a;(void)b;(void)c;(void)d;}
void gfx_draw_text_shadow(int a,int b,const char* c,u8 d,u8 e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void gfx_draw_text_centered(int a,int b,int c,const char* d,u8 e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void gfx_draw_button(int a,int b,int c,int d,const char* e,bool f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
void gfx_draw_badge(int a,int b,const char* c,u8 d){(void)a;(void)b;(void)c;(void)d;}
void gfx_draw_swatch(int a,int b,int c,u8 d,const char* e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void gfx_draw_progress_bar(int a,int b,int c,int d,int e,int f,u8 g,u8 h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
u8 gfx_get_accent_color(int i){(void)i;return 1;}
u8 gfx_get_rainbow_color(int i){(void)i;return 1;}
u8 gfx_get_trail_color(int i){(void)i;return 1;}
u8 gfx_get_trail_color_animated(int i,int f){(void)i;(void)f;return 1;}
u8 gfx_get_laser_color(int i){(void)i;return 1;}
const char* gfx_get_accent_name(int i){(void)i;return "PAINT";}
const char* gfx_get_accent_desc(int i){(void)i;return "d";}
const char* gfx_get_ship_style_name(int i){(void)i;return "HULL";}
const char* gfx_get_ship_style_desc(int i){(void)i;return "d";}
const char* gfx_get_trail_name(int i){(void)i;return "TRAIL";}
const char* gfx_get_trail_desc(int i){(void)i;return "d";}
const char* gfx_get_weapon_name(WeaponRig r){(void)r;return "RIG";}
const char* gfx_get_weapon_desc(WeaponRig r){(void)r;return "d";}
const char* gfx_get_laser_name(int i){(void)i;return "LASER";}
const char* gfx_get_laser_desc(int i){(void)i;return "d";}
const char* gfx_get_diff_name(Difficulty d){(void)d;return "PILOT";}
const u8* gfx_get_laser_standard_sprite(int i){(void)i;return gfx_static_layer;}
const u8* gfx_get_laser_heavy_sprite(int i){(void)i;return gfx_static_layer;}

/* audio / starfield / coop: no-ops */
void audio_init(void){} void audio_start(void){} void audio_update(void){}
void audio_play_bgm(BgmTrack t){(void)t;} void audio_play_sfx(SfxId t){(void)t;}
void audio_stop_bgm(void){} void audio_stop_all(void){}
void audio_begin_boss_music(void){} void audio_end_boss_music(void){}
const s8* audio_host_mix_buffer(void){return 0;}
void starfield_init(void){} void starfield_update(void){}
void starfield_draw_base(int a,int b){(void)a;(void)b;}
void starfield_draw_stars(int a,int b){(void)a;(void)b;}
/* Backdrop themes are purely cosmetic, so the headless sim just records the
 * last one asked for. */
static int s_theme_stub = 0;
void starfield_set_theme(int t){ s_theme_stub = t; }
int  starfield_theme(void){ return s_theme_stub; }
void coop_init(void){} void coop_tick(void){}
void coop_on_matched(int h){(void)h;} void coop_on_unmatched(void){}
int coop_in_session(void){return 0;} int coop_is_host(void){return 0;}
void coop_leave_session(void){}

/* Sprite data stand-ins (rendering is stubbed out, only addresses matter). */
#include "gfx_data.h"
const u16 master_palette[256];
const u8 spr_ship[9][20*16];
const u8 spr_ship_styles[NUM_SHIP_STYLES][20*16];
const u8 spr_ast_large[24*24];
const u8 spr_ast_med_a[16*16];
const u8 spr_ast_med_b[16*16];
const u8 spr_ast_small[10*10];
const u8 spr_ast_tiny[6*6];
const u8 spr_drone[18*14];
const u8 spr_laser_standard[4*10];
const u8 spr_laser_heavy[6*14];
const u8 spr_laser_enemy[6*6];
const u8 spr_shield_bubble[24*24];
const u8 spr_pwr_shield[10*10];
const u8 spr_pwr_rapid[10*10];
const u8 spr_pwr_repair[10*10];
const u8 spr_explosion[9][24*24];
const u8 font_5x7[96][7];

/* Only the bits ui_shots needs that aren't real code: audio + EOS + coop. */
#include "types.h"
#include "audio.h"
#include "coop.h"
#include "eos_online.h"
void audio_init(void){} void audio_start(void){} void audio_update(void){}
void audio_play_bgm(BgmTrack t){(void)t;} void audio_play_sfx(SfxId t){(void)t;}
void audio_stop_bgm(void){} void audio_stop_all(void){}
void audio_begin_boss_music(void){} void audio_end_boss_music(void){}
const s8* audio_host_mix_buffer(void){return 0;}
void coop_init(void){} void coop_tick(void){}
void coop_on_matched(int h){(void)h;} void coop_on_unmatched(void){}
int coop_in_session(void){return 0;} int coop_is_host(void){return 0;}
void coop_leave_session(void){}
int eos_online_status(void){return 0;}
const char* eos_online_status_text(void){return "stub";}
int eos_online_is_host(void){return 0;}
int eos_online_member_count(void){return 0;}
int eos_online_quick_match(void){return 0;}
void eos_online_cancel_match(void){}

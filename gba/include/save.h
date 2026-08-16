#ifndef SAVE_H
#define SAVE_H

#include "types.h"

void save_init_defaults(void);
void save_load(void);
void save_write(void);

/* Writes the coin balance into dst ("1,234,567"; big balances shorten to
 * "1.5B" / "999T"). dst_cap includes the terminator. */
void save_format_coins(char* dst, int dst_cap);

#ifdef PLATFORM_HOST
/* Cheat codes are an Android-only feature: the entry UI lives in the
 * Android Settings screen and pops a native text dialog.
 * Returns a nonzero result kind when applied, 0 for an unknown code. */
int save_apply_cheat(const char* code);
/* Wipe coins, unlocks, upgrades, high score, and settings back to a
 * fresh install. Writes the new defaults immediately. */
void save_reset_all(void);
#endif

// Item pricing & catalog query helpers
int shop_get_accent_price(int idx);
int shop_get_trail_price(int idx);
int shop_get_rig_price(WeaponRig rig);
int shop_get_laser_price(int idx);
int shop_get_ship_price(int idx);
int shop_get_upgrade_price(UpgradeType upg, int level);

bool shop_is_accent_owned(int idx);
bool shop_is_trail_owned(int idx);
bool shop_is_rig_owned(WeaponRig rig);
bool shop_is_laser_owned(int idx);
bool shop_is_ship_owned(int idx);
int  shop_get_upgrade_level(UpgradeType upg);

bool shop_try_purchase_accent(int idx);
bool shop_try_purchase_trail(int idx);
bool shop_try_purchase_rig(WeaponRig rig);
bool shop_try_purchase_laser(int idx);
bool shop_try_purchase_ship(int idx);
bool shop_try_purchase_upgrade(UpgradeType upg);

void shop_equip_accent(int idx);
void shop_equip_trail(int idx);
void shop_equip_rig(WeaponRig rig);
void shop_equip_laser(int idx);
void shop_equip_ship(int idx);

const char* shop_get_upgrade_name(UpgradeType upg);
const char* shop_get_upgrade_desc_line1(UpgradeType upg);
const char* shop_get_upgrade_desc_line2(UpgradeType upg, int level);

#endif

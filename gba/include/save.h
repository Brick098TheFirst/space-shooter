#ifndef SAVE_H
#define SAVE_H

#include "types.h"

void save_init_defaults(void);
void save_load(void);
void save_write(void);

// Shop helpers
int shop_get_accent_price(int idx);
int shop_get_trail_price(int idx);
int shop_get_rig_price(WeaponRig rig);
int shop_get_laser_price(int idx);
bool shop_is_accent_owned(int idx);
bool shop_is_trail_owned(int idx);
bool shop_is_rig_owned(WeaponRig rig);
bool shop_is_laser_owned(int idx);
bool shop_try_purchase_accent(int idx);
bool shop_try_purchase_trail(int idx);
bool shop_try_purchase_rig(WeaponRig rig);
bool shop_try_purchase_laser(int idx);
void shop_equip_accent(int idx);
void shop_equip_trail(int idx);
void shop_equip_rig(WeaponRig rig);
void shop_equip_laser(int idx);

#endif

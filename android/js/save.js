// Save & Settings Manager for Space Unlimited Android

import { NUM_UPGRADES, UPG_MAX_LEVEL, NUM_ACCENTS, NUM_TRAILS, NUM_RIGS, NUM_LASERS, DIFFICULTIES } from './constants.js';

const STORAGE_KEY = 'space_shooter_android_save_v1';

export class SaveManager {
  constructor() {
    this.settings = this.getDefaults();
    this.load();
  }

  getDefaults() {
    return {
      difficulty: DIFFICULTIES.PILOT,
      musicVolume: 80,
      sfxVolume: 80,
      screenShake: true,
      haptics: true,
      fpsDisplay: true,
      joystickMode: 'dynamic', // 'dynamic' (touch anywhere on left) or 'fixed' (fixed bottom left)
      accentIndex: 1, // Ion Cyan Starter
      trailIndex: 1,  // Ion Starter
      weaponRig: 0,   // Single Blaster Starter
      laserIndex: 0,  // Ion Basic Starter
      coins: 0,
      highScore: 0,
      ownedAccents: (1 << 1), // Ion Cyan
      ownedTrails: (1 << 1),  // Ion
      ownedRigs: (1 << 0),    // Single Blaster
      ownedLasers: (1 << 0),  // Ion Basic
      upgradeLevels: new Array(NUM_UPGRADES).fill(0)
    };
  }

  load() {
    try {
      const dataStr = localStorage.getItem(STORAGE_KEY);
      if (dataStr) {
        const parsed = JSON.parse(dataStr);
        this.settings = { ...this.getDefaults(), ...parsed };
        
        // Ensure valid ranges
        this.settings.accentIndex = Math.max(0, Math.min(NUM_ACCENTS - 1, this.settings.accentIndex));
        this.settings.trailIndex = Math.max(0, Math.min(NUM_TRAILS - 1, this.settings.trailIndex));
        this.settings.weaponRig = Math.max(0, Math.min(NUM_RIGS - 1, this.settings.weaponRig));
        this.settings.laserIndex = Math.max(0, Math.min(NUM_LASERS - 1, this.settings.laserIndex));
        
        if (!Array.isArray(this.settings.upgradeLevels) || this.settings.upgradeLevels.length !== NUM_UPGRADES) {
          this.settings.upgradeLevels = new Array(NUM_UPGRADES).fill(0);
        } else {
          this.settings.upgradeLevels = this.settings.upgradeLevels.map(lvl => Math.max(0, Math.min(UPG_MAX_LEVEL, lvl || 0)));
        }
        return;
      }
    } catch (e) {
      console.warn('Failed to load save from localStorage:', e);
    }
  }

  save() {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(this.settings));
    } catch (e) {
      console.warn('Failed to save to localStorage:', e);
    }
  }

  resetAll() {
    this.settings = this.getDefaults();
    this.save();
  }

  isAccentOwned(idx) {
    return (this.settings.ownedAccents & (1 << idx)) !== 0;
  }

  isTrailOwned(idx) {
    return (this.settings.ownedTrails & (1 << idx)) !== 0;
  }

  isRigOwned(rig) {
    return (this.settings.ownedRigs & (1 << rig)) !== 0;
  }

  isLaserOwned(idx) {
    return (this.settings.ownedLasers & (1 << idx)) !== 0;
  }

  getUpgradeLevel(upgId) {
    return this.settings.upgradeLevels[upgId] || 0;
  }

  equipAccent(idx) {
    if (this.isAccentOwned(idx)) {
      this.settings.accentIndex = idx;
      this.save();
      return true;
    }
    return false;
  }

  equipTrail(idx) {
    if (this.isTrailOwned(idx)) {
      this.settings.trailIndex = idx;
      this.save();
      return true;
    }
    return false;
  }

  equipRig(rig) {
    if (this.isRigOwned(rig)) {
      this.settings.weaponRig = rig;
      this.save();
      return true;
    }
    return false;
  }

  equipLaser(idx) {
    if (this.isLaserOwned(idx)) {
      this.settings.laserIndex = idx;
      this.save();
      return true;
    }
    return false;
  }

  tryBuyAccent(idx, price) {
    if (this.isAccentOwned(idx)) {
      this.equipAccent(idx);
      return true;
    }
    if (this.settings.coins >= price) {
      this.settings.coins -= price;
      this.settings.ownedAccents |= (1 << idx);
      this.settings.accentIndex = idx;
      this.save();
      return true;
    }
    return false;
  }

  tryBuyTrail(idx, price) {
    if (this.isTrailOwned(idx)) {
      this.equipTrail(idx);
      return true;
    }
    if (this.settings.coins >= price) {
      this.settings.coins -= price;
      this.settings.ownedTrails |= (1 << idx);
      this.settings.trailIndex = idx;
      this.save();
      return true;
    }
    return false;
  }

  tryBuyRig(rig, price) {
    if (this.isRigOwned(rig)) {
      this.equipRig(rig);
      return true;
    }
    if (this.settings.coins >= price) {
      this.settings.coins -= price;
      this.settings.ownedRigs |= (1 << rig);
      this.settings.weaponRig = rig;
      this.save();
      return true;
    }
    return false;
  }

  tryBuyLaser(idx, price) {
    if (this.isLaserOwned(idx)) {
      this.equipLaser(idx);
      return true;
    }
    if (this.settings.coins >= price) {
      this.settings.coins -= price;
      this.settings.ownedLasers |= (1 << idx);
      this.settings.laserIndex = idx;
      this.save();
      return true;
    }
    return false;
  }

  tryBuyUpgrade(upgId, price) {
    const curLvl = this.getUpgradeLevel(upgId);
    if (curLvl >= UPG_MAX_LEVEL) return false;
    if (this.settings.coins >= price) {
      this.settings.coins -= price;
      this.settings.upgradeLevels[upgId] = curLvl + 1;
      this.save();
      return true;
    }
    return false;
  }

  addCoins(amount) {
    this.settings.coins = Math.min(9999999, Math.max(0, this.settings.coins + amount));
    this.save();
  }

  updateHighScore(score) {
    if (score > this.settings.highScore) {
      this.settings.highScore = score;
      this.save();
      return true;
    }
    return false;
  }
}

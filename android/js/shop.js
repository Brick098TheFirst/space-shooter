// Upgrade Hangar & Shop System for Space Unlimited Android

import {
  SHIP_PAINTS,
  ENGINE_TRAILS,
  WEAPON_RIGS,
  LASER_CRYSTALS,
  TECH_UPGRADES,
  formatPrice,
  UPG_MAX_LEVEL
} from './constants.js';

export class ShopSystem {
  constructor(saveManager, audioManager, spriteRenderer) {
    this.saveManager = saveManager;
    this.audioManager = audioManager;
    this.spriteRenderer = spriteRenderer;

    this.currentCategory = 0; // 0: PAINTS, 1: TRAILS, 2: WEAPONS, 3: LASERS, 4: TECH
    this.selectedItemIndex = [1, 1, 0, 0, 0]; // per category selection
    this.animFrame = 0;

    this.testLaserBullets = [];
    this.testLaserTimer = 0;

    this.message = '';
    this.messageColor = '#ffd24a';
    this.messageTimer = 0;
  }

  setCategory(catIdx) {
    this.currentCategory = Math.max(0, Math.min(4, catIdx));
    this.testLaserBullets = [];
  }

  selectItem(itemIdx) {
    this.selectedItemIndex[this.currentCategory] = itemIdx;
  }

  setMessage(msg, color = '#ffd24a') {
    this.message = msg;
    this.messageColor = color;
    this.messageTimer = 100;
  }

  getCurrentCategoryList() {
    switch (this.currentCategory) {
      case 0: return SHIP_PAINTS;
      case 1: return ENGINE_TRAILS;
      case 2: return WEAPON_RIGS;
      case 3: return LASER_CRYSTALS;
      case 4: return TECH_UPGRADES;
      default: return [];
    }
  }

  getSelectedItem() {
    const list = this.getCurrentCategoryList();
    const idx = this.selectedItemIndex[this.currentCategory] || 0;
    return list[idx] || list[0];
  }

  buyOrEquipSelected() {
    const cat = this.currentCategory;
    const item = this.getSelectedItem();
    if (!item) return false;

    if (cat === 0) { // PAINTS
      if (this.saveManager.isAccentOwned(item.id)) {
        this.saveManager.equipAccent(item.id);
        this.setMessage('EQUIPPED PAINT!', '#50f08c');
        this.audioManager.playSfx('pickup');
        return true;
      }
      const ok = this.saveManager.tryBuyAccent(item.id, item.price);
      if (ok) {
        this.setMessage('PURCHASED PAINT!', '#ffd24a');
        this.audioManager.playSfx('pickup');
        return true;
      }
      this.setMessage(`NEED ${formatPrice(item.price)} COINS!`, '#ff3c3c');
      return false;
    }

    if (cat === 1) { // TRAILS
      if (this.saveManager.isTrailOwned(item.id)) {
        this.saveManager.equipTrail(item.id);
        this.setMessage('EQUIPPED TRAIL!', '#50f08c');
        this.audioManager.playSfx('pickup');
        return true;
      }
      const ok = this.saveManager.tryBuyTrail(item.id, item.price);
      if (ok) {
        this.setMessage('PURCHASED TRAIL!', '#ffd24a');
        this.audioManager.playSfx('pickup');
        return true;
      }
      this.setMessage(`NEED ${formatPrice(item.price)} COINS!`, '#ff3c3c');
      return false;
    }

    if (cat === 2) { // WEAPONS
      if (this.saveManager.isRigOwned(item.id)) {
        this.saveManager.equipRig(item.id);
        this.setMessage('EQUIPPED WEAPON!', '#50f08c');
        this.audioManager.playSfx('pickup');
        return true;
      }
      const ok = this.saveManager.tryBuyRig(item.id, item.price);
      if (ok) {
        this.setMessage('PURCHASED WEAPON!', '#ffd24a');
        this.audioManager.playSfx('pickup');
        return true;
      }
      this.setMessage(`NEED ${formatPrice(item.price)} COINS!`, '#ff3c3c');
      return false;
    }

    if (cat === 3) { // LASERS
      if (this.saveManager.isLaserOwned(item.id)) {
        this.saveManager.equipLaser(item.id);
        this.setMessage('EQUIPPED LASER!', '#50f08c');
        this.audioManager.playSfx('pickup');
        return true;
      }
      const ok = this.saveManager.tryBuyLaser(item.id, item.price);
      if (ok) {
        this.setMessage('PURCHASED LASER!', '#ffd24a');
        this.audioManager.playSfx('pickup');
        return true;
      }
      this.setMessage(`NEED ${formatPrice(item.price)} COINS!`, '#ff3c3c');
      return false;
    }

    if (cat === 4) { // TECH
      const curLvl = this.saveManager.getUpgradeLevel(item.id);
      if (curLvl >= UPG_MAX_LEVEL) {
        this.setMessage('MAX LEVEL REACHED!', '#ffd24a');
        return false;
      }
      const price = item.prices[curLvl];
      const ok = this.saveManager.tryBuyUpgrade(item.id, price);
      if (ok) {
        this.setMessage('UPGRADED TECH! +POWER', '#ffd24a');
        this.audioManager.playSfx('pickup');
        return true;
      }
      this.setMessage(`NEED ${formatPrice(price)} COINS!`, '#ff3c3c');
      return false;
    }

    return false;
  }

  update() {
    this.animFrame++;
    if (this.messageTimer > 0) this.messageTimer--;

    // Update test lasers in preview chamber
    this.testLaserTimer++;
    if (this.testLaserTimer >= 18) {
      this.testLaserTimer = 0;
      const rig = (this.currentCategory === 2) ? this.getSelectedItem().id : this.saveManager.settings.weaponRig;
      const laser = (this.currentCategory === 3) ? this.getSelectedItem().id : this.saveManager.settings.laserIndex;
      this.spawnTestLaser(rig, laser);
    }

    for (let i = this.testLaserBullets.length - 1; i >= 0; i--) {
      const b = this.testLaserBullets[i];
      b.y += b.vy;
      b.x += b.vx;
      if (b.y < 0) {
        this.testLaserBullets.splice(i, 1);
      }
    }
  }

  spawnTestLaser(rigId, laserId) {
    const isHeavy = (rigId === 3 || rigId === 4 || rigId === 5 || rigId === 6 || rigId === 7);
    const speed = -3.5;

    switch (rigId) {
      case 0: // Single
        this.testLaserBullets.push({ x: 0, y: -6, vx: 0, vy: speed, heavy: false, laser: laserId });
        break;
      case 1: // Twin
        this.testLaserBullets.push({ x: -4, y: -6, vx: 0, vy: speed, heavy: false, laser: laserId });
        this.testLaserBullets.push({ x: 4, y: -6, vx: 0, vy: speed, heavy: false, laser: laserId });
        break;
      case 2: // Spread
        this.testLaserBullets.push({ x: 0, y: -6, vx: 0, vy: speed, heavy: false, laser: laserId });
        this.testLaserBullets.push({ x: -4, y: -4, vx: -0.8, vy: speed * 0.9, heavy: false, laser: laserId });
        this.testLaserBullets.push({ x: 4, y: -4, vx: 0.8, vy: speed * 0.9, heavy: false, laser: laserId });
        break;
      case 3: // Focused
        this.testLaserBullets.push({ x: 0, y: -8, vx: 0, vy: speed * 1.2, heavy: true, laser: laserId });
        break;
      case 4: // Triple
        this.testLaserBullets.push({ x: -6, y: -6, vx: 0, vy: speed, heavy: false, laser: laserId });
        this.testLaserBullets.push({ x: 0, y: -8, vx: 0, vy: speed * 1.1, heavy: true, laser: laserId });
        this.testLaserBullets.push({ x: 6, y: -6, vx: 0, vy: speed, heavy: false, laser: laserId });
        break;
      case 5: // Plasma
        this.testLaserBullets.push({ x: -5, y: -7, vx: -0.3, vy: speed, heavy: true, laser: laserId });
        this.testLaserBullets.push({ x: 5, y: -7, vx: 0.3, vy: speed, heavy: true, laser: laserId });
        break;
      case 6: // Quantum
        this.testLaserBullets.push({ x: -4, y: -8, vx: 0, vy: speed * 1.2, heavy: true, laser: laserId });
        this.testLaserBullets.push({ x: 4, y: -8, vx: 0, vy: speed * 1.2, heavy: true, laser: laserId });
        this.testLaserBullets.push({ x: 0, y: -10, vx: 0, vy: speed * 1.3, heavy: true, laser: laserId });
        break;
      case 7: // Nova
        this.testLaserBullets.push({ x: 0, y: -10, vx: 0, vy: speed * 1.4, heavy: true, laser: laserId });
        this.testLaserBullets.push({ x: -5, y: -8, vx: -0.5, vy: speed * 1.3, heavy: true, laser: laserId });
        this.testLaserBullets.push({ x: 5, y: -8, vx: 0.5, vy: speed * 1.3, heavy: true, laser: laserId });
        this.testLaserBullets.push({ x: -9, y: -6, vx: -1.0, vy: speed * 1.1, heavy: true, laser: laserId });
        this.testLaserBullets.push({ x: 9, y: -6, vx: 1.0, vy: speed * 1.1, heavy: true, laser: laserId });
        break;
      default:
        this.testLaserBullets.push({ x: 0, y: -6, vx: 0, vy: speed, heavy: isHeavy, laser: laserId });
        break;
    }
  }

  drawShipPreview(ctx, centerX, centerY) {
    const previewAccent = (this.currentCategory === 0)
      ? this.getSelectedItem().id
      : this.saveManager.settings.accentIndex;

    const previewTrail = (this.currentCategory === 1)
      ? this.getSelectedItem().id
      : this.saveManager.settings.trailIndex;

    const shipX = centerX - 10;
    const shipY = centerY - 8;

    // Draw test firing lasers
    for (const b of this.testLaserBullets) {
      const lx = centerX + b.x;
      const ly = shipY + b.y;
      this.spriteRenderer.drawLaser(ctx, lx, ly, b.heavy, b.laser, this.animFrame, false);
    }

    // Draw active engine exhaust particles
    const trailData = ENGINE_TRAILS[previewTrail] || ENGINE_TRAILS[1];
    let trailRgb = trailData.rgb || [42, 214, 255];

    if (previewTrail === 7) { // Rainbow Trail
      const phase = (this.animFrame >> 2) % 7;
      const rainbowCols = [[255,70,70], [255,120,56], [255,210,74], [102,255,184], [42,214,255], [188,92,255], [255,90,230]];
      trailRgb = rainbowCols[phase];
    }

    const flameLen = ((this.animFrame % 6) < 3) ? 5 : 3;
    ctx.fillStyle = `rgb(${trailRgb[0]}, ${trailRgb[1]}, ${trailRgb[2]})`;
    ctx.fillRect(Math.floor(centerX - 2), Math.floor(shipY + 16), 4, flameLen);
    ctx.fillStyle = 'rgba(255, 255, 255, 0.9)';
    ctx.fillRect(Math.floor(centerX - 1), Math.floor(shipY + 16), 2, Math.max(1, flameLen - 2));

    // Draw Ship
    this.spriteRenderer.drawShip(ctx, shipX, shipY, previewAccent, this.animFrame);
  }
}

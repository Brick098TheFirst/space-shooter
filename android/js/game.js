// Core Gameplay Engine for Space Unlimited Android
// Features Widescreen, 90/120Hz High-Refresh Variable Rate Loop, Authentic GBA Physics & Scaling

import {
  VIRTUAL_WIDTH,
  VIRTUAL_HEIGHT,
  TARGET_PHYSICS_HZ,
  FIXED_TIMESTEP,
  DIFFICULTIES,
  DIFFICULTY_INFO,
  TECH_UPGRADES,
  WEAPON_RIGS,
  LASER_CRYSTALS,
  formatPrice
} from './constants.js';

export class GameEngine {
  constructor(canvas, saveManager, audioManager, touchControls, spriteRenderer, starfield) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d', { alpha: false });
    this.saveManager = saveManager;
    this.audioManager = audioManager;
    this.touchControls = touchControls;
    this.spriteRenderer = spriteRenderer;
    this.starfield = starfield;

    this.width = VIRTUAL_WIDTH;
    this.height = VIRTUAL_HEIGHT;

    // High refresh rate timing
    this.lastTime = performance.now();
    this.accumulator = 0;
    this.fps = 0;
    this.frameCount = 0;
    this.fpsTimer = performance.now();
    this.displayHz = 90; // estimated display refresh rate

    // Game state
    this.running = false;
    this.paused = false;
    this.isGameOver = false;
    this.isNewHighScore = false;
    this.gameFrame = 0;

    // Entities
    this.player = this.createPlayer();
    this.asteroids = [];
    this.bullets = [];
    this.drones = [];
    this.powerups = [];
    this.particles = [];
    this.explosions = [];

    this.score = 0;
    this.wave = 0;
    this.combo = 1;
    this.comboTimer = 0;
    this.waveBannerTimer = 0;
    this.shakeTimer = 0;
    this.shakeX = 0;
    this.shakeY = 0;
    this.intermissionTimer = 30;

    this.onGameOver = null;
  }

  createPlayer() {
    return {
      x: this.width / 2,
      y: this.height - 28,
      vx: 0,
      vy: 0,
      radius: 6,
      lives: 3,
      shieldCharges: 0,
      fireCooldown: 0,
      dashCooldown: 0,
      dashRemaining: 0,
      dashDirX: 0,
      dashDirY: -1,
      invulnerableTimer: 90,
      rapidFireTimer: 0
    };
  }

  // --- Upgrade Buff Calculations ---
  getEngineMult() {
    const lvl = this.saveManager.getUpgradeLevel(0);
    const tbl = [180/256, 220/256, 256/256, 320/256, 410/256, 512/256];
    return tbl[Math.min(5, lvl)];
  }

  getFireRateCdMult() {
    const lvl = this.saveManager.getUpgradeLevel(1);
    const tbl = [256/256, 210/256, 165/256, 125/256, 95/256, 75/256];
    return tbl[Math.min(5, lvl)];
  }

  getDamageBonus() {
    return this.saveManager.getUpgradeLevel(2); // +0..+5
  }

  getLaserBonus() {
    const crystal = LASER_CRYSTALS[this.saveManager.settings.laserIndex] || LASER_CRYSTALS[0];
    return crystal.bonusDmg;
  }

  getMaxShields() {
    const lvl = this.saveManager.getUpgradeLevel(3);
    const maxs = [2, 3, 4, 5, 6, 6];
    return maxs[Math.min(5, lvl)];
  }

  getStartShields() {
    const lvl = this.saveManager.getUpgradeLevel(3);
    const starts = [0, 1, 1, 2, 2, 3];
    return starts[Math.min(5, lvl)];
  }

  getMaxLives() {
    const lvl = this.saveManager.getUpgradeLevel(4);
    const lives = [2, 3, 4, 5, 6, 7];
    return lives[Math.min(5, lvl)];
  }

  getMaxDashCooldown() {
    const lvl = this.saveManager.getUpgradeLevel(5);
    const cds = [84, 66, 52, 40, 30, 24]; // 1.4s -> 0.4s
    return cds[Math.min(5, lvl)];
  }

  getDashInvuln() {
    const lvl = this.saveManager.getUpgradeLevel(5);
    return 16 + lvl * 3;
  }

  getCoinMult() {
    const lvl = this.saveManager.getUpgradeLevel(6);
    return 1.0 + lvl * 0.35; // 100% -> 275%
  }

  getRapidDuration() {
    const lvl = this.saveManager.getUpgradeLevel(7);
    const durs = [480, 660, 840, 1080, 1320, 1560];
    return durs[Math.min(5, lvl)];
  }

  getDiffSpeedMult() {
    const diff = this.saveManager.settings.difficulty;
    if (diff === DIFFICULTIES.CADET) return 0.86;
    if (diff === DIFFICULTIES.ACE) return 1.21;
    return 1.0;
  }

  // --- Game Lifecycle ---
  start() {
    this.isGameOver = false;
    this.isNewHighScore = false;
    this.score = 0;
    this.wave = 0;
    this.combo = 1;
    this.comboTimer = 0;
    this.waveBannerTimer = 0;
    this.shakeTimer = 0;
    this.intermissionTimer = 30;
    this.gameFrame = 0;

    this.asteroids = [];
    this.bullets = [];
    this.drones = [];
    this.powerups = [];
    this.particles = [];
    this.explosions = [];

    this.player = this.createPlayer();

    // Hull & Shield starting setup
    let startLives = this.getMaxLives();
    let startShields = this.getStartShields();

    const diff = this.saveManager.settings.difficulty;
    if (diff === DIFFICULTIES.CADET) {
      startLives += 1;
      startShields += 1;
    } else if (diff === DIFFICULTIES.ACE && startLives > 2) {
      startLives -= 1;
    }

    this.player.lives = startLives;
    this.player.shieldCharges = Math.min(this.getMaxShields(), startShields);
    this.player.invulnerableTimer = 90;

    this.running = true;
    this.paused = false;
    this.audioManager.playBgm('game');
  }

  stop() {
    this.running = false;
  }

  setPaused(paused) {
    this.paused = paused;
    if (paused) {
      this.saveManager.save();
    }
  }

  // --- Particles & Explosions ---
  spawnParticle(x, y, vx, vy, colorHex, life) {
    if (this.particles.length > 80) this.particles.shift();
    this.particles.push({ x, y, vx, vy, color: colorHex, life, maxLife: life });
  }

  triggerExplosion(x, y) {
    if (this.explosions.length > 12) this.explosions.shift();
    this.explosions.push({ x, y, frame: 0, timer: 0 });

    for (let p = 0; p < 12; p++) {
      const angle = (p / 12) * Math.PI * 2;
      const spd = 0.8 + Math.random() * 1.5;
      const vx = Math.cos(angle) * spd;
      const vy = Math.sin(angle) * spd;
      const cols = ['#ff4646', '#ff7838', '#ffd24a', '#ffffff', '#23d6ff'];
      const col = cols[Math.floor(Math.random() * cols.length)];
      this.spawnParticle(x, y, vx, vy, col, 12 + Math.floor(Math.random() * 14));
    }

    if (this.saveManager.settings.screenShake) {
      this.shakeTimer = 14;
    }
    this.audioManager.playSfx('explosion');
    this.touchControls.vibrate(40);
  }

  emitEngineParticle() {
    const trailIdx = this.saveManager.settings.trailIndex;
    let col = '#2ad6ff';

    if (trailIdx === 7) { // Rainbow Trail
      const rainbowCols = ['#ff4646', '#ff7838', '#ffd24a', '#66ffb8', '#2ad6ff', '#bc5cff', '#ff5ae6'];
      col = rainbowCols[(this.gameFrame >> 2) % 7];
    } else {
      const trails = ['#ff7838', '#2ad6ff', '#bc5cff', '#66ffb8', '#ffd24a', '#ff3c3c', '#826eaa'];
      col = trails[trailIdx] || '#2ad6ff';
    }

    const px = this.player.x + (Math.random() * 6 - 3);
    const py = this.player.y + 8;
    const vx = (Math.random() - 0.5) * 0.8;
    const vy = 1.2 + Math.random() * 1.0;
    this.spawnParticle(px, py, vx, vy, col, 8 + Math.floor(Math.random() * 6));
  }

  emitEnemyEngineParticle(drone) {
    const px = drone.x + (Math.random() * 6 - 3);
    const py = drone.y - 8;
    const vx = (Math.random() - 0.5) * 0.8;
    const vy = -(1.0 + Math.random() * 0.8);
    this.spawnParticle(px, py, vx, vy, '#ff3c3c', 6 + Math.floor(Math.random() * 5));
  }

  // --- Rewards & Scoring ---
  awardScore(pts) {
    this.score += pts * this.combo;
    if (this.combo < 15) this.combo++;
    this.comboTimer = 160;
  }

  awardCoins(baseAmount) {
    const mult = this.getCoinMult();
    const earned = Math.max(1, Math.floor(baseAmount * mult));
    this.saveManager.addCoins(earned);
  }

  trySpawnPowerup(x, y, chancePct = 5) {
    if (Math.random() * 100 >= chancePct) return;
    if (this.powerups.length >= 6) return;

    const roll = Math.random() * 100;
    let type = 0; // Shield
    if (roll < 40) type = 0;
    else if (roll < 80) type = 1; // Rapid Fire
    else type = 2; // Repair

    this.powerups.push({ x, y, vy: 0.55, type });
  }

  damagePlayer() {
    if (this.player.invulnerableTimer > 0) return;

    if (this.player.shieldCharges > 0) {
      this.player.shieldCharges--;
      this.player.invulnerableTimer = 60;
      this.triggerExplosion(this.player.x, this.player.y);
    } else {
      this.player.lives--;
      this.player.invulnerableTimer = 90 + this.getDashInvuln();
      this.player.x = this.width / 2;
      this.player.y = this.height - 28;
      this.triggerExplosion(this.player.x, this.player.y);

      if (this.player.lives <= 0) {
        this.isGameOver = true;
        this.isNewHighScore = this.saveManager.updateHighScore(this.score);
        this.saveManager.save();
        if (this.onGameOver) this.onGameOver(this.score, this.isNewHighScore);
      }
    }

    this.combo = 1;
    this.comboTimer = 0;
    if (this.saveManager.settings.screenShake) this.shakeTimer = 20;
    this.touchControls.vibrate([60, 30, 90]);
  }

  // --- Asteroids & Enemies ---
  spawnAsteroid(type, x, y, vx, vy) {
    if (this.asteroids.length >= 28) return;
    let radius = 12;
    let hp = 1;

    if (type === 0) { // Large
      radius = 12;
      hp = 2 + Math.floor(this.wave / 5);
    } else if (type === 1 || type === 2) { // Med A / Med B
      radius = 8;
      hp = 1 + Math.floor(this.wave / 8);
    } else if (type === 3) { // Small
      radius = 5;
      hp = 1;
    } else { // Tiny
      radius = 3;
      hp = 1;
    }

    this.asteroids.push({ type, x, y, vx, vy, radius, hp });
  }

  destroyAsteroid(idx, award = true) {
    const a = this.asteroids[idx];
    if (!a) return;
    this.triggerExplosion(a.x, a.y);
    this.asteroids.splice(idx, 1);

    if (award) {
      let pts = 20;
      let coins = 4;
      if (a.type === 0) { pts = 60; coins = 20; }
      else if (a.type === 1 || a.type === 2) { pts = 35; coins = 12; }
      else if (a.type === 3) { pts = 20; coins = 6; }

      this.awardScore(pts);
      this.awardCoins(coins);

      const mult = this.getDiffSpeedMult() + this.wave * 0.05;
      if (a.type === 0) { // Large splits into Med A + Med B
        const spd = 0.75 * mult;
        this.spawnAsteroid(1, a.x - 6, a.y, -spd, spd);
        this.spawnAsteroid(2, a.x + 6, a.y, spd, spd);
      } else if (a.type === 1 || a.type === 2) { // Med splits into Small + Tiny
        const spd = 0.95 * mult;
        this.spawnAsteroid(3, a.x - 4, a.y, -spd, spd);
        this.spawnAsteroid(4, a.x + 4, a.y, spd, spd);
      }

      this.trySpawnPowerup(a.x, a.y, 5);
    }
  }

  destroyDrone(idx, award = true) {
    const d = this.drones[idx];
    if (!d) return;
    this.triggerExplosion(d.x, d.y);
    this.drones.splice(idx, 1);

    if (award) {
      this.awardScore(120);
      this.awardCoins(45);
      this.trySpawnPowerup(d.x, d.y, 12);
    }
  }

  beginWave() {
    this.wave++;
    this.waveBannerTimer = 120;

    if (this.wave > 1) {
      this.awardCoins(this.wave * 30);
    }

    const diff = this.saveManager.settings.difficulty;
    let diffExtra = (diff === DIFFICULTIES.ACE) ? 3 : (diff === DIFFICULTIES.CADET ? -1 : 0);

    let astCount = 4 + this.wave * 2 + (this.wave > 2 ? this.wave : 0) + diffExtra;
    astCount = Math.max(3, Math.min(22, astCount));

    const diffMult = this.getDiffSpeedMult();
    const mult = diffMult + this.wave * 0.07;

    for (let i = 0; i < astCount; i++) {
      const x = Math.random() * (this.width - 40) + 20;
      const y = -(Math.random() * 80 + 10 + i * 14);
      const vx = ((Math.random() * 1.2) - 0.6) * mult;
      const vy = (0.55 + Math.random() * 0.65 + this.wave * 0.04) * mult;
      const isLarge = (Math.random() * 100) < (15 + this.wave * 7);
      const type = isLarge ? 0 : (Math.random() < 0.5 ? 1 : 2);
      this.spawnAsteroid(type, x, y, vx, vy);
    }

    // Drones from wave 2 onwards
    if (this.wave >= 2) {
      let droneCount = Math.floor(this.wave / 2) + 1;
      if (diff === DIFFICULTIES.ACE) droneCount++;
      droneCount = Math.min(8, droneCount);

      for (let i = 0; i < droneCount; i++) {
        const spacing = (this.width - 60) / (droneCount > 1 ? droneCount - 1 : 1);
        const x = 30 + (droneCount > 1 ? i * spacing : (this.width / 2 - 30));
        const y = -(20 + i * 28);
        const vy = (0.45 + this.wave * 0.03) * mult;
        const baseCd = Math.max(20, 70 - this.wave * 4);

        this.drones.push({
          x,
          y,
          vx: 0,
          vy,
          shootTimer: Math.floor(Math.random() * 40) + baseCd,
          burstTimer: 0,
          burstShots: 0,
          phase: Math.random() * 100,
          hp: Math.min(6, 2 + Math.floor(this.wave / 3) + (diff === DIFFICULTIES.ACE ? 1 : 0))
        });
      }
    }
  }

  // --- Weapons & Firing ---
  firePlayerWeapon() {
    const rapid = (this.player.rapidFireTimer > 0);
    const px = this.player.x;
    const py = this.player.y;
    const dmgBonus = this.getDamageBonus() + this.getLaserBonus();
    const isOmega = (this.saveManager.settings.laserIndex === 11);

    const rigId = this.saveManager.settings.weaponRig;
    const rigData = WEAPON_RIGS[rigId] || WEAPON_RIGS[0];
    const frMult = this.getFireRateCdMult();

    let cooldown = Math.floor((rigData.baseCd * frMult) * 0.5); // scaled for 120Hz
    if (rapid) cooldown = Math.floor((cooldown * 2) / 5);
    if (isOmega && cooldown > 2) cooldown -= 1;
    cooldown = Math.max(2, cooldown);

    const bulletSpeed = -3.8;

    const addBullet = (bx, by, vx, vy, dmg, heavy) => {
      if (this.bullets.length >= 64) return;
      this.bullets.push({
        x: bx,
        y: by,
        vx,
        vy,
        radius: heavy ? 3 : 2,
        damage: dmg,
        life: 70,
        enemy: false,
        heavy
      });
    };

    switch (rigId) {
      case 0: // Single (starter weak, no pierce)
        addBullet(px, py - 8, 0, bulletSpeed, 1 + dmgBonus, false);
        break;
      case 1: // Twin
        addBullet(px - 4, py - 6, 0, bulletSpeed, 1 + dmgBonus, false);
        addBullet(px + 4, py - 6, 0, bulletSpeed, 1 + dmgBonus, false);
        break;
      case 2: // Spread
        addBullet(px, py - 6, 0, bulletSpeed, 1 + dmgBonus, false);
        addBullet(px - 4, py - 4, -0.8, bulletSpeed * 0.9, 1 + dmgBonus, false);
        addBullet(px + 4, py - 4, 0.8, bulletSpeed * 0.9, 1 + dmgBonus, false);
        break;
      case 3: // Focused
        addBullet(px, py - 8, 0, bulletSpeed * 1.1, 2 + dmgBonus, true);
        break;
      case 4: // Triple
        addBullet(px - 6, py - 6, 0, bulletSpeed, 1 + dmgBonus, false);
        addBullet(px, py - 8, 0, bulletSpeed * 1.1, 2 + dmgBonus, true);
        addBullet(px + 6, py - 6, 0, bulletSpeed, 1 + dmgBonus, false);
        break;
      case 5: // Plasma
        addBullet(px - 5, py - 7, -0.3, bulletSpeed, 3 + dmgBonus, true);
        addBullet(px + 5, py - 7, 0.3, bulletSpeed, 3 + dmgBonus, true);
        break;
      case 6: // Quantum
        addBullet(px - 4, py - 8, 0, bulletSpeed * 1.2, 4 + dmgBonus, true);
        addBullet(px + 4, py - 8, 0, bulletSpeed * 1.2, 4 + dmgBonus, true);
        addBullet(px, py - 10, 0, bulletSpeed * 1.3, 4 + dmgBonus, true);
        break;
      case 7: // Nova (God tier)
        const d = 5 + dmgBonus + (isOmega ? 2 : 0);
        addBullet(px, py - 10, 0, bulletSpeed * 1.4, d, true);
        addBullet(px - 5, py - 8, -0.6, bulletSpeed * 1.3, d, true);
        addBullet(px + 5, py - 8, 0.6, bulletSpeed * 1.3, d, true);
        addBullet(px - 9, py - 6, -1.2, bulletSpeed * 1.1, d - 1, true);
        addBullet(px + 9, py - 6, 1.2, bulletSpeed * 1.1, d - 1, true);
        break;
      default:
        addBullet(px, py - 6, 0, bulletSpeed, 1 + dmgBonus, false);
        break;
    }

    this.player.fireCooldown = cooldown;
    this.audioManager.playSfx('laser');
    this.touchControls.vibrate(12);
  }

  // --- Fixed Physics Update Step (Deterministic @ 120Hz) ---
  updatePhysics() {
    this.gameFrame++;
    this.starfield.update(1.0);

    if (this.shakeTimer > 0) {
      this.shakeTimer--;
      this.shakeX = (Math.random() * 4 - 2);
      this.shakeY = (Math.random() * 4 - 2);
    } else {
      this.shakeX = 0;
      this.shakeY = 0;
    }

    if (this.waveBannerTimer > 0) this.waveBannerTimer--;

    // Update Particles
    for (let i = this.particles.length - 1; i >= 0; i--) {
      const p = this.particles[i];
      p.x += p.vx;
      p.y += p.vy;
      p.life--;
      if (p.life <= 0) this.particles.splice(i, 1);
    }

    // Update Explosions
    for (let i = this.explosions.length - 1; i >= 0; i--) {
      const ex = this.explosions[i];
      ex.timer++;
      if (ex.timer >= 3) {
        ex.timer = 0;
        ex.frame++;
        if (ex.frame >= 9) this.explosions.splice(i, 1);
      }
    }

    if (this.isGameOver) return;

    // Combo Timer Decay
    if (this.comboTimer > 0) {
      this.comboTimer--;
      if (this.comboTimer === 0) this.combo = 1;
    }

    // Player Timers
    if (this.player.fireCooldown > 0) this.player.fireCooldown--;
    if (this.player.dashCooldown > 0) this.player.dashCooldown--;
    if (this.player.dashRemaining > 0) this.player.dashRemaining--;
    if (this.player.invulnerableTimer > 0) this.player.invulnerableTimer--;
    if (this.player.rapidFireTimer > 0) this.player.rapidFireTimer--;

    // Poll Input
    const input = this.touchControls.pollInput();

    if (input.pause) {
      this.setPaused(!this.paused);
      return;
    }

    // Dash Trigger
    const maxDashCd = this.getMaxDashCooldown();
    if (input.dash && this.player.dashCooldown === 0) {
      if (Math.hypot(input.vx, input.vy) > 0.1) {
        this.player.dashDirX = input.vx;
        this.player.dashDirY = input.vy;
      } else {
        this.player.dashDirX = 0;
        this.player.dashDirY = -1;
      }
      this.player.dashRemaining = 12 + this.saveManager.getUpgradeLevel(0);
      this.player.dashCooldown = maxDashCd;
      this.player.invulnerableTimer = this.getDashInvuln();

      for (let b = 0; b < 10; b++) this.emitEngineParticle();
      this.touchControls.vibrate(30);
    }

    // Update Dash Cooldown Progress in Touch Controls
    const dashReadyFraction = (maxDashCd - this.player.dashCooldown) / maxDashCd;
    this.touchControls.updateDashCooldown(dashReadyFraction);

    // Player Movement with Engine Upgrade
    const engMult = this.getEngineMult();
    const baseSpeed = 1.35 * engMult;
    const dashExtra = (2.2 + this.saveManager.getUpgradeLevel(0) * 0.2) * engMult;
    const moveSpeed = (this.player.dashRemaining > 0) ? (baseSpeed + dashExtra) : baseSpeed;

    const dirX = (this.player.dashRemaining > 0) ? this.player.dashDirX : input.vx;
    const dirY = (this.player.dashRemaining > 0) ? this.player.dashDirY : input.vy;

    this.player.x += dirX * moveSpeed;
    this.player.y += dirY * moveSpeed;

    // Boundaries
    this.player.x = Math.max(12, Math.min(this.width - 12, this.player.x));
    this.player.y = Math.max(24, Math.min(this.height - 12, this.player.y));

    if (Math.hypot(dirX, dirY) > 0.1 || this.player.dashRemaining > 0) {
      if (Math.random() < 0.55) this.emitEngineParticle();
    }

    // Firing
    if (input.fire && this.player.fireCooldown === 0) {
      this.firePlayerWeapon();
    }

    // Bullets Update
    for (let i = this.bullets.length - 1; i >= 0; i--) {
      const b = this.bullets[i];
      b.x += b.vx;
      b.y += b.vy;
      b.life--;
      if (b.life <= 0 || b.x < -16 || b.x > this.width + 16 || b.y < -24 || b.y > this.height + 24) {
        this.bullets.splice(i, 1);
      }
    }

    // Asteroids Update
    for (const a of this.asteroids) {
      a.x += a.vx;
      a.y += a.vy;
      if (a.x < -a.radius) a.x = this.width + a.radius;
      if (a.x > this.width + a.radius) a.x = -a.radius;
      if (a.y > this.height + a.radius) {
        a.y = -a.radius - 10;
        a.x = Math.random() * (this.width - 30) + 15;
      }
    }

    // Drones Update & Burst Firing
    const mult = this.getDiffSpeedMult() + this.wave * 0.05;
    for (const drone of this.drones) {
      if (drone.y < 32) {
        drone.y += drone.vy;
      } else {
        drone.phase += 0.04;
        const wobble = Math.sin(drone.phase) * 6;
        const targetX = this.player.x + wobble;
        const dx = targetX - drone.x;
        const trackStep = 0.65 * mult;

        if (dx > trackStep) drone.x += trackStep;
        else if (dx < -trackStep) drone.x -= trackStep;
        else drone.x = targetX;

        drone.x = Math.max(12, Math.min(this.width - 12, drone.x));
      }

      if ((this.gameFrame % 4) === 0) this.emitEnemyEngineParticle(drone);
      if (drone.y <= 20) continue;

      if (drone.burstShots > 0) {
        drone.burstTimer--;
        if (drone.burstTimer <= 0) {
          const cannonX = drone.x + ((drone.burstShots % 2 === 0) ? -4 : 4);
          const bulletSpeed = (1.4 + this.wave * 0.06) * mult;

          if (this.bullets.length < 64) {
            this.bullets.push({
              x: cannonX,
              y: drone.y + 9,
              vx: 0,
              vy: bulletSpeed,
              radius: 2,
              damage: 1,
              life: 140,
              enemy: true,
              heavy: false
            });
            this.audioManager.playSfx('laser');
          }

          drone.burstShots--;
          drone.burstTimer = 5 + Math.floor(Math.random() * 5);
          if (drone.burstShots === 0) {
            const baseCd = Math.max(18, 65 - this.wave * 3);
            drone.shootTimer = Math.floor(Math.random() * 50 + baseCd) / mult;
          }
        }
      } else {
        drone.shootTimer--;
        if (drone.shootTimer <= 0) {
          drone.burstShots = 2 + Math.floor(Math.random() * 3) + Math.floor(this.wave / 4);
          drone.burstShots = Math.min(5, drone.burstShots);
          drone.burstTimer = 0;
        }
      }
    }

    // Powerups Magnetic Attraction
    const scavLvl = this.saveManager.getUpgradeLevel(6);
    const magDistSq = (scavLvl > 0) ? (30 + scavLvl * 30) ** 2 : 0;

    for (let i = this.powerups.length - 1; i >= 0; i--) {
      const pow = this.powerups[i];
      pow.y += pow.vy;

      if (magDistSq > 0) {
        const dx = this.player.x - pow.x;
        const dy = this.player.y - pow.y;
        const dsq = dx * dx + dy * dy;
        if (dsq < magDistSq && dsq > 9) {
          const pull = (0.4 + scavLvl * 0.25);
          pow.x += (dx > 0 ? pull : -pull);
          pow.y += (dy > 0 ? pull : -pull);
        }
      }

      if (pow.y > this.height + 10) {
        this.powerups.splice(i, 1);
      }
    }

    // Bullet Collisions
    const px = this.player.x;
    const py = this.player.y;

    for (let bIdx = this.bullets.length - 1; bIdx >= 0; bIdx--) {
      const b = this.bullets[bIdx];
      if (!b) continue;

      if (b.enemy) {
        if (this.player.invulnerableTimer === 0) {
          const distSq = (b.x - px) ** 2 + (b.y - py) ** 2;
          if (distSq <= (b.radius + 6) ** 2) {
            this.bullets.splice(bIdx, 1);
            this.damagePlayer();
          }
        }
        continue;
      }

      // Player Bullet vs Asteroids
      let consumed = false;
      for (let aIdx = this.asteroids.length - 1; aIdx >= 0; aIdx--) {
        const a = this.asteroids[aIdx];
        const distSq = (b.x - a.x) ** 2 + (b.y - a.y) ** 2;
        if (distSq <= (b.radius + a.radius) ** 2) {
          a.hp -= b.damage;
          const pierce = b.heavy && (this.saveManager.settings.weaponRig !== 0);
          if (!pierce || (a.type === 0 && this.saveManager.settings.weaponRig !== 7)) {
            this.bullets.splice(bIdx, 1);
            consumed = true;
          }
          if (a.hp <= 0) {
            this.destroyAsteroid(aIdx, true);
          }
          break;
        }
      }
      if (consumed) continue;

      // Player Bullet vs Drones
      for (let dIdx = this.drones.length - 1; dIdx >= 0; dIdx--) {
        const d = this.drones[dIdx];
        const distSq = (b.x - d.x) ** 2 + (b.y - d.y) ** 2;
        if (distSq <= (b.radius + 8) ** 2) {
          d.hp -= b.damage;
          this.bullets.splice(bIdx, 1);
          if (d.hp <= 0) {
            this.destroyDrone(dIdx, true);
          }
          break;
        }
      }
    }

    // Direct Body Collisions (Player vs Asteroid / Drone)
    if (this.player.invulnerableTimer === 0) {
      for (let aIdx = this.asteroids.length - 1; aIdx >= 0; aIdx--) {
        const a = this.asteroids[aIdx];
        const distSq = (px - a.x) ** 2 + (py - a.y) ** 2;
        if (distSq <= (5 + a.radius) ** 2) {
          this.destroyAsteroid(aIdx, false);
          this.damagePlayer();
          break;
        }
      }

      for (let dIdx = this.drones.length - 1; dIdx >= 0; dIdx--) {
        const d = this.drones[dIdx];
        const distSq = (px - d.x) ** 2 + (py - d.y) ** 2;
        if (distSq <= (5 + 8) ** 2) {
          this.destroyDrone(dIdx, false);
          this.damagePlayer();
          break;
        }
      }
    }

    // Powerup Pickups
    const maxShields = this.getMaxShields();
    const maxLives = this.getMaxLives() + 1;
    const rapidDur = this.getRapidDuration();

    for (let pIdx = this.powerups.length - 1; pIdx >= 0; pIdx--) {
      const pow = this.powerups[pIdx];
      const distSq = (px - pow.x) ** 2 + (py - pow.y) ** 2;
      if (distSq <= (6 + 6) ** 2) {
        if (pow.type === 0) { // Shield
          if (this.player.shieldCharges < maxShields) this.player.shieldCharges++;
        } else if (pow.type === 1) { // Rapid Fire
          this.player.rapidFireTimer = rapidDur;
        } else if (pow.type === 2) { // Repair
          if (this.player.lives < maxLives) this.player.lives++;
        }

        this.score += 75;
        this.awardCoins(15);
        this.powerups.splice(pIdx, 1);
        this.audioManager.playSfx('pickup');
        this.touchControls.vibrate(20);
      }
    }

    // Wave Progression
    const activeHostiles = this.asteroids.length + this.drones.length;
    if (activeHostiles === 0) {
      this.intermissionTimer--;
      if (this.intermissionTimer <= 0) {
        this.beginWave();
        this.intermissionTimer = 50;
      }
    } else {
      this.intermissionTimer = 50;
    }
  }

  // --- Rendering Pipeline ---
  render(alpha = 1.0) {
    const ctx = this.ctx;
    const ox = this.shakeX;
    const oy = this.shakeY;

    // Draw Parallax Starfield
    this.starfield.draw(ctx, ox, oy);

    // Draw Powerups
    for (const pow of this.powerups) {
      this.spriteRenderer.drawPowerup(ctx, pow.x + ox, pow.y + oy, pow.type);
    }

    // Draw Bullets
    const laserIdx = this.saveManager.settings.laserIndex;
    for (const b of this.bullets) {
      this.spriteRenderer.drawLaser(ctx, b.x + ox, b.y + oy, b.heavy, laserIdx, this.gameFrame, b.enemy);
    }

    // Draw Asteroids
    for (const a of this.asteroids) {
      this.spriteRenderer.drawAsteroid(ctx, a.x + ox, a.y + oy, a.type);
    }

    // Draw Particles
    for (const p of this.particles) {
      ctx.fillStyle = p.color;
      ctx.fillRect(Math.floor(p.x + ox), Math.floor(p.y + oy), 1, 1);
    }

    // Draw Hunter Drones
    for (const drone of this.drones) {
      this.spriteRenderer.drawEnemyShip(ctx, drone.x - 10 + ox, drone.y - 8 + oy);
    }

    // Draw Player Ship
    if (!this.isGameOver) {
      const visible = (this.player.invulnerableTimer === 0) || ((this.player.invulnerableTimer % 4) < 2);
      if (visible) {
        const accentIdx = this.saveManager.settings.accentIndex;
        this.spriteRenderer.drawShip(ctx, this.player.x - 10 + ox, this.player.y - 8 + oy, accentIdx, this.gameFrame);

        if (this.player.shieldCharges > 0) {
          this.spriteRenderer.drawShield(ctx, this.player.x + ox, this.player.y + oy, this.gameFrame);
        }
      }
    }

    // Draw Explosions
    for (const ex of this.explosions) {
      this.spriteRenderer.drawExplosion(ctx, ex.x + ox, ex.y + oy, ex.frame);
    }

    // Draw Widescreen HUD
    this.drawHUD(ctx);
  }

  drawHUD(ctx) {
    const white = [240, 246, 255];
    const cyan = [35, 214, 255];
    const gold = [255, 210, 74];
    const green = [80, 240, 140];

    // Score Card (Top Left)
    this.spriteRenderer.drawGlassCard(ctx, 4, 3, 76, 16, '#23d6ff', 'rgba(6, 10, 20, 0.85)');
    const scoreStr = String(this.score).padStart(6, '0');
    this.spriteRenderer.drawText(ctx, 8, 7, scoreStr, white);

    // Wave Card (Top Center)
    this.spriteRenderer.drawGlassCard(ctx, this.width / 2 - 28, 3, 56, 16, '#23d6ff', 'rgba(6, 10, 20, 0.85)');
    const waveStr = `W${String(this.wave).padStart(2, '0')}`;
    this.spriteRenderer.drawTextCentered(ctx, this.width / 2 - 28, 7, 56, waveStr, cyan);

    // Coins Card (Top Right)
    this.spriteRenderer.drawGlassCard(ctx, this.width - 80, 3, 76, 16, '#ffd24a', 'rgba(6, 10, 20, 0.85)');
    const coinStr = `$${this.saveManager.settings.coins}`;
    this.spriteRenderer.drawText(ctx, this.width - 76, 7, coinStr, gold);

    // Lives (Hearts / Ship icons)
    for (let i = 0; i < this.player.lives && i < 7; i++) {
      this.spriteRenderer.drawChar(ctx, 84 + i * 7, 8, '^', green);
    }

    // Shields (Star / Energy icons)
    for (let i = 0; i < this.player.shieldCharges && i < 6; i++) {
      this.spriteRenderer.drawChar(ctx, 134 + i * 7, 8, '*', cyan);
    }

    // Combo Multiplier & Countdown Gauge
    if (this.combo > 1) {
      const comboStr = `x${this.combo}`;
      this.spriteRenderer.drawText(ctx, 6, 23, comboStr, gold);
      this.spriteRenderer.drawProgressBar(ctx, 24, 25, 48, 4, this.comboTimer, 160, '#ffd24a', '#1a2233');
    }

    // Rapid Fire Powerup Timer
    if (this.player.rapidFireTimer > 0) {
      const sec = Math.ceil(this.player.rapidFireTimer / 120);
      const rapidStr = `RAPID ${sec}s`;
      this.spriteRenderer.drawTextCentered(ctx, this.width / 2 - 35, 23, 70, rapidStr, gold);
    }

    // Real-time FPS & Refresh Rate (Top Right Sub-HUD)
    if (this.saveManager.settings.fpsDisplay) {
      const hzStr = `${this.fps} FPS`;
      this.spriteRenderer.drawText(ctx, this.width - 78, 23, hzStr, [180, 200, 225]);
    }

    // Wave Banner Alert
    if (this.waveBannerTimer > 0) {
      const bannerW = 140;
      const bannerH = 26;
      const bx = Math.floor((this.width - bannerW) / 2);
      const by = 80;

      this.spriteRenderer.drawGlassCard(ctx, bx, by, bannerW, bannerH, '#23d6ff', 'rgba(6, 10, 20, 0.92)');
      this.spriteRenderer.drawTextCentered(ctx, bx, by + 5, bannerW, `WAVE ${String(this.wave).padStart(2, '0')}`, white);
      this.spriteRenderer.drawTextCentered(ctx, bx, by + 15, bannerW, 'HOSTILES INCOMING!', cyan);
    }
  }

  // --- High Refresh Rate Main Loop (Supports 60, 90, 120, 144 Hz) ---
  loop(timestamp) {
    if (!this.running) return;

    let delta = timestamp - this.lastTime;
    if (delta > 250) delta = 250; // clamp spiral
    this.lastTime = timestamp;
    this.accumulator += delta;

    // Fixed-timestep physics simulation
    while (this.accumulator >= FIXED_TIMESTEP) {
      if (!this.paused) {
        this.updatePhysics();
      }
      this.accumulator -= FIXED_TIMESTEP;
    }

    // Render interpolated frame
    const alpha = this.accumulator / FIXED_TIMESTEP;
    this.render(alpha);

    // FPS Meter
    this.frameCount++;
    if (timestamp - this.fpsTimer >= 1000) {
      this.fps = this.frameCount;
      this.frameCount = 0;
      this.fpsTimer = timestamp;
    }
  }
}

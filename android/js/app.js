// Application Coordinator & UI Screen Router for Space Unlimited Android

import { SaveManager } from './save.js';
import { AudioManager } from './audio.js';
import { SpriteRenderer } from './sprites.js';
import { Starfield } from './starfield.js';
import { TouchControls } from './touch-controls.js';
import { ShopSystem } from './shop.js';
import { GameEngine } from './game.js';
import { VIRTUAL_WIDTH, VIRTUAL_HEIGHT, formatPrice, DIFFICULTY_INFO, UPG_MAX_LEVEL } from './constants.js';

class AndroidApp {
  constructor() {
    this.saveManager = new SaveManager();
    this.audioManager = new AudioManager(this.saveManager);
    this.spriteRenderer = new SpriteRenderer();
    this.starfield = new Starfield(VIRTUAL_WIDTH, VIRTUAL_HEIGHT);
    this.shopSystem = new ShopSystem(this.saveManager, this.audioManager, this.spriteRenderer);

    this.currentScreen = 'menu'; // 'menu', 'hangar', 'settings', 'controls', 'credits', 'playing', 'paused', 'gameover'
    this.wakeLock = null;

    this.initDOM();
    this.initPWA();
    this.initEngine();
    this.setupEventListeners();
    this.resizeCanvas();
    this.startMainLoop();
  }

  initDOM() {
    this.appContainer = document.getElementById('app-container');
    this.gameCanvas = document.getElementById('game-canvas');
    this.shopCanvas = document.getElementById('shop-preview-canvas');

    // Screens
    this.screens = {
      menu: document.getElementById('screen-menu'),
      hangar: document.getElementById('screen-hangar'),
      settings: document.getElementById('screen-settings'),
      controls: document.getElementById('screen-controls'),
      credits: document.getElementById('screen-credits'),
      paused: document.getElementById('modal-paused'),
      gameover: document.getElementById('modal-gameover')
    };

    // Header coins / stats
    this.menuCoinsEl = document.getElementById('menu-coins-display');
    this.menuHighscoreEl = document.getElementById('menu-highscore-display');
    this.hangarCoinsEl = document.getElementById('hangar-coins-display');

    // Shop DOM
    this.shopTabs = document.querySelectorAll('.shop-tab-btn');
    this.shopItemList = document.getElementById('shop-items-container');
    this.shopItemName = document.getElementById('shop-detail-name');
    this.shopItemBadge = document.getElementById('shop-detail-badge');
    this.shopItemDesc = document.getElementById('shop-detail-desc');
    this.shopItemStats = document.getElementById('shop-detail-stats');
    this.shopBtnAction = document.getElementById('shop-btn-action');
    this.shopMessageEl = document.getElementById('shop-status-message');

    // Settings DOM
    this.btnDiffCadet = document.getElementById('setting-diff-cadet');
    this.btnDiffPilot = document.getElementById('setting-diff-pilot');
    this.btnDiffAce = document.getElementById('setting-diff-ace');
    this.sliderMusic = document.getElementById('setting-slider-music');
    this.sliderSfx = document.getElementById('setting-slider-sfx');
    this.toggleShake = document.getElementById('setting-toggle-shake');
    this.toggleHaptics = document.getElementById('setting-toggle-haptics');
    this.toggleFps = document.getElementById('setting-toggle-fps');
    this.selectJoyMode = document.getElementById('setting-select-joy');
  }

  initPWA() {
    if ('serviceWorker' in navigator) {
      navigator.serviceWorker.register('./sw.js').catch((err) => {
        console.log('SW registration note:', err);
      });
    }
  }

  initEngine() {
    this.touchControls = new TouchControls(this.appContainer, this.saveManager);
    this.gameEngine = new GameEngine(
      this.gameCanvas,
      this.saveManager,
      this.audioManager,
      this.touchControls,
      this.spriteRenderer,
      this.starfield
    );

    this.gameEngine.onGameOver = (score, isNewHighScore) => {
      this.showGameOverModal(score, isNewHighScore);
    };

    this.audioManager.init();
  }

  setupEventListeners() {
    window.addEventListener('resize', () => this.resizeCanvas());
    window.addEventListener('orientationchange', () => setTimeout(() => this.resizeCanvas(), 200));

    // Audio unlock on first touch
    const unlockAudio = () => {
      this.audioManager.unlock();
      this.requestImmersiveMode();
      window.removeEventListener('touchstart', unlockAudio);
      window.removeEventListener('click', unlockAudio);
    };
    window.addEventListener('touchstart', unlockAudio, { passive: true });
    window.addEventListener('click', unlockAudio, { passive: true });
    window.addEventListener('user-interaction', unlockAudio);

    // Navigation buttons
    document.getElementById('btn-play')?.addEventListener('click', () => this.startGame());
    document.getElementById('btn-hangar')?.addEventListener('click', () => this.showScreen('hangar'));
    document.getElementById('btn-settings')?.addEventListener('click', () => this.showScreen('settings'));
    document.getElementById('btn-controls')?.addEventListener('click', () => this.showScreen('controls'));
    document.getElementById('btn-credits')?.addEventListener('click', () => this.showScreen('credits'));
    document.getElementById('btn-fullscreen-toggle')?.addEventListener('click', () => this.toggleFullscreen());

    // Back to Menu buttons
    document.querySelectorAll('.btn-back-menu').forEach((btn) => {
      btn.addEventListener('click', () => this.showScreen('menu'));
    });

    // Pause Modal buttons
    document.getElementById('btn-pause-resume')?.addEventListener('click', () => this.resumeGame());
    document.getElementById('btn-pause-restart')?.addEventListener('click', () => this.startGame());
    document.getElementById('btn-pause-hangar')?.addEventListener('click', () => {
      this.gameEngine.stop();
      this.showScreen('hangar');
    });
    document.getElementById('btn-pause-menu')?.addEventListener('click', () => {
      this.gameEngine.stop();
      this.showScreen('menu');
    });

    // In-game top Pause trigger button
    document.getElementById('btn-ingame-pause')?.addEventListener('click', () => {
      this.pauseGame();
    });

    // Game Over Modal buttons
    document.getElementById('btn-gameover-retry')?.addEventListener('click', () => this.startGame());
    document.getElementById('btn-gameover-hangar')?.addEventListener('click', () => {
      this.showScreen('hangar');
    });
    document.getElementById('btn-gameover-menu')?.addEventListener('click', () => {
      this.showScreen('menu');
    });

    // Shop Tabs
    this.shopTabs.forEach((tab) => {
      tab.addEventListener('click', (e) => {
        const catIdx = parseInt(e.currentTarget.dataset.category, 10);
        this.setShopCategory(catIdx);
      });
    });

    // Shop Action Button (Buy / Equip)
    this.shopBtnAction?.addEventListener('click', () => {
      this.shopSystem.buyOrEquipSelected();
      this.refreshShopUI();
      this.updateHeaderStats();
    });

    // Settings Controls
    this.btnDiffCadet?.addEventListener('click', () => this.setDifficulty(0));
    this.btnDiffPilot?.addEventListener('click', () => this.setDifficulty(1));
    this.btnDiffAce?.addEventListener('click', () => this.setDifficulty(2));

    this.sliderMusic?.addEventListener('input', (e) => {
      this.saveManager.settings.musicVolume = parseInt(e.target.value, 10);
      this.audioManager.updateVolumes();
      this.saveManager.save();
    });

    this.sliderSfx?.addEventListener('input', (e) => {
      this.saveManager.settings.sfxVolume = parseInt(e.target.value, 10);
      this.audioManager.updateVolumes();
      this.saveManager.save();
    });

    this.toggleShake?.addEventListener('change', (e) => {
      this.saveManager.settings.screenShake = e.target.checked;
      this.saveManager.save();
    });

    this.toggleHaptics?.addEventListener('change', (e) => {
      this.saveManager.settings.haptics = e.target.checked;
      this.saveManager.save();
    });

    this.toggleFps?.addEventListener('change', (e) => {
      this.saveManager.settings.fpsDisplay = e.target.checked;
      this.saveManager.save();
    });

    this.selectJoyMode?.addEventListener('change', (e) => {
      this.saveManager.settings.joystickMode = e.target.value;
      this.saveManager.save();
    });

    document.getElementById('btn-reset-save')?.addEventListener('click', () => {
      if (confirm('Are you sure you want to reset all save data and unlocked items?')) {
        this.saveManager.resetAll();
        this.refreshSettingsUI();
        this.updateHeaderStats();
        this.audioManager.playSfx('pickup');
      }
    });
  }

  resizeCanvas() {
    const container = this.appContainer;
    const cw = container.clientWidth;
    const ch = container.clientHeight;

    const targetRatio = VIRTUAL_WIDTH / VIRTUAL_HEIGHT; // 16:9
    const windowRatio = cw / ch;

    let scale;
    if (windowRatio > targetRatio) {
      scale = ch / VIRTUAL_HEIGHT;
    } else {
      scale = cw / VIRTUAL_WIDTH;
    }

    const scaledW = Math.floor(VIRTUAL_WIDTH * scale);
    const scaledH = Math.floor(VIRTUAL_HEIGHT * scale);

    this.gameCanvas.style.width = `${scaledW}px`;
    this.gameCanvas.style.height = `${scaledH}px`;
  }

  async requestImmersiveMode() {
    try {
      if (document.documentElement.requestFullscreen) {
        await document.documentElement.requestFullscreen({ navigationUI: 'hide' }).catch(() => {});
      } else if (document.documentElement.webkitRequestFullscreen) {
        await document.documentElement.webkitRequestFullscreen().catch(() => {});
      }

      if (screen.orientation && screen.orientation.lock) {
        await screen.orientation.lock('landscape').catch(() => {});
      }

      if ('wakeLock' in navigator && !this.wakeLock) {
        this.wakeLock = await navigator.wakeLock.request('screen').catch(() => {});
      }
    } catch (e) {
      console.log('Immersive mode notice:', e);
    }
  }

  toggleFullscreen() {
    if (!document.fullscreenElement) {
      this.requestImmersiveMode();
    } else {
      if (document.exitFullscreen) document.exitFullscreen();
    }
  }

  showScreen(screenName) {
    this.currentScreen = screenName;

    // Hide all screens & modals
    Object.values(this.screens).forEach((el) => {
      if (el) el.classList.remove('active');
    });

    if (this.screens[screenName]) {
      this.screens[screenName].classList.add('active');
    }

    this.touchControls.showControls(screenName === 'playing');
    this.updateHeaderStats();

    if (screenName === 'hangar') {
      this.setShopCategory(this.shopSystem.currentCategory);
    } else if (screenName === 'settings') {
      this.refreshSettingsUI();
    }

    if (screenName !== 'playing') {
      this.audioManager.playBgm('menu');
    }
  }

  startGame() {
    this.showScreen('playing');
    this.screens.paused.classList.remove('active');
    this.screens.gameover.classList.remove('active');
    this.gameEngine.start();
  }

  pauseGame() {
    if (this.currentScreen !== 'playing') return;
    this.gameEngine.setPaused(true);
    this.screens.paused.classList.add('active');
  }

  resumeGame() {
    this.screens.paused.classList.remove('active');
    this.gameEngine.setPaused(false);
  }

  showGameOverModal(score, isNewHighScore) {
    const finalScoreEl = document.getElementById('gameover-score');
    const finalHighEl = document.getElementById('gameover-highscore');
    const finalWaveEl = document.getElementById('gameover-wave');
    const finalCoinsEl = document.getElementById('gameover-coins');
    const newBadgeEl = document.getElementById('gameover-new-badge');

    if (finalScoreEl) finalScoreEl.innerText = String(score).padStart(6, '0');
    if (finalHighEl) finalHighEl.innerText = String(this.saveManager.settings.highScore).padStart(6, '0');
    if (finalWaveEl) finalWaveEl.innerText = `Wave ${this.gameEngine.wave}`;
    if (finalCoinsEl) finalCoinsEl.innerText = `$${this.saveManager.settings.coins}`;
    if (newBadgeEl) newBadgeEl.style.display = isNewHighScore ? 'inline-block' : 'none';

    this.screens.gameover.classList.add('active');
    this.audioManager.playBgm('menu');
  }

  updateHeaderStats() {
    const s = this.saveManager.settings;
    if (this.menuCoinsEl) this.menuCoinsEl.innerText = `$${s.coins}`;
    if (this.menuHighscoreEl) this.menuHighscoreEl.innerText = `BEST: ${String(s.highScore).padStart(6, '0')}`;
    if (this.hangarCoinsEl) this.hangarCoinsEl.innerText = `$${s.coins}`;
  }

  // --- Shop Handling ---
  setShopCategory(catIdx) {
    this.shopSystem.setCategory(catIdx);
    this.shopTabs.forEach((tab, i) => {
      tab.classList.toggle('active', i === catIdx);
    });
    this.renderShopItems();
    this.refreshShopUI();
  }

  renderShopItems() {
    if (!this.shopItemList) return;
    this.shopItemList.innerHTML = '';

    const list = this.shopSystem.getCurrentCategoryList();
    const selectedIdx = this.shopSystem.selectedItemIndex[this.shopSystem.currentCategory] || 0;
    const cat = this.shopSystem.currentCategory;

    list.forEach((item, idx) => {
      const card = document.createElement('div');
      card.className = `shop-item-card ${idx === selectedIdx ? 'selected' : ''}`;
      card.dataset.index = idx;

      let statusBadge = '';
      let badgeClass = 'badge-price';

      if (cat === 0) { // PAINTS
        const isEq = (this.saveManager.settings.accentIndex === item.id);
        const isOwn = this.saveManager.isAccentOwned(item.id);
        if (isEq) { statusBadge = '[EQUIPPED]'; badgeClass = 'badge-equipped'; }
        else if (isOwn) { statusBadge = 'OWNED'; badgeClass = 'badge-owned'; }
        else { statusBadge = formatPrice(item.price); }
      } else if (cat === 1) { // TRAILS
        const isEq = (this.saveManager.settings.trailIndex === item.id);
        const isOwn = this.saveManager.isTrailOwned(item.id);
        if (isEq) { statusBadge = '[EQUIPPED]'; badgeClass = 'badge-equipped'; }
        else if (isOwn) { statusBadge = 'OWNED'; badgeClass = 'badge-owned'; }
        else { statusBadge = formatPrice(item.price); }
      } else if (cat === 2) { // WEAPONS
        const isEq = (this.saveManager.settings.weaponRig === item.id);
        const isOwn = this.saveManager.isRigOwned(item.id);
        if (isEq) { statusBadge = '[EQUIPPED]'; badgeClass = 'badge-equipped'; }
        else if (isOwn) { statusBadge = 'OWNED'; badgeClass = 'badge-owned'; }
        else { statusBadge = formatPrice(item.price); }
      } else if (cat === 3) { // LASERS
        const isEq = (this.saveManager.settings.laserIndex === item.id);
        const isOwn = this.saveManager.isLaserOwned(item.id);
        if (isEq) { statusBadge = '[EQUIPPED]'; badgeClass = 'badge-equipped'; }
        else if (isOwn) { statusBadge = 'OWNED'; badgeClass = 'badge-owned'; }
        else { statusBadge = formatPrice(item.price); }
      } else if (cat === 4) { // TECH
        const curLvl = this.saveManager.getUpgradeLevel(item.id);
        if (curLvl >= UPG_MAX_LEVEL) {
          statusBadge = 'MAX';
          badgeClass = 'badge-owned';
        } else {
          statusBadge = formatPrice(item.prices[curLvl]);
        }
      }

      card.innerHTML = `
        <div class="item-name">${item.name}</div>
        <div class="item-badge ${badgeClass}">${statusBadge}</div>
      `;

      card.addEventListener('click', () => {
        this.shopSystem.selectItem(idx);
        this.shopItemList.querySelectorAll('.shop-item-card').forEach(c => c.classList.remove('selected'));
        card.classList.add('selected');
        this.refreshShopUI();
      });

      this.shopItemList.appendChild(card);
    });
  }

  refreshShopUI() {
    const item = this.shopSystem.getSelectedItem();
    if (!item) return;

    const cat = this.shopSystem.currentCategory;
    if (this.shopItemName) this.shopItemName.innerText = item.name;
    if (this.shopItemDesc) this.shopItemDesc.innerText = item.desc || item.shortDesc || '';

    let statusText = '';
    let btnText = '';
    let btnClass = 'btn-buy';
    let statsText = '';

    if (cat === 0) { // PAINTS
      const isEq = (this.saveManager.settings.accentIndex === item.id);
      const isOwn = this.saveManager.isAccentOwned(item.id);
      if (isEq) {
        statusText = 'STATUS: EQUIPPED';
        btnText = 'EQUIPPED';
        btnClass = 'btn-equipped';
      } else if (isOwn) {
        statusText = 'STATUS: OWNED';
        btnText = 'EQUIP PAINT';
        btnClass = 'btn-equip';
      } else {
        statusText = `PRICE: ${formatPrice(item.price)} COINS`;
        btnText = `BUY FOR ${formatPrice(item.price)}`;
        btnClass = (this.saveManager.settings.coins >= item.price) ? 'btn-buy' : 'btn-disabled';
      }
      statsText = item.animated ? 'Dynamic spectrum wave finish' : 'Hull nano-coat plating';
    } else if (cat === 1) { // TRAILS
      const isEq = (this.saveManager.settings.trailIndex === item.id);
      const isOwn = this.saveManager.isTrailOwned(item.id);
      if (isEq) {
        statusText = 'STATUS: EQUIPPED';
        btnText = 'EQUIPPED';
        btnClass = 'btn-equipped';
      } else if (isOwn) {
        statusText = 'STATUS: OWNED';
        btnText = 'EQUIP TRAIL';
        btnClass = 'btn-equip';
      } else {
        statusText = `PRICE: ${formatPrice(item.price)} COINS`;
        btnText = `BUY FOR ${formatPrice(item.price)}`;
        btnClass = (this.saveManager.settings.coins >= item.price) ? 'btn-buy' : 'btn-disabled';
      }
      statsText = item.animated ? 'Chromatic exhaust trail' : 'Drive propulsion wake';
    } else if (cat === 2) { // WEAPONS
      const isEq = (this.saveManager.settings.weaponRig === item.id);
      const isOwn = this.saveManager.isRigOwned(item.id);
      if (isEq) {
        statusText = 'STATUS: EQUIPPED';
        btnText = 'EQUIPPED';
        btnClass = 'btn-equipped';
      } else if (isOwn) {
        statusText = 'STATUS: OWNED';
        btnText = 'EQUIP RIG';
        btnClass = 'btn-equip';
      } else {
        statusText = `PRICE: ${formatPrice(item.price)} COINS`;
        btnText = `BUY FOR ${formatPrice(item.price)}`;
        btnClass = (this.saveManager.settings.coins >= item.price) ? 'btn-buy' : 'btn-disabled';
      }
      statsText = `${item.bolts} bolts • Base CD: ${item.baseCd} • ${item.heavy ? 'Heavy Piercing' : 'Standard'}`;
    } else if (cat === 3) { // LASERS
      const isEq = (this.saveManager.settings.laserIndex === item.id);
      const isOwn = this.saveManager.isLaserOwned(item.id);
      if (isEq) {
        statusText = 'STATUS: EQUIPPED';
        btnText = 'EQUIPPED';
        btnClass = 'btn-equipped';
      } else if (isOwn) {
        statusText = 'STATUS: OWNED';
        btnText = 'EQUIP CRYSTAL';
        btnClass = 'btn-equip';
      } else {
        statusText = `PRICE: ${formatPrice(item.price)} COINS`;
        btnText = `BUY FOR ${formatPrice(item.price)}`;
        btnClass = (this.saveManager.settings.coins >= item.price) ? 'btn-buy' : 'btn-disabled';
      }
      statsText = `Bonus Damage: +${item.bonusDmg} • Special: ${item.special}`;
    } else if (cat === 4) { // TECH
      const curLvl = this.saveManager.getUpgradeLevel(item.id);
      if (curLvl >= UPG_MAX_LEVEL) {
        statusText = 'STATUS: MAX LEVEL REACHED';
        btnText = 'MAX LEVEL';
        btnClass = 'btn-equipped';
        statsText = item.levelDescs[UPG_MAX_LEVEL];
      } else {
        const nextPrice = item.prices[curLvl];
        statusText = `NEXT LEVEL: ${curLvl + 1} / ${UPG_MAX_LEVEL} • COST: ${formatPrice(nextPrice)}`;
        btnText = `UPGRADE FOR ${formatPrice(nextPrice)}`;
        btnClass = (this.saveManager.settings.coins >= nextPrice) ? 'btn-buy' : 'btn-disabled';
        statsText = `Current: ${item.levelDescs[curLvl]} ➔ Next: ${item.levelDescs[curLvl + 1]}`;
      }
    }

    if (this.shopItemBadge) this.shopItemBadge.innerText = statusText;
    if (this.shopItemStats) this.shopItemStats.innerText = statsText;
    if (this.shopBtnAction) {
      this.shopBtnAction.innerText = btnText;
      this.shopBtnAction.className = `action-btn ${btnClass}`;
    }

    if (this.shopMessageEl) {
      if (this.shopSystem.messageTimer > 0) {
        this.shopMessageEl.innerText = this.shopSystem.message;
        this.shopMessageEl.style.color = this.shopSystem.messageColor;
        this.shopMessageEl.style.display = 'block';
      } else {
        this.shopMessageEl.style.display = 'none';
      }
    }
  }

  // --- Settings Handling ---
  setDifficulty(diffIdx) {
    this.saveManager.settings.difficulty = diffIdx;
    this.saveManager.save();
    this.refreshSettingsUI();
  }

  refreshSettingsUI() {
    const s = this.saveManager.settings;
    if (this.btnDiffCadet) this.btnDiffCadet.classList.toggle('active', s.difficulty === 0);
    if (this.btnDiffPilot) this.btnDiffPilot.classList.toggle('active', s.difficulty === 1);
    if (this.btnDiffAce) this.btnDiffAce.classList.toggle('active', s.difficulty === 2);

    if (this.sliderMusic) this.sliderMusic.value = s.musicVolume;
    if (this.sliderSfx) this.sliderSfx.value = s.sfxVolume;
    if (this.toggleShake) this.toggleShake.checked = s.screenShake;
    if (this.toggleHaptics) this.toggleHaptics.checked = s.haptics;
    if (this.toggleFps) this.toggleFps.checked = s.fpsDisplay;
    if (this.selectJoyMode) this.selectJoyMode.value = s.joystickMode;
  }

  // --- Unified Main Animation Loop ---
  startMainLoop() {
    const renderPreview = () => {
      if (this.shopCanvas && (this.currentScreen === 'hangar' || this.currentScreen === 'menu')) {
        const sCtx = this.shopCanvas.getContext('2d');
        sCtx.clearRect(0, 0, this.shopCanvas.width, this.shopCanvas.height);
        this.shopSystem.drawShipPreview(sCtx, this.shopCanvas.width / 2, this.shopCanvas.height / 2 - 4);
      }
    };

    const loop = (timestamp) => {
      if (this.currentScreen === 'playing') {
        this.gameEngine.loop(timestamp);
      } else {
        // Render background starfield on menu canvas
        const ctx = this.gameCanvas.getContext('2d', { alpha: false });
        this.starfield.update(0.6);
        this.starfield.draw(ctx, 0, 0);

        this.shopSystem.update();
        renderPreview();
      }

      requestAnimationFrame(loop);
    };

    requestAnimationFrame(loop);
  }
}

window.addEventListener('DOMContentLoaded', () => {
  window.app = new AndroidApp();
});

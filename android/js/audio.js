// Web Audio Engine for Space Unlimited Android

export class AudioManager {
  constructor(saveManager) {
    this.saveManager = saveManager;
    this.ctx = null;
    this.buffers = {};
    this.bgmSource = null;
    this.bgmGainNode = null;
    this.sfxGainNode = null;
    this.currentBgm = null;
    this.isUnlocked = false;
    this.loaded = false;

    this.soundPaths = {
      menu: 'assets/audio/menu.wav',
      game: 'assets/audio/game.wav',
      laser: 'assets/audio/laser.wav',
      explosion: 'assets/audio/explosion.wav',
      pickup: 'assets/audio/pickup.wav'
    };

    this.setupVisibilityListeners();
  }

  setupVisibilityListeners() {
    document.addEventListener('visibilitychange', () => {
      if (document.hidden) {
        if (this.ctx && this.ctx.state === 'running') {
          this.ctx.suspend();
        }
      } else {
        if (this.ctx && this.ctx.state === 'suspended' && this.isUnlocked) {
          this.ctx.resume();
        }
      }
    });
  }

  async init() {
    const AudioCtx = window.AudioContext || window.webkitAudioContext;
    if (!AudioCtx) {
      console.warn('Web Audio API not supported in this browser.');
      return;
    }
    this.ctx = new AudioCtx();
    this.bgmGainNode = this.ctx.createGain();
    this.sfxGainNode = this.ctx.createGain();

    this.bgmGainNode.connect(this.ctx.destination);
    this.sfxGainNode.connect(this.ctx.destination);

    this.updateVolumes();
    await this.loadAllSounds();
  }

  unlock() {
    if (!this.ctx) return;
    if (this.ctx.state === 'suspended') {
      this.ctx.resume().then(() => {
        this.isUnlocked = true;
      });
    } else {
      this.isUnlocked = true;
    }
  }

  updateVolumes() {
    if (!this.bgmGainNode || !this.sfxGainNode) return;
    const s = this.saveManager.settings;
    const musicVol = (s.musicVolume / 100) * 0.75;
    const sfxVol = (s.sfxVolume / 100) * 0.85;

    this.bgmGainNode.gain.setValueAtTime(musicVol, this.ctx ? this.ctx.currentTime : 0);
    this.sfxGainNode.gain.setValueAtTime(sfxVol, this.ctx ? this.ctx.currentTime : 0);
  }

  async loadAllSounds() {
    const promises = Object.entries(this.soundPaths).map(async ([key, url]) => {
      try {
        const res = await fetch(url);
        const arrayBuf = await res.arrayBuffer();
        const audioBuf = await this.ctx.decodeAudioData(arrayBuf);
        this.buffers[key] = audioBuf;
      } catch (err) {
        console.warn(`Could not load audio for "${key}":`, err);
      }
    });

    await Promise.all(promises);
    this.loaded = true;
  }

  playBgm(name) {
    if (!this.ctx || !this.buffers[name] || this.currentBgm === name) return;
    this.stopBgm();

    try {
      const source = this.ctx.createBufferSource();
      source.buffer = this.buffers[name];
      source.loop = true;
      source.connect(this.bgmGainNode);
      source.start(0);

      this.bgmSource = source;
      this.currentBgm = name;
    } catch (e) {
      console.warn('Error starting BGM:', e);
    }
  }

  stopBgm() {
    if (this.bgmSource) {
      try {
        this.bgmSource.stop();
        this.bgmSource.disconnect();
      } catch (e) {}
      this.bgmSource = null;
      this.currentBgm = null;
    }
  }

  playSfx(name, playbackRate = 1.0) {
    if (!this.ctx || !this.buffers[name]) return;
    if (this.ctx.state === 'suspended') {
      this.ctx.resume();
    }

    try {
      const source = this.ctx.createBufferSource();
      source.buffer = this.buffers[name];
      source.playbackRate.value = playbackRate;
      source.connect(this.sfxGainNode);
      source.start(0);
    } catch (e) {
      console.warn('Error playing SFX:', e);
    }
  }
}

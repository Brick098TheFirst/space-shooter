// Virtual Analog Joystick & Multi-Touch Controller for Space Unlimited Android

export class TouchControls {
  constructor(containerEl, saveManager) {
    this.container = containerEl;
    this.saveManager = saveManager;

    // Joystick state
    this.joystickActive = false;
    this.joystickTouchId = null;
    this.joystickBase = { x: 0, y: 0 };
    this.joystickKnob = { x: 0, y: 0 };
    this.joystickRadius = 50; // max drag radius
    this.vector = { x: 0, y: 0 }; // normalized -1.0 .. +1.0
    this.magnitude = 0; // 0.0 .. 1.0

    // Button states
    this.isFiring = false;
    this.isDashing = false;
    this.dashTriggered = false;
    this.pauseTriggered = false;

    // Swipe tracking for menus/tabs
    this.touchStartX = 0;
    this.touchStartY = 0;
    this.onSwipeLeft = null;
    this.onSwipeRight = null;
    this.onTap = null;

    // Keyboard & Gamepad state fallback
    this.keys = new Set();

    this.initDOM();
    this.bindEvents();
  }

  initDOM() {
    this.overlay = document.getElementById('touch-controls-layer');
    this.joystickZone = document.getElementById('joystick-zone');
    this.joystickBaseEl = document.getElementById('joystick-base');
    this.joystickKnobEl = document.getElementById('joystick-knob');
    this.btnFire = document.getElementById('btn-fire');
    this.btnDash = document.getElementById('btn-dash');
    this.dashCooldownSvg = document.getElementById('dash-cooldown-circle');
  }

  bindEvents() {
    // Prevent default mobile gestures (pinch zoom, double tap zoom, bounce scroll)
    window.addEventListener('touchstart', (e) => {
      // Audio unlock hook
      window.dispatchEvent(new CustomEvent('user-interaction'));
    }, { passive: false });

    // Touch events on Joystick Zone
    if (this.joystickZone) {
      this.joystickZone.addEventListener('touchstart', (e) => this.handleJoystickStart(e), { passive: false });
      this.joystickZone.addEventListener('touchmove', (e) => this.handleJoystickMove(e), { passive: false });
      this.joystickZone.addEventListener('touchend', (e) => this.handleJoystickEnd(e), { passive: false });
      this.joystickZone.addEventListener('touchcancel', (e) => this.handleJoystickEnd(e), { passive: false });
    }

    // Touch events on Action Buttons
    if (this.btnFire) {
      this.btnFire.addEventListener('touchstart', (e) => {
        e.preventDefault();
        e.stopPropagation();
        this.isFiring = true;
        this.btnFire.classList.add('active');
        this.vibrate(10);
      }, { passive: false });

      this.btnFire.addEventListener('touchend', (e) => {
        e.preventDefault();
        this.isFiring = false;
        this.btnFire.classList.remove('active');
      }, { passive: false });

      this.btnFire.addEventListener('touchcancel', () => {
        this.isFiring = false;
        this.btnFire.classList.remove('active');
      }, { passive: false });
    }

    if (this.btnDash) {
      this.btnDash.addEventListener('touchstart', (e) => {
        e.preventDefault();
        e.stopPropagation();
        this.isDashing = true;
        this.dashTriggered = true;
        this.btnDash.classList.add('active');
        this.vibrate(15);
      }, { passive: false });

      this.btnDash.addEventListener('touchend', (e) => {
        e.preventDefault();
        this.isDashing = false;
        this.btnDash.classList.remove('active');
      }, { passive: false });

      this.btnDash.addEventListener('touchcancel', () => {
        this.isDashing = false;
        this.btnDash.classList.remove('active');
      }, { passive: false });
    }

    // Keyboard Fallback
    window.addEventListener('keydown', (e) => {
      this.keys.add(e.code);
      if (e.code === 'Space' || e.code === 'KeyZ' || e.code === 'KeyJ') {
        this.isFiring = true;
      }
      if (e.code === 'ShiftLeft' || e.code === 'ShiftRight' || e.code === 'KeyX' || e.code === 'KeyK') {
        this.dashTriggered = true;
      }
      if (e.code === 'KeyP' || e.code === 'Escape') {
        this.pauseTriggered = true;
      }
    });

    window.addEventListener('keyup', (e) => {
      this.keys.delete(e.code);
      if (e.code === 'Space' || e.code === 'KeyZ' || e.code === 'KeyJ') {
        this.isFiring = false;
      }
    });
  }

  handleJoystickStart(e) {
    e.preventDefault();
    if (this.joystickActive) return;

    const touch = e.changedTouches[0];
    this.joystickTouchId = touch.identifier;
    this.joystickActive = true;

    const rect = this.joystickZone.getBoundingClientRect();
    const touchX = touch.clientX - rect.left;
    const touchY = touch.clientY - rect.top;

    const isFixed = (this.saveManager.settings.joystickMode === 'fixed');
    if (isFixed) {
      this.joystickBase = { x: rect.width * 0.45, y: rect.height * 0.55 };
    } else {
      this.joystickBase = { x: touchX, y: touchY };
    }

    this.joystickKnob = { ...this.joystickBase };

    this.updateJoystickDOM();
    if (this.joystickBaseEl) this.joystickBaseEl.classList.add('visible');
  }

  handleJoystickMove(e) {
    e.preventDefault();
    if (!this.joystickActive) return;

    for (let i = 0; i < e.changedTouches.length; i++) {
      const touch = e.changedTouches[i];
      if (touch.identifier === this.joystickTouchId) {
        const rect = this.joystickZone.getBoundingClientRect();
        const touchX = touch.clientX - rect.left;
        const touchY = touch.clientY - rect.top;

        const dx = touchX - this.joystickBase.x;
        const dy = touchY - this.joystickBase.y;
        const dist = Math.hypot(dx, dy);

        if (dist === 0) {
          this.vector = { x: 0, y: 0 };
          this.magnitude = 0;
          this.joystickKnob = { ...this.joystickBase };
        } else {
          const clampedDist = Math.min(this.joystickRadius, dist);
          const angle = Math.atan2(dy, dx);

          this.joystickKnob = {
            x: this.joystickBase.x + Math.cos(angle) * clampedDist,
            y: this.joystickBase.y + Math.sin(angle) * clampedDist
          };

          // Deadzone calculation (15% deadzone)
          const normDist = clampedDist / this.joystickRadius;
          if (normDist < 0.15) {
            this.vector = { x: 0, y: 0 };
            this.magnitude = 0;
          } else {
            const remappedMag = (normDist - 0.15) / 0.85;
            this.vector = {
              x: Math.cos(angle) * remappedMag,
              y: Math.sin(angle) * remappedMag
            };
            this.magnitude = remappedMag;
          }
        }

        this.updateJoystickDOM();
        break;
      }
    }
  }

  handleJoystickEnd(e) {
    for (let i = 0; i < e.changedTouches.length; i++) {
      if (e.changedTouches[i].identifier === this.joystickTouchId) {
        this.joystickActive = false;
        this.joystickTouchId = null;
        this.vector = { x: 0, y: 0 };
        this.magnitude = 0;

        if (this.joystickBaseEl) this.joystickBaseEl.classList.remove('visible');
        break;
      }
    }
  }

  updateJoystickDOM() {
    if (!this.joystickBaseEl || !this.joystickKnobEl) return;
    this.joystickBaseEl.style.left = `${this.joystickBase.x}px`;
    this.joystickBaseEl.style.top = `${this.joystickBase.y}px`;

    const knobDx = this.joystickKnob.x - this.joystickBase.x;
    const knobDy = this.joystickKnob.y - this.joystickBase.y;
    this.joystickKnobEl.style.transform = `translate(${knobDx}px, ${knobDy}px)`;
  }

  updateDashCooldown(readyFraction) {
    if (!this.dashCooldownSvg) return;
    // 0.0 (just dashed, on cooldown) to 1.0 (ready)
    const circumference = 2 * Math.PI * 26; // r=26
    const offset = circumference * (1 - Math.min(1, Math.max(0, readyFraction)));
    this.dashCooldownSvg.style.strokeDashoffset = offset;

    if (this.btnDash) {
      if (readyFraction >= 1.0) {
        this.btnDash.classList.add('ready');
      } else {
        this.btnDash.classList.remove('ready');
      }
    }
  }

  showControls(show = true) {
    if (this.overlay) {
      this.overlay.style.display = show ? 'flex' : 'none';
    }
  }

  pollInput() {
    let vx = this.vector.x;
    let vy = this.vector.y;

    // Keyboard overlay
    if (this.keys.has('KeyA') || this.keys.has('ArrowLeft')) vx -= 1.0;
    if (this.keys.has('KeyD') || this.keys.has('ArrowRight')) vx += 1.0;
    if (this.keys.has('KeyW') || this.keys.has('ArrowUp')) vy -= 1.0;
    if (this.keys.has('KeyS') || this.keys.has('ArrowDown')) vy += 1.0;

    // Clamp magnitude to 1.0
    const mag = Math.hypot(vx, vy);
    if (mag > 1.0) {
      vx /= mag;
      vy /= mag;
    }

    const fire = this.isFiring || this.keys.has('Space') || this.keys.has('KeyZ') || this.keys.has('KeyJ');
    const dash = this.dashTriggered;
    const pause = this.pauseTriggered;

    this.dashTriggered = false;
    this.pauseTriggered = false;

    return {
      vx,
      vy,
      fire,
      dash,
      pause
    };
  }

  vibrate(durationMs = 15) {
    if (!this.saveManager.settings.haptics) return;
    if (navigator.vibrate) {
      try {
        navigator.vibrate(durationMs);
      } catch (e) {}
    }
  }
}

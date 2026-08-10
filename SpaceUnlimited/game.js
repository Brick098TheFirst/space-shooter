/* ============================================================================
 * SPACE UNLIMITED — game engine
 * A faithful 1:1 port of the Scratch project "spaceshooter.sb3"
 * (original author: Brick098TheFirst).
 *
 * Every rule was transcribed from the Scratch blocks in project.json:
 *  - menu -> "get ready" -> gameplay -> game over flow (Enter to advance)
 *  - 5 rock types with the exact spawn chances and glide speeds
 *  - laser fires while the mouse is held down (0.1 s cooldown, +15 y per tick)
 *  - scoring/lives exactly as the original (incl. quirks: med rock2 gives no
 *    points; small/tiny rocks are silent when destroyed by a laser)
 *  - shield pickup: +1 life, capped at 3 (the original caps it twice)
 *  - hi-score behaves like the original cloud variable, but is stored
 *    locally on this device instead (tiny adjustment)
 *  - same speed as Scratch: 30 logic ticks per second
 *
 * Tiny adjustments (all documented in the README):
 *  1. P / Esc pauses the game
 *  2. M mutes all sound
 *  3. hi-score persists in localStorage (cloud variable equivalent)
 *  4. small on-screen control hints on the menu
 *
 * This file is pure logic (no DOM) so it can be unit-tested in Node.
 * ==========================================================================*/
'use strict';

(function (global) {
  var TICK_MS = 1000 / 30; // Scratch runs about 30 ticks per second

  /* Rock definitions transcribed from the Scratch sprites. */
  var ROCKS = {
    // spawn: "pick random (1..spawnEvery) == spawnMatch" per tick, as in Scratch
    big:   { img: 'meteorBrown_big1',   size: 100, glideMin: 1.5, glideMax: 5,   spawnEvery: 50,  spawnMatch: 2, radius: 30, score: true,  laserSound: 'expl6',     hitSound: 'expl6' },
    med:   { img: 'meteorBrown_med1',   size: 120, glideMin: 1,   glideMax: 3,   spawnEvery: 60,  spawnMatch: 5, radius: 24, score: true,  laserSound: 'expl6',     hitSound: 'expl6' },
    med2:  { img: 'meteorBrown_med3',   size: 120, glideMin: 1,   glideMax: 3,   spawnEvery: 60,  spawnMatch: 5, radius: 24, score: false, laserSound: 'expl6',     hitSound: 'expl6' },
    small: { img: 'meteorBrown_small2', size: 100, glideMin: 0.5, glideMax: 2.5, spawnEvery: 75,  spawnMatch: 5, radius: 15, score: true,  laserSound: null,        hitSound: 'Explosion2' },
    tiny:  { img: 'meteorBrown_tiny1',  size: 100, glideMin: 0.3, glideMax: 2,   spawnEvery: 100, spawnMatch: 5, radius: 10, score: true,  laserSound: null,        hitSound: 'Explosion2' }
  };
  // Note: in the original Scratch project, small/tiny rock laser-hit scripts
  // play "expl6" but those sprites don't own that sound, so they are silent
  // (laserSound: null reproduces that). Their player-hit sound is Explosion2.

  var PLAYER_R = 18;   // collision radius of the ship (size 50)
  var LASER_R = 4;     // collision radius of the laser bolt
  var SHIELD_R = 16;   // collision radius of the shield pickup

  /* Life-ship HUD: [left, middle, right]. Visibility logic transcribed from
   * spaceshiplife3 / spaceshiplife1 / spaceshiplifw2:
   *   left   : shown from the start, hides only on game over
   *   middle : hidden when lifes == 1  (so visible while lifes >= 2)
   *   right  : hidden when lifes == 2  (so visible only while lifes == 3)
   *   endgame fires when lifes < 1 (checked by the left ship sprite)
   */
  var LIFE_SHIPS = [
    { x: 110, y: 160, visibleIf: function (lifes) { return lifes >= 1; } },
    { x: 160, y: 160, visibleIf: function (lifes) { return lifes >= 2; } },
    { x: 210, y: 160, visibleIf: function (lifes) { return lifes >= 3; } }
  ];

  function createGame(env) {
    var state = {
      mode: 'menu',            // 'menu' | 'ready' | 'play' | 'over'
      paused: false,
      muted: false,
      score: 0,
      lifes: 3,
      hiScore: (typeof env.storageLoad === 'function' ? (env.storageLoad() || 0) : 0),
      player: { x: -106, y: -145, visible: false },
      lasers: [],
      rocks: [],
      shields: [],
      explosion: null,         // { x, y, frame, ticks }
      lifeShips: [
        { x: 110, y: 160, visible: true },
        { x: 160, y: 160, visible: true },
        { x: 210, y: 160, visible: true }
      ],
      tickCount: 0
    };

    var input = { mouseX: -106, mouseDown: false };
    var cooldown = 0;      // laser cooldown in ticks (0.1 s = 3 ticks)
    var readyTimer = 0;    // ms left in the "get ready" sequence

    /* ---------------- helpers ---------------- */

    function randInt(a, b) { return Math.floor(env.rand(a, b + 1)); }
    function dist(a, b) { return Math.hypot(a.x - b.x, a.y - b.y); }

    function clearEntities() {
      state.lasers = [];
      state.rocks = [];
      state.shields = [];
      state.explosion = null;
    }

    function saveHiScore() {
      if (state.score > state.hiScore) {
        state.hiScore = state.score;
        if (typeof env.storageSave === 'function') env.storageSave(state.hiScore);
      }
    }

    /* ---------------- mode transitions ---------------- */

    function goMenu() {
      state.mode = 'menu';
      clearEntities();
      state.player.visible = false;
      if (typeof env.stopSounds === 'function') env.stopSounds();
    }

    function goReady() {
      state.mode = 'ready';
      readyTimer = 1325 + 1000; // "get ready" sound (1.325 s) + 1 s pause, as in Scratch
      clearEntities();
      state.player.visible = false;
      if (typeof env.music === 'function') env.music('getready', false);
    }

    function goPlay() {
      state.mode = 'play';
      state.score = 0;
      state.lifes = 3;
      cooldown = 0;
      clearEntities();
      state.player.visible = true;
      if (typeof env.music === 'function') env.music('frozenjam2', true);
    }

    function goOver() {
      state.mode = 'over';
      state.player.visible = false;
      clearEntities();
      // Original: endgame stops the gameplay music (Character stops its
      // scripts) and the Stage plays "398220_frozenjam" once.
      if (typeof env.music === 'function') env.music('frozenjam', false);
    }

    /* ---------------- spawning ---------------- */

    function spawnRock(kind) {
      var def = ROCKS[kind];
      var dur = env.rand(def.glideMin, def.glideMax);
      state.rocks.push({
        kind: kind,
        x0: randInt(-140, 140), y0: 145,
        x1: randInt(-240, 240), y1: -180,
        dur: dur,
        p: 0,                 // glide progress 0..1
        step: 1 / (dur * 30), // progress per tick
        rot: 90 + randInt(-180, 180), // "turn right (random -180..180)" once at spawn
        size: def.size,
        radius: def.radius,
        img: def.img
      });
    }

    function spawnLaser() {
      state.lasers.push({ x: state.player.x, y: -145, prevY: -145, life: 22 });
    }

    function spawnShield() {
      state.shields.push({ x: randInt(-140, 140), y: 145, prevY: 145, life: 70 });
    }

    /* ---------------- collisions ---------------- */

    function rockHitByLaser(rock, laser) {
      var def = ROCKS[rock.kind];
      if (def.laserSound && typeof env.sfx === 'function') env.sfx(def.laserSound);
      if (def.score) {
        state.score += 1;
        // original broadcasts "b" -> every laser clone deletes itself
        state.lasers = [];
      }
      // rock is deleted (both the hit script and the redundant
      // "touching laser -> delete clone" script do this)
      rock.dead = true;
    }

    function rockHitPlayer(rock) {
      var def = ROCKS[rock.kind];
      if (typeof env.sfx === 'function') env.sfx(def.hitSound);
      state.lifes -= 1;
      state.explosion = { x: state.player.x, y: -145, frame: 0, ticks: 0 };
      rock.dead = true;
      if (state.lifes < 1) goOver();
    }

    /* ---------------- main tick (30 Hz) ---------------- */

    function tick() {
      if (state.paused) return;
      state.tickCount++;

      // "get ready" countdown (get-ready sound 1.325 s + 1 s pause)
      if (state.mode === 'ready') {
        readyTimer -= TICK_MS;
        if (readyTimer <= 0) goPlay();
        return;
      }
      if (state.mode !== 'play') return;

      // --- player: "go to (mouse x, -145)" ---
      state.player.x = input.mouseX;
      state.player.y = -145;

      // --- firing: "if mouse down -> broadcast message1" (0.1 s cooldown) ---
      if (input.mouseDown) {
        cooldown--;
        if (cooldown <= 0) {
          cooldown = 3; // 3 ticks = 0.1 s
          spawnLaser();
          if (typeof env.sfx === 'function') env.sfx('pew');
        }
      } else {
        cooldown = 0; // original: the cooldown only ticks while the button is held
      }

      // --- rock spawners (exact probabilities from the Scratch project) ---
      var kinds = ['big', 'med', 'med2', 'small', 'tiny'];
      for (var i = 0; i < kinds.length; i++) {
        var def = ROCKS[kinds[i]];
        if (randInt(1, def.spawnEvery) === def.spawnMatch) spawnRock(kinds[i]);
      }

      // --- shield spawner: random 1..250 == 1 ---
      if (randInt(1, 250) === 1) spawnShield();

      // --- lasers: +15 y per tick, 22 ticks of life ---
      for (var l = 0; l < state.lasers.length; l++) {
        var laser = state.lasers[l];
        laser.prevY = laser.y;
        laser.y += 15;
        laser.life--;
      }
      state.lasers = state.lasers.filter(function (ls) { return ls.life > 0; });

      // --- rocks: glide + collisions ---
      for (var r = 0; r < state.rocks.length; r++) {
        var rock = state.rocks[r];
        rock.p += rock.step;
        if (rock.p >= 1) { rock.dead = true; continue; }
        var rx = rock.x0 + (rock.x1 - rock.x0) * rock.p;
        var ry = rock.y0 + (rock.y1 - rock.y0) * rock.p;
        rock.x = rx; rock.y = ry;

        // laser collision
        for (var l2 = 0; l2 < state.lasers.length; l2++) {
          if (dist(rock, state.lasers[l2]) < rock.radius + LASER_R) {
            rockHitByLaser(rock, state.lasers[l2]);
            break;
          }
        }
        if (rock.dead) continue;

        // player collision
        if (dist(rock, state.player) < rock.radius + PLAYER_R) {
          rockHitPlayer(rock);
        }
      }
      state.rocks = state.rocks.filter(function (rk) { return !rk.dead; });

      // --- shields: fall 5 per tick, 70 ticks, pickup on contact ---
      for (var s = 0; s < state.shields.length; s++) {
        var sh = state.shields[s];
        sh.prevY = sh.y;
        sh.y -= 5;
        sh.life--;
        if (sh.life <= 0) { sh.dead = true; continue; }
        if (dist(sh, state.player) < SHIELD_R + PLAYER_R) {
          if (typeof env.sfx === 'function') env.sfx('Pop');
          state.lifes += 1;
          if (state.lifes > 3) state.lifes = 3; // original caps at 3
          sh.dead = true;
        }
      }
      state.shields = state.shields.filter(function (shd) { return !shd.dead; });

      // --- life-ship HUD ---
      for (var i3 = 0; i3 < 3; i3++) {
        state.lifeShips[i3].visible = LIFE_SHIPS[i3].visibleIf(state.lifes);
      }

      // --- explosion follows the player (original has a forever go-to) ---
      if (state.explosion) {
        state.explosion.x = state.player.x;
        state.explosion.y = -145;
        state.explosion.ticks++;
        if (state.explosion.ticks >= 9) state.explosion = null;
        else state.explosion.frame++;
      }

      // --- hi-score: "if score > hi-score -> set hi-score to score" ---
      saveHiScore();
    }

    /* ---------------- public API ---------------- */

    return {
      state: state,
      tick: tick,
      setMouse: function (x, down) {
        input.mouseX = x;
        input.mouseDown = down;
      },
      pressEnter: function () {
        if (state.mode === 'menu') goReady();
        else if (state.mode === 'over') goMenu();
      },
      togglePause: function () {
        if (state.mode === 'play') state.paused = !state.paused;
        return state.paused;
      },
      toggleMute: function () {
        state.muted = !state.muted;
        return state.muted;
      },
      start: goMenu
    };
  }

  global.createGame = createGame;
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = { createGame: createGame };
  }
})(typeof window !== 'undefined' ? window : globalThis);

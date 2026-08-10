'use strict';
/* Headless test of the Space Unlimited game engine (game.js). */
const { createGame } = require('./game.js');

let failures = 0;
function check(name, cond) {
  if (cond) console.log('  PASS ' + name);
  else { failures++; console.log('  FAIL ' + name); }
}

// seeded RNG so the test is deterministic
let seed = 42;
function rand(a, b) {
  seed = (seed * 1103515245 + 12345) & 0x7fffffff;
  return a + (seed / 0x7fffffff) * (b - a);
}

function makeEnv() {
  let stored = null;
  const sfxLog = [];
  const musicLog = [];
  return {
    env: {
      rand,
      sfx: (n) => sfxLog.push(n),
      music: (n, loop) => musicLog.push([n, loop]),
      stopSounds: () => musicLog.push(['stop']),
      storageLoad: () => stored,
      storageSave: (n) => { stored = n; }
    },
    sfxLog, musicLog,
    get stored() { return stored; }
  };
}

const t = makeEnv();
const g = createGame(t.env);
const s = g.state;

function placeRockAt(kind, x, y, radius, img) {
  s.rocks.push({ kind: kind, x0: x, y0: y, x1: x, y1: y, dur: 1, p: 0.5, step: 0.1, rot: 0, size: 100, radius: radius, img: img });
  return s.rocks[s.rocks.length - 1];
}



console.log('== initial state ==');
check('starts in menu', s.mode === 'menu');
check('hi-score loads from storage (0 initially)', s.hiScore === 0);

console.log('== menu -> ready -> play ==');
g.pressEnter();
check('enter from menu -> ready', s.mode === 'ready');
for (let i = 0; i < 80; i++) g.tick();
check('ready ends -> play', s.mode === 'play');
check('score reset to 0', s.score === 0);
check('lifes reset to 3', s.lifes === 3);
check('gameplay music started (frozenjam2, loop)', t.musicLog.some(m => m[0] === 'frozenjam2' && m[1] === true));

console.log('== spawners produce entities (separate instance) ==');
const t2 = makeEnv();
const g2 = createGame(t2.env);
g2.pressEnter();
for (let i = 0; i < 80; i++) g2.tick();
let rockSpawned = false;
let shieldSpawned = false;
for (let i = 0; i < 4000; i++) {
  g2.tick();
  if (g2.state.rocks.length > 0) rockSpawned = true;
  if (g2.state.shields.length > 0) shieldSpawned = true;
  if (rockSpawned && shieldSpawned) break;
}
check('rocks spawn during play', rockSpawned);
check('shields spawn during play', shieldSpawned);

console.log('== main instance: park player far off-screen for collision tests ==');
g.setMouse(9999, false);

console.log('== laser collision: score +1, laser removed, rock removed ==');
placeRockAt('med', 0, 0, 24, 'meteorBrown_med1');
s.lasers.push({ x: 0, y: 0, prevY: 0, life: 10 });
const scoreBefore = s.score;
g.tick();
check('score increased by 1 (med rock)', s.score === scoreBefore + 1);
check('rock removed', !s.rocks.some(r => r.kind === 'med'));
check('lasers all removed (broadcast b)', s.lasers.length === 0);
check('expl6 sfx played', t.sfxLog.includes('expl6'));

console.log('== med rock2 gives NO score (faithful quirk) ==');
placeRockAt('med2', 0, 0, 24, 'meteorBrown_med3');
s.lasers.push({ x: 0, y: 0, prevY: 0, life: 10 });
const scoreBefore2 = s.score;
g.tick();
check('score unchanged (med2 gives no points)', s.score === scoreBefore2);
check('med2 rock removed', !s.rocks.some(r => r.kind === 'med2'));

console.log('== small rock laser hit: silent (no expl6) but +1 score ==');
t.sfxLog.length = 0;
placeRockAt('small', 0, 0, 15, 'meteorBrown_small2');
s.lasers.push({ x: 0, y: 0, prevY: 0, life: 10 });
const scoreBefore3 = s.score;
g.tick();
check('score increased by 1 (small rock)', s.score === scoreBefore3 + 1);
check('no sound on small rock laser hit (faithful silence)', !t.sfxLog.includes('expl6'));

console.log('== rock hits player: -1 life, explosion, Explosion2 for small ==');
t.sfxLog.length = 0;
const rk = placeRockAt('small', s.player.x, s.player.y, 15, 'meteorBrown_small2');
const lifesBefore = s.lifes;
g.tick();
check('lifes decreased', s.lifes === lifesBefore - 1);
check('explosion active', s.explosion !== null);
check('Explosion2 sfx played (small rock)', t.sfxLog.includes('Explosion2'));

console.log('== explosion animates 9 frames then hides ==');
s.explosion = { x: 0, y: -145, frame: 0, ticks: 0 };
for (let i = 0; i < 9; i++) g.tick();
check('explosion finished after 9 ticks', s.explosion === null);

console.log('== shield pickup: +1 life capped at 3 ==');
s.lifes = 2;
s.shields.push({ x: s.player.x, y: s.player.y, prevY: s.player.y, life: 70 });
g.tick();
check('lifes increased to 3', s.lifes === 3);
s.lifes = 3;
s.shields.push({ x: s.player.x, y: s.player.y, prevY: s.player.y, life: 70 });
g.tick();
check('lifes capped at 3 (shield at full lives gives nothing)', s.lifes === 3);
check('Pop sfx played', t.sfxLog.includes('Pop'));

console.log('== shield falls 5/tick and expires after 70 ticks ==');
s.shields.push({ x: 0, y: 100, prevY: 100, life: 70 });
for (let i = 0; i < 70; i++) g.tick();
check('shield expired after 70 ticks', !s.shields.some(sh => sh.life > 0));

console.log('== life-ship HUD visibility ==');
s.lifes = 3; g.tick();
check('3 lives: all ships visible', s.lifeShips.every(l => l.visible));
s.lifes = 2; g.tick();
check('2 lives: left+middle visible, right hidden', s.lifeShips[0].visible && s.lifeShips[1].visible && !s.lifeShips[2].visible);
s.lifes = 1; g.tick();
check('1 life: left visible, others hidden', s.lifeShips[0].visible && !s.lifeShips[1].visible && !s.lifeShips[2].visible);

console.log('== laser fires while mouse held (0.1s cooldown = 3 ticks) ==');
t.sfxLog.length = 0;
s.lasers.length = 0;
s.rocks.length = 0;
g.setMouse(50, true);
for (let i = 0; i < 6; i++) g.tick();
check('laser spawned on first tick of mouse-down', s.lasers.length >= 1);
check('pew played', t.sfxLog.includes('pew'));
const pewCount = t.sfxLog.filter(x => x === 'pew').length;
check('pew rate ~10/s (3 ticks apart): 6 ticks -> 2 pews', pewCount === 2);
g.setMouse(50, false);

console.log('== game over when lifes < 1, then back to menu ==');
s.lifes = 1;
s.rocks.length = 0;
placeRockAt('tiny', s.player.x, s.player.y, 10, 'meteorBrown_tiny1');
g.tick();
check('game over triggered at lifes 0', s.mode === 'over');
check('end music played (frozenjam, once)', t.musicLog.some(m => m[0] === 'frozenjam' && m[1] === false));
g.pressEnter();
check('enter from over -> menu', s.mode === 'menu');
g.pressEnter();
check('enter from menu -> ready again', s.mode === 'ready');
for (let i = 0; i < 80; i++) g.tick();
check('back in play', s.mode === 'play');

console.log('== hi-score persistence ==');
s.score = 12;
for (let i = 0; i < 3; i++) g.tick();
check('hi-score saved to storage', t.stored === 12 && s.hiScore === 12);

console.log('== pause ==');
g.togglePause();
check('paused flag set', s.paused === true);
const ticksAtPause = s.tickCount;
for (let i = 0; i < 100; i++) g.tick();
check('no ticks advance while paused', s.tickCount === ticksAtPause);
g.togglePause();
check('unpaused', s.paused === false);

console.log('== laser movement: +15 y per tick, gone after 22 ticks ==');
s.lasers.push({ x: 0, y: -145, prevY: -145, life: 22 });
for (let i = 0; i < 22; i++) g.tick();
check('laser removed after 22 ticks', s.lasers.length === 0);

console.log('== rock glides from y=145 toward y=-180 and despawns ==');
s.rocks.push({ kind: 'big', x0: 0, y0: 145, x1: 0, y1: -180, dur: 1, p: 0, step: 1 / 30, rot: 0, size: 100, radius: 30, img: 'meteorBrown_big1' });
g.tick();
const rock = s.rocks[s.rocks.length - 1];
// first tick: 1/30 of the 325px glide = ~10.8px downward from y=145
check('rock starts at y=145 and glides downward', rock.y < 145 && rock.y > 130);
for (let i = 0; i < 40; i++) g.tick();
check('rock despawned after glide', !s.rocks.includes(rock));

console.log(failures === 0 ? '\nALL TESTS PASSED' : '\n' + failures + ' TEST(S) FAILED');
process.exit(failures === 0 ? 0 : 1);

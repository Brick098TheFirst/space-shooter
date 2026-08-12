// Game Constants & Balance Data for Space Unlimited Android

export const VIRTUAL_WIDTH = 384;
export const VIRTUAL_HEIGHT = 216;

export const TARGET_PHYSICS_HZ = 120; // 120Hz physics accumulator
export const FIXED_TIMESTEP = 1000 / TARGET_PHYSICS_HZ; // 8.333ms

export const NUM_ACCENTS = 9;
export const NUM_TRAILS = 8;
export const NUM_RIGS = 8;
export const NUM_LASERS = 12;
export const NUM_UPGRADES = 8;
export const UPG_MAX_LEVEL = 5;

export const DIFFICULTIES = {
  CADET: 0,
  PILOT: 1,
  ACE: 2
};

export const DIFFICULTY_INFO = [
  { id: 0, name: 'Cadet', speedMult: 0.86, lifeBonus: 1, shieldBonus: 1, desc: 'Casual flight, slower hostiles' },
  { id: 1, name: 'Pilot', speedMult: 1.00, lifeBonus: 0, shieldBonus: 0, desc: 'Standard arcade combat' },
  { id: 2, name: 'Ace', speedMult: 1.21, lifeBonus: -1, shieldBonus: 0, desc: 'Tough, aggressive swarm' }
];

export const WEAPON_RIGS = [
  { id: 0, name: 'Single Blaster', price: 0, baseCd: 30, desc: 'Starter solo shot - 1 rock at a time', heavy: false, bolts: 1 },
  { id: 1, name: 'Twin Cannons', price: 500, baseCd: 26, desc: 'Dual balanced parallel cannons', heavy: false, bolts: 2 },
  { id: 2, name: 'Spread Cannon', price: 2500, baseCd: 28, desc: '3-way broad angle salvo', heavy: false, bolts: 3 },
  { id: 3, name: 'Focused Beam', price: 6500, baseCd: 22, desc: 'Heavy piercing concentrated beam', heavy: true, bolts: 1 },
  { id: 4, name: 'Triple Blaster', price: 14000, baseCd: 20, desc: 'Tri-barrel heavy assault barrage', heavy: true, bolts: 3 },
  { id: 5, name: 'Plasma Flak', price: 30000, baseCd: 18, desc: 'Twin heavy explosive plasma flak', heavy: true, bolts: 2 },
  { id: 6, name: 'Quantum Core', price: 65000, baseCd: 14, desc: 'Rapid high-energy drill array', heavy: true, bolts: 3 },
  { id: 7, name: 'Nova Annihilator', price: 150000, baseCd: 10, desc: '5x piercing devastation god weapon', heavy: true, bolts: 5 }
];

export const LASER_CRYSTALS = [
  { id: 0, name: 'Ion Basic', price: 0, bonusDmg: 0, colorIdx: 21, hex: '#23d6ff', desc: 'Starter weak crystal +0 dmg', special: 'basic' },
  { id: 1, name: 'Solar Gold', price: 1200, bonusDmg: 1, colorIdx: 24, hex: '#ffd24a', desc: 'Amber photon focus +1 dmg', special: 'none' },
  { id: 2, name: 'Nebula Violet', price: 3500, bonusDmg: 1, colorIdx: 28, hex: '#b46eff', desc: 'Purple ion bolt +1 dmg', special: 'none' },
  { id: 3, name: 'Toxic Mint', price: 7500, bonusDmg: 2, colorIdx: 27, hex: '#50f08c', desc: 'Toxic bio-plasma +2 dmg', special: 'none' },
  { id: 4, name: 'Crimson Fury', price: 15000, bonusDmg: 2, colorIdx: 26, hex: '#ff3c3c', desc: 'Thermal overload +2 dmg', special: 'none' },
  { id: 5, name: 'Emerald Surge', price: 30000, bonusDmg: 3, colorIdx: 62, hex: '#66ffb8', desc: 'High-frequency gamma +3 dmg', special: 'none' },
  { id: 6, name: 'Void Shadow', price: 60000, bonusDmg: 3, colorIdx: 116, hex: '#af5ff5', desc: 'Dark matter distortion +3 dmg', special: 'none' },
  { id: 7, name: 'Rainbow Laser', price: 100000, bonusDmg: 3, colorIdx: 120, hex: '#ff78be', desc: 'Chromatic wave + piercing beam', special: 'rainbow' },
  { id: 8, name: 'Inferno Red', price: 40000, bonusDmg: 2, colorIdx: 70, hex: '#ff4646', desc: 'Incendiary scorch +2 dmg', special: 'none' },
  { id: 9, name: 'Frost Blue', price: 70000, bonusDmg: 3, colorIdx: 54, hex: '#2ad6ff', desc: 'Sub-zero cryo shock +3 dmg', special: 'none' },
  { id: 10, name: 'Photon Gold', price: 110000, bonusDmg: 4, colorIdx: 66, hex: '#ffd24a', desc: 'Concentrated solar blast +4 dmg', special: 'none' },
  { id: 11, name: 'Omega Prism', price: 250000, bonusDmg: 5, colorIdx: 78, hex: '#ff5ae6', desc: 'GOD! Heavy pierce +5 dmg & cooldown boost', special: 'omega' }
];

export const SHIP_PAINTS = [
  { id: 0, name: 'Solar Orange', price: 800, hex: '#ff7838', desc: 'Fleet amber heat shield coating' },
  { id: 1, name: 'Ion Cyan', price: 0, hex: '#2ad6ff', desc: 'Cadet issue cobalt armor' },
  { id: 2, name: 'Nova Violet', price: 2500, hex: '#bc5cff', desc: 'Nebula violet stealth finish' },
  { id: 3, name: 'Plasma Mint', price: 5500, hex: '#66ffb8', desc: 'Bio-polymer mint nanite composite' },
  { id: 4, name: 'Pulsar Gold', price: 14000, hex: '#ffd24a', desc: 'Gilded royal flagship armor' },
  { id: 5, name: 'Crimson Void', price: 30000, hex: '#ff4646', desc: 'Raider crimson battle plate' },
  { id: 6, name: 'Obsidian Dark', price: 65000, hex: '#707d91', desc: 'Radar-absorbing titanium shadow' },
  { id: 7, name: 'Quantum Neon', price: 120000, hex: '#ff5ae6', desc: 'Zero-point fluorescent overdrive' },
  { id: 8, name: 'Rainbow Prism', price: 1000000, hex: '#ffffff', animated: true, desc: 'Animated dynamic chromatic spectrum wave' }
];

export const ENGINE_TRAILS = [
  { id: 0, name: 'Ember Fire', price: 1000, hex: '#ff7838', rgb: [255, 120, 56], desc: 'Combustion afterburner flame' },
  { id: 1, name: 'Ion Cyan', price: 0, hex: '#2ad6ff', rgb: [42, 214, 255], desc: 'Sub-light ion drive wake' },
  { id: 2, name: 'Nova Purple', price: 3200, hex: '#bc5cff', rgb: [188, 92, 255], desc: 'Exotic tachyon particle plume' },
  { id: 3, name: 'Aurora Mint', price: 7000, hex: '#66ffb8', rgb: [102, 255, 184], desc: 'Plasma jet stream exhaust' },
  { id: 4, name: 'Solar Gold', price: 16000, hex: '#ffd24a', rgb: [255, 210, 50], desc: 'Photon flare thrust wake' },
  { id: 5, name: 'Crimson Flame', price: 35000, hex: '#ff3c3c', rgb: [255, 60, 60], desc: 'Heavy raider afterburner' },
  { id: 6, name: 'Void Shadow', price: 70000, hex: '#826eaa', rgb: [130, 110, 170], desc: 'Dark energy drive exhaust' },
  { id: 7, name: 'Rainbow Trail', price: 130000, hex: '#ffffff', animated: true, desc: 'Animated spectrum rainbow wake' }
];

export const TECH_UPGRADES = [
  {
    id: 0,
    name: 'Ion Engine',
    shortDesc: 'Ship speed 0.7x -> 2.0x',
    prices: [800, 2500, 7000, 18000, 40000],
    levelDescs: [
      'Lv0 Slow 0.70x',
      'Lv1 Agility 0.86x',
      'Lv2 Normal 1.00x',
      'Lv3 Fast 1.25x',
      'Lv4 Turbo 1.60x',
      'MAX Hyper 2.00x!'
    ],
    mults: [180/256, 220/256, 256/256, 320/256, 410/256, 512/256]
  },
  {
    id: 1,
    name: 'Fire Rate',
    shortDesc: '2 shots/sec -> 10+/sec',
    prices: [1000, 3000, 8000, 20000, 45000],
    levelDescs: [
      'Lv0 2 shots/sec',
      'Lv1 ~3.2 shots/sec',
      'Lv2 ~4.5 shots/sec',
      'Lv3 ~6.0 shots/sec',
      'Lv4 ~8.2 shots/sec',
      'MAX 10+ shots/sec!'
    ],
    cdMults: [256/256, 210/256, 165/256, 125/256, 95/256, 75/256]
  },
  {
    id: 2,
    name: 'Plasma Core',
    shortDesc: '+1..+5 damage to all guns',
    prices: [1500, 4000, 10000, 25000, 55000],
    levelDescs: [
      'Lv0 Base damage',
      'Lv1 +1 damage',
      'Lv2 +2 heavy damage',
      'Lv3 +3 god power',
      'Lv4 +4 devastation',
      'MAX +5 destruction!'
    ]
  },
  {
    id: 3,
    name: 'Shield Battery',
    shortDesc: 'Higher capacity & start shields',
    prices: [1200, 3500, 9000, 22000, 50000],
    levelDescs: [
      'Lv0: 0 start, 2 max',
      'Lv1: 1 start, 3 max',
      'Lv2: 1 start, 4 max',
      'Lv3: 2 start, 5 max',
      'Lv4: 2 start, 6 max',
      'MAX: 3 start, 6 cap!'
    ],
    startShields: [0, 1, 1, 2, 2, 3],
    maxShields: [2, 3, 4, 5, 6, 6]
  },
  {
    id: 4,
    name: 'Hull Plating',
    shortDesc: 'Extra ship lives (2 -> 7)',
    prices: [1200, 3500, 9000, 22000, 50000],
    levelDescs: [
      'Lv0: 2 lives (tough!)',
      'Lv1: 3 lives normal',
      'Lv2: 4 lives sturdy',
      'Lv3: 5 lives tank',
      'Lv4: 6 lives titan',
      'MAX: 7 lives beast!'
    ],
    maxLives: [2, 3, 4, 5, 6, 7]
  },
  {
    id: 5,
    name: 'Afterburner',
    shortDesc: 'Lower Dash cooldown & +invuln',
    prices: [800, 2500, 7000, 18000, 40000],
    levelDescs: [
      'Lv0: 1.4s cooldown',
      'Lv1: 1.1s (-20%)',
      'Lv2: 0.86s (-35%)',
      'Lv3: 0.66s (-50%)',
      'Lv4: 0.50s (-65%)',
      'MAX: 0.40s instant!'
    ],
    cooldowns: [84, 66, 52, 40, 30, 24],
    invulnTicks: [16, 19, 22, 25, 28, 31]
  },
  {
    id: 6,
    name: 'Graviton Magnet',
    shortDesc: '+Coins & magnetic pull',
    prices: [1000, 3000, 8000, 20000, 45000],
    levelDescs: [
      'Lv0: 1x coins, no magnet',
      'Lv1: +35% $ & small pull',
      'Lv2: +70% $ & med pull',
      'Lv3: +105% $ & big pull',
      'Lv4: +140% $ & huge pull',
      'MAX: +175% $ & vortex!'
    ],
    coinMults: [1.0, 1.35, 1.70, 2.05, 2.40, 2.75]
  },
  {
    id: 7,
    name: 'Overdrive Unit',
    shortDesc: 'Rapid Fire powerup duration',
    prices: [1000, 3000, 7500, 18000, 40000],
    levelDescs: [
      'Lv0: Rapid fire 8 sec',
      'Lv1: Rapid fire 11 sec',
      'Lv2: Rapid fire 14 sec',
      'Lv3: Rapid fire 18 sec',
      'Lv4: Rapid fire 22 sec',
      'MAX: Rapid fire 26 sec!'
    ],
    durations: [480, 660, 840, 1080, 1320, 1560]
  }
];

export const RAINBOW_COLORS_RGB = [
  [255, 70, 70],   // Red
  [255, 120, 56],  // Orange
  [255, 210, 74],  // Gold
  [102, 255, 184], // Mint
  [42, 214, 255],  // Cyan
  [188, 92, 255],  // Violet
  [255, 90, 230]   // Pink
];

export const formatPrice = (price) => {
  if (price >= 1000000) return `${Math.floor(price / 1000000)}M`;
  if (price >= 1000) {
    if (price % 1000 === 0) return `${Math.floor(price / 1000)}k`;
    return `${(price / 1000).toFixed(1)}k`;
  }
  return `${price}c`;
};

// Authentic Pixelated Sprite & Font Renderer for Space Unlimited Android

import { GFX_DATA } from './gfx-data.js';
import { RAINBOW_COLORS_RGB } from './constants.js';

const {
  palette,
  spr_ship,
  spr_ast_large,
  spr_ast_med_a,
  spr_ast_med_b,
  spr_ast_small,
  spr_ast_tiny,
  spr_drone,
  spr_laser_standard,
  spr_laser_heavy,
  spr_shield_bubble,
  spr_pwr_shield,
  spr_pwr_rapid,
  spr_pwr_repair,
  spr_explosion,
  font_5x7
} = GFX_DATA;

const RAINBOW_ACCENT_STEPS = [5, 0, 4, 3, 1, 2, 7]; // Crimson, Orange, Gold, Mint, Cyan, Violet, Pink

// Palette color helpers
export const getColorRgb = (idx) => {
  if (idx < 0 || idx >= palette.length) return [0, 0, 0];
  return palette[idx];
};

export const getColorHex = (idx) => {
  const [r, g, b] = getColorRgb(idx);
  return `rgb(${r},${g},${b})`;
};

export class SpriteRenderer {
  constructor() {
    this.rainbowAccents = RAINBOW_ACCENT_STEPS;
    this.rainbowColors = RAINBOW_COLORS_RGB;
  }

  // Draw 5x7 Retro Bitmap Character
  drawChar(ctx, x, y, char, colorRgb) {
    const code = char.charCodeAt(0);
    if (code < 32 || code > 127) return;
    const glyph = font_5x7[code - 32];
    if (!glyph) return;

    const [r, g, b] = colorRgb;
    ctx.fillStyle = `rgb(${r},${g},${b})`;

    for (let row = 0; row < 7; row++) {
      const bitRow = glyph[row];
      for (let col = 0; col < 5; col++) {
        if (bitRow & (1 << (4 - col))) {
          ctx.fillRect(Math.floor(x + col), Math.floor(y + row), 1, 1);
        }
      }
    }
  }

  // Draw Retro Bitmap Text
  drawText(ctx, x, y, text, colorRgb = [240, 246, 255]) {
    if (!text) return;
    const str = String(text);
    let curX = Math.floor(x);
    let curY = Math.floor(y);

    for (let i = 0; i < str.length; i++) {
      const c = str[i];
      if (c === '\n') {
        curX = Math.floor(x);
        curY += 9;
      } else {
        this.drawChar(ctx, curX, curY, c, colorRgb);
        curX += 6;
      }
    }
  }

  drawTextShadow(ctx, x, y, text, colorRgb = [240, 246, 255], shadowRgb = [10, 15, 25]) {
    this.drawText(ctx, x + 1, y + 1, text, shadowRgb);
    this.drawText(ctx, x, y, text, colorRgb);
  }

  drawTextCentered(ctx, x, y, width, text, colorRgb = [240, 246, 255]) {
    if (!text) return;
    const textWidth = text.length * 6 - 1;
    const startX = Math.floor(x + (width - textWidth) / 2);
    this.drawText(ctx, startX, y, text, colorRgb);
  }

  // Generic 1D/2D indexed pixel array renderer
  drawIndexedPixels(ctx, destX, destY, width, height, pixels, customColorFn = null) {
    const startX = Math.floor(destX);
    const startY = Math.floor(destY);

    for (let sy = 0; sy < height; sy++) {
      for (let sx = 0; sx < width; sx++) {
        const pix = pixels[sy * width + sx];
        if (pix === 0) continue;

        let rgb;
        if (customColorFn) {
          rgb = customColorFn(pix, sx, sy);
          if (!rgb) continue;
        } else {
          rgb = palette[pix];
        }

        ctx.fillStyle = `rgb(${rgb[0]},${rgb[1]},${rgb[2]})`;
        ctx.fillRect(startX + sx, startY + sy, 1, 1);
      }
    }
  }

  // Ship renderer supporting all 9 paints + animated Rainbow Chroma
  drawShip(ctx, x, y, accentIdx = 1, animFrame = 0) {
    const px = Math.floor(x);
    const py = Math.floor(y);
    const safeAccent = Math.max(0, Math.min(8, accentIdx));

    if (safeAccent !== 8) {
      // Standard static paint (0..7)
      this.drawIndexedPixels(ctx, px, py, 20, 16, spr_ship[safeAccent]);
      return;
    }

    // Dynamic Rainbow Chroma Paint (8): flowing chromatic wave
    const pixels = spr_ship[8];
    this.drawIndexedPixels(ctx, px, py, 20, 16, pixels, (pix, sx, sy) => {
      if (pix >= 240 && pix <= 243) {
        const shade = pix - 240;
        const phase = (Math.floor(animFrame / 2) + sx * 2 + sy) % 28;
        const step = Math.floor(phase / 4); // 0..6
        const baseAccent = this.rainbowAccents[step];
        const palIdx = 48 + baseAccent * 4 + shade;
        return palette[palIdx];
      }
      return palette[pix];
    });
  }

  // Enemy Hunter Drone (Crimson ship rotated 180 degrees)
  drawEnemyShip(ctx, x, y) {
    const startX = Math.floor(x);
    const startY = Math.floor(y);
    const src = spr_ship[5]; // Crimson paint

    for (let sy = 0; sy < 16; sy++) {
      for (let sx = 0; sx < 20; sx++) {
        const pix = src[(15 - sy) * 20 + (19 - sx)];
        if (pix !== 0) {
          const rgb = palette[pix];
          ctx.fillStyle = `rgb(${rgb[0]},${rgb[1]},${rgb[2]})`;
          ctx.fillRect(startX + sx, startY + sy, 1, 1);
        }
      }
    }
  }

  // Asteroid Renderer
  drawAsteroid(ctx, x, y, type) {
    const px = Math.floor(x);
    const py = Math.floor(y);
    switch (type) {
      case 0: // Large 24x24
        this.drawIndexedPixels(ctx, px - 12, py - 12, 24, 24, spr_ast_large);
        break;
      case 1: // Med A 16x16
        this.drawIndexedPixels(ctx, px - 8, py - 8, 16, 16, spr_ast_med_a);
        break;
      case 2: // Med B 16x16
        this.drawIndexedPixels(ctx, px - 8, py - 8, 16, 16, spr_ast_med_b);
        break;
      case 3: // Small 10x10
        this.drawIndexedPixels(ctx, px - 5, py - 5, 10, 10, spr_ast_small);
        break;
      case 4: // Tiny 6x6
      default:
        this.drawIndexedPixels(ctx, px - 3, py - 3, 6, 6, spr_ast_tiny);
        break;
    }
  }

  // Laser Renderer with all 12 crystals & special animated rainbow/omega
  drawLaser(ctx, centerX, centerY, heavy = false, laserIdx = 0, animFrame = 0, downward = false) {
    const w = heavy ? 6 : 4;
    const h = heavy ? 14 : 10;
    const startX = Math.floor(centerX - w / 2);
    const startY = Math.floor(centerY - h / 2);
    const rawPixels = heavy ? spr_laser_heavy : spr_laser_standard;

    // Palette mappings for 12 lasers
    const laserColors = [21, 24, 28, 27, 26, 62, 116, 120, 70, 54, 66, 78];
    const baseColorIdx = laserColors[laserIdx] || 21;
    const isRainbow = (laserIdx === 7);
    const isOmega = (laserIdx === 11);

    for (let drawY = 0; drawY < h; drawY++) {
      const sy = downward ? (h - 1 - drawY) : drawY;
      for (let sx = 0; sx < w; sx++) {
        const pix = rawPixels[sy * w + sx];
        if (pix === 0) continue;

        let rgb;
        if (isRainbow) {
          if (pix === 16 || pix === 13) {
            rgb = [255, 255, 255]; // white core
          } else {
            const phase = Math.floor(animFrame / 2) + sy * 2 + sx;
            rgb = this.rainbowColors[Math.abs(phase) % 7];
          }
        } else if (isOmega) {
          if (pix === 16 || pix === 13) {
            rgb = [255, 255, 255];
          } else {
            const phase = Math.floor(animFrame / 3) + sy + sx;
            rgb = this.rainbowColors[Math.abs(phase) % 7];
          }
        } else {
          if (pix === 21 || pix === 22 || pix === 23) {
            rgb = palette[baseColorIdx];
          } else {
            rgb = palette[pix];
          }
        }

        ctx.fillStyle = `rgb(${rgb[0]},${rgb[1]},${rgb[2]})`;
        ctx.fillRect(startX + sx, startY + drawY, 1, 1);
      }
    }
  }

  // Shield Bubble
  drawShield(ctx, x, y, pulsePhase = 0) {
    const px = Math.floor(x - 12);
    const py = Math.floor(y - 12);
    this.drawIndexedPixels(ctx, px, py, 24, 24, spr_shield_bubble, (pix) => {
      const t = Math.sin(pulsePhase * 0.1) * 0.2 + 0.8;
      const rgb = palette[pix];
      return [
        Math.floor(rgb[0] * t),
        Math.floor(rgb[1] * t),
        Math.floor(rgb[2])
      ];
    });
  }

  // Powerup Icon
  drawPowerup(ctx, x, y, type) {
    const px = Math.floor(x - 5);
    const py = Math.floor(y - 5);
    let spr = spr_pwr_shield;
    if (type === 1) spr = spr_pwr_rapid;
    else if (type === 2) spr = spr_pwr_repair;
    this.drawIndexedPixels(ctx, px, py, 10, 10, spr);
  }

  // Animated Explosion
  drawExplosion(ctx, x, y, frame = 0) {
    const safeFrame = Math.max(0, Math.min(8, Math.floor(frame)));
    const px = Math.floor(x - 12);
    const py = Math.floor(y - 12);
    this.drawIndexedPixels(ctx, px, py, 24, 24, spr_explosion[safeFrame]);
  }

  // UI Glass Card
  drawGlassCard(ctx, x, y, w, h, borderColor = '#23d6ff', fillColor = 'rgba(6, 10, 20, 0.85)') {
    const px = Math.floor(x);
    const py = Math.floor(y);
    const pw = Math.floor(w);
    const ph = Math.floor(h);

    ctx.fillStyle = fillColor;
    ctx.fillRect(px + 1, py + 1, pw - 2, ph - 2);

    ctx.strokeStyle = borderColor;
    ctx.lineWidth = 1;
    ctx.strokeRect(px + 0.5, py + 0.5, pw - 1, ph - 1);
  }

  // UI Progress Bar
  drawProgressBar(ctx, x, y, w, h, current, maxVal, fgHex = '#23d6ff', bgHex = '#1a2233') {
    const px = Math.floor(x);
    const py = Math.floor(y);
    const pw = Math.floor(w);
    const ph = Math.floor(h);

    ctx.fillStyle = bgHex;
    ctx.fillRect(px, py, pw, ph);

    if (maxVal > 0) {
      const fillW = Math.max(0, Math.min(pw - 2, Math.floor((current * (pw - 2)) / maxVal)));
      if (fillW > 0) {
        ctx.fillStyle = fgHex;
        ctx.fillRect(px + 1, py + 1, fillW, ph - 2);
      }
    }

    ctx.strokeStyle = '#3c5a8c';
    ctx.lineWidth = 1;
    ctx.strokeRect(px + 0.5, py + 0.5, pw - 1, ph - 1);
  }
}

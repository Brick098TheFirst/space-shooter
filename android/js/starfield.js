// Widescreen Parallax Starfield & Deep Space Nebulae for Android

import { VIRTUAL_WIDTH, VIRTUAL_HEIGHT } from './constants.js';

export class Starfield {
  constructor(width = VIRTUAL_WIDTH, height = VIRTUAL_HEIGHT) {
    this.width = width;
    this.height = height;
    this.stars = [];
    this.nebulae = [];
    this.init();
  }

  init() {
    this.stars = [];
    const numStars = 60; // wider starfield
    for (let i = 0; i < numStars; i++) {
      const layer = i % 3;
      let speed, color;
      if (layer === 0) {
        speed = 0.25 + Math.random() * 0.25;
        color = 'rgb(40, 60, 105)'; // faint distant star
      } else if (layer === 1) {
        speed = 0.6 + Math.random() * 0.45;
        color = 'rgb(80, 120, 190)'; // mid star
      } else {
        speed = 1.2 + Math.random() * 0.8;
        color = 'rgb(220, 240, 255)'; // bright foreground star
      }

      this.stars.push({
        x: Math.random() * this.width,
        y: Math.random() * this.height,
        speed,
        color,
        layer,
        phase: Math.random() * 100
      });
    }

    this.nebulae = [];
    const numNebulae = 6;
    for (let i = 0; i < numNebulae; i++) {
      this.nebulae.push({
        x: Math.random() * (this.width - 60) + 30,
        y: Math.random() * this.height,
        radius: 20 + Math.random() * 25,
        speed: 0.12 + Math.random() * 0.15,
        color: i % 2 === 0 ? 'rgba(12, 18, 45, 0.4)' : 'rgba(25, 12, 45, 0.35)'
      });
    }
  }

  update(speedMult = 1.0) {
    for (const star of this.stars) {
      star.y += star.speed * speedMult;
      star.phase += 0.08;
      if (star.y >= this.height) {
        star.y = 0;
        star.x = Math.random() * this.width;
      }
    }

    for (const neb of this.nebulae) {
      neb.y += neb.speed * speedMult;
      if (neb.y >= this.height + neb.radius) {
        neb.y = -neb.radius;
        neb.x = Math.random() * (this.width - 60) + 30;
      }
    }
  }

  draw(ctx, offsetX = 0, offsetY = 0) {
    // Clear space background
    ctx.fillStyle = '#050811';
    ctx.fillRect(0, 0, this.width, this.height);

    // Draw nebulae
    for (const neb of this.nebulae) {
      const nx = neb.x + offsetX;
      const ny = neb.y + offsetY;
      const r = neb.radius;

      ctx.fillStyle = neb.color;
      for (let dy = -r; dy <= r; dy += 3) {
        const span = Math.floor(r - Math.abs(dy));
        if (span > 0) {
          ctx.fillRect(Math.floor(nx - span), Math.floor(ny + dy), span * 2, 3);
        }
      }
    }

    // Draw stars
    for (const star of this.stars) {
      const sx = Math.floor(star.x + offsetX);
      const sy = Math.floor(star.y + offsetY);

      if (sx >= 0 && sx < this.width && sy >= 0 && sy < this.height) {
        if (star.layer === 2) {
          const twinkle = Math.sin(star.phase);
          if (twinkle > 0.6) {
            ctx.fillStyle = '#ffffff';
            ctx.fillRect(sx, sy, 1, 1);
            if (twinkle > 0.85) {
              ctx.fillStyle = 'rgba(35, 214, 255, 0.6)';
              ctx.fillRect(sx - 1, sy, 3, 1);
              ctx.fillRect(sx, sy - 1, 1, 3);
            }
            continue;
          }
        }
        ctx.fillStyle = star.color;
        ctx.fillRect(sx, sy, 1, 1);
      }
    }
  }
}

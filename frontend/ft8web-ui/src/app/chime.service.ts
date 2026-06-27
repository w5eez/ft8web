// SPDX-License-Identifier: GPL-3.0-or-later

import { Injectable } from '@angular/core';

@Injectable({ providedIn: 'root' })
export class ChimeService {
  private ctx?: AudioContext;
  private enabled = true;

  constructor() {
    try { this.enabled = localStorage.getItem('ft8web.chime') !== '0'; } catch {   }
    const unlock = () => { const c = this.ensure(); if (c && c.state === 'suspended') c.resume(); };
    window.addEventListener('pointerdown', unlock);
    window.addEventListener('keydown', unlock);
  }

  setEnabled(on: boolean): void {
    this.enabled = on;
    try { localStorage.setItem('ft8web.chime', on ? '1' : '0'); } catch {   }
  }
  isEnabled(): boolean { return this.enabled; }

  private ensure(): AudioContext | undefined {
    if (!this.ctx) {
      const Ctor = (window as unknown as {
        AudioContext?: typeof AudioContext; webkitAudioContext?: typeof AudioContext;
      });
      const C = Ctor.AudioContext || Ctor.webkitAudioContext;
      if (C) this.ctx = new C();
    }
    return this.ctx;
  }

  qsoComplete(): void {
    if (!this.enabled) return;
    const ctx = this.ensure();
    if (!ctx) return;
    if (ctx.state === 'suspended') ctx.resume();
    const t = ctx.currentTime;

    this.ping(ctx, 659.25, t + 0.00, 0.16);
    this.ping(ctx, 830.61, t + 0.11, 0.16);
    this.ping(ctx, 987.77, t + 0.22, 0.16);
    this.ping(ctx, 1318.51, t + 0.34, 0.34, 0.18);
  }

  private ping(ctx: AudioContext, freq: number, start: number, dur: number, gain = 0.22): void {
    const osc = ctx.createOscillator();
    const g = ctx.createGain();
    osc.type = 'sine';
    osc.frequency.value = freq;

    g.gain.setValueAtTime(0.0001, start);
    g.gain.exponentialRampToValueAtTime(gain, start + 0.012);
    g.gain.exponentialRampToValueAtTime(0.0001, start + dur);
    osc.connect(g).connect(ctx.destination);
    osc.start(start);
    osc.stop(start + dur + 0.03);
  }
}

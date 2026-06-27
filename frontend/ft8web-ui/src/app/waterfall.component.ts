// SPDX-License-Identifier: GPL-3.0-or-later

import { Component, ElementRef, ViewChild, AfterViewInit, OnInit, OnDestroy, HostListener, NgZone } from '@angular/core';
import { CommonModule } from '@angular/common';
import { Subscription } from 'rxjs';
import { WsService } from './ws.service';
import { ApiService } from './api.service';
import { ConfigService } from './config.service';
import { Decode } from './models';
import { senderOf } from './maidenhead';

const BW: Record<string, number> = {
  FT8: 50, FT4: 83, FT2: 167, WSPR: 6,
};

@Component({
  selector: 'app-waterfall',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="wf-wrap">
      <div class="txstrip" (pointerdown)="onStripDown($event)">
        <div class="subband" *ngIf="txMin() > 0 || txMax() < maxHz"
             [style.left.%]="pct(txMin())" [style.width.%]="pct(txMax() - txMin())"
             title="valid TX range for this mode"></div>
        <div class="txmark" [style.left.%]="pct(markLeft())" [style.width.%]="pctw(bw)">
          <div class="goal"></div>
          <div class="knob" [style.left.%]="knobPct()"></div>
        </div>
        <div class="txlabel" [style.left.%]="pct(markLeft() + bw / 2)">TX {{ tx }}</div>
        <div class="slots" (pointerdown)="$event.stopPropagation()"
             title="Period slots: the highlighted one is being received now (matches the colored stripe at the waterfall's left edge). TX = the slot you transmit in (Settings → Transmit).">
          <span class="slot even" [class.on]="curEven">EVEN<i *ngIf="txEven">TX</i></span>
          <span class="slot odd" [class.on]="!curEven">ODD<i *ngIf="!txEven">TX</i></span>
        </div>
      </div>
      <div #wfcv class="wfcv">
        <canvas #cv class="wf" (click)="onCanvasClick($event)"
                (pointerdown)="onWfDown($event)" (pointermove)="onWfMove($event)"
                (pointerup)="onWfUp($event)" (pointercancel)="onWfUp($event)"></canvas>
        <div #labels class="wflab"></div>
        <button class="wfauto" (click)="autoTx()" title="Pick a clear TX frequency">Auto</button>
        <div class="wfctl">
          <input type="range" min="0.4" max="4" step="0.1" [value]="gain"
                 (input)="onGainInput($event)" (change)="saveGain()" title="waterfall scale / brightness">
          <select (change)="setPalette($event)" title="colour palette">
            <option *ngFor="let p of paletteNames" [value]="p" [selected]="p === paletteName">{{ p }}</option>
          </select>
        </div>
      </div>
      <div class="axis">
        <span *ngFor="let t of ticks()" [style.left.%]="pct(t)">{{ t / 1000 | number:'1.1-2' }}k</span>
      </div>
    </div>`,
  styles: [`
    .wf-wrap { position:relative; touch-action:none; }
    .txstrip { position:relative; height:20px; background:#0b1118; cursor:ew-resize;
               touch-action:none; border:1px solid #1c2530; border-bottom:0; border-radius:6px 6px 0 0; }
    .subband { position:absolute; top:0; bottom:0; background:rgba(240,136,62,.18);
               border-left:1px solid #7a5a1e; border-right:1px solid #7a5a1e; pointer-events:none; }
    .txmark { position:absolute; bottom:0; height:100%; min-width:10px; pointer-events:none; z-index:3; }
    .goal { position:absolute; bottom:0; left:0; right:0; height:12px; box-sizing:border-box;
            border:2px solid #f85149; border-top-width:3px; border-bottom:0; }
    .knob { position:absolute; left:50%; bottom:9px; width:10px; height:10px; border-radius:50%;
            background:#f85149; transform:translateX(-50%); box-shadow:0 0 0 2px #0b1118; }
    .txlabel { position:absolute; bottom:calc(100% + 1px); transform:translateX(-50%);
               font:10px ui-monospace, monospace; color:#ffb3ae; pointer-events:none; z-index:3; white-space:nowrap; }
    .slots { position:absolute; right:4px; bottom:2px; display:flex; gap:4px; z-index:4; cursor:default; }
    .slot { display:flex; align-items:center; gap:4px; padding:1px 6px; border-radius:3px;
            font:600 9px ui-monospace, monospace; color:#54626e; background:#0d141d;
            border:1px solid #1c2530; }
    .slot.even.on { background:#1d3527; color:#3fb950; border-color:#2ea043; }
    .slot.odd.on  { background:#2b2240; color:#a371f7; border-color:#8957e5; }
    .slot i { font-style:normal; font-weight:700; font-size:8px; background:#f0883e;
              color:#241405; border-radius:2px; padding:0 3px; letter-spacing:.5px; }

    @media (pointer: coarse) {
      .txstrip { height:34px; }
      .goal { height:16px; }
      .knob { bottom:14px; width:16px; height:16px; }
      .txlabel { font-size:12px; }
    }
    .wf { width:100%; height:240px; display:block; background:#04060a;
          border-left:1px solid #1c2530; border-right:1px solid #1c2530; cursor:crosshair;
          touch-action:none; }
    .wfcv { position:relative; overflow:hidden; }
    .wflab { position:absolute; inset:0; overflow:hidden; pointer-events:none; }
    .wfauto { position:absolute; top:4px; left:6px; z-index:5; touch-action:auto;
              background:rgba(8,12,18,.66); border:1px solid #1c2530; border-radius:4px;
              color:#9fb0bf; font:600 11px ui-monospace, monospace; padding:2px 9px; cursor:pointer; }
    .wfauto:hover { color:#eef4ff; border-color:#2ea043; }
    .wfauto:active { background:rgba(46,160,67,.28); color:#eef4ff; }
    .wfctl { position:absolute; top:4px; right:6px; z-index:5; display:flex; gap:6px; align-items:center;
             padding:2px 6px; background:rgba(8,12,18,.66); border:1px solid #1c2530; border-radius:4px;
             touch-action:auto; }
    .wfctl input[type=range] { width:78px; height:14px; accent-color:#3fb950; cursor:pointer; }
    .wfctl select { background:#0d141d; color:#9fb0bf; border:1px solid #1c2530; border-radius:3px;
                    font:600 10px ui-monospace, monospace; padding:1px 2px; cursor:pointer; }
    .axis { position:relative; height:16px; background:#0b1118; border:1px solid #1c2530;
            border-top:0; border-radius:0 0 6px 6px; }
    .axis span { position:absolute; transform:translateX(-50%); font:10px ui-monospace, monospace;
                 color:#6b7a86; top:2px; }
  `],
})
export class WaterfallComponent implements AfterViewInit, OnInit, OnDestroy {
  @ViewChild('cv', { static: true }) cv!: ElementRef<HTMLCanvasElement>;
  @ViewChild('wfcv', { static: true }) wfcv!: ElementRef<HTMLDivElement>;
  @ViewChild('labels', { static: true }) labels!: ElementRef<HTMLDivElement>;
  tx = 1500;
  bw = 50;
  curEven = true;
  txEven = true;
  radioMode = 'FT8';
  private period = 15;
  private offsetMs = 0;
  readonly maxHz = 3000;

  txMin(): number { return this.radioMode === 'WSPR' ? 1400 : 0; }
  txMax(): number { return this.radioMode === 'WSPR' ? 1600 : this.maxHz; }
  snap(): number { return this.radioMode === 'WSPR' ? 5 : 25; }
  viewMin(): number { return this.radioMode === 'WSPR' ? 1400 : this.vLo; }
  viewMax(): number { return this.radioMode === 'WSPR' ? 1600 : this.vHi; }

  private centered(): boolean { return this.radioMode === 'WSPR'; }
  markLeft(): number { return this.centered() ? this.tx - this.bw / 2 : this.tx; }

  knobPct(): number { return this.centered() ? 50 : 0; }
  ticks(): number[] {
    if (this.radioMode === 'WSPR') return [1400, 1450, 1500, 1550, 1600];
    const lo = this.viewMin(), hi = this.viewMax(), span = hi - lo || 1;
    let step = 500;
    for (const s of [100, 200, 250, 500, 1000]) { if (span / s <= 7) { step = s; break; } }
    const out: number[] = [];
    for (let t = Math.ceil(lo / step) * step; t <= hi + 0.5; t += step) out.push(t);
    return out;
  }
  private ctx!: CanvasRenderingContext2D;
  private subs: Subscription[] = [];
  private dragging = false;

  private vLo = 0;
  private vHi = 3000;
  private readonly minSpan = 300;
  private ptrs = new Map<number, number>();
  private pinchD0 = 0;
  private pinchSpan0 = 0;
  private pinchFrac = 0;
  private pinchCenterHz = 0;
  private panX = 0;
  private moved = 0;
  private suppressClick = false;

  private pendingTx: number | null = null;
  private pendingTxAt = 0;
  private readonly W = 512;
  private readonly H = 240;

  gain = 1;
  paletteName = 'Classic';
  readonly paletteNames = Object.keys(PALETTES);
  private lut = new Uint8Array(256 * 3);
  private wfDragging = false;
  private showCalls = true;

  private recentLabels: { xPx: number; bottomY: number; t: number }[] = [];
  private readonly LABEL_MS = 24000;
  private readonly WF_HZ = 2;
  private readonly ROWPX = Math.max(1, Math.round(this.H / (this.LABEL_MS / 1000) / this.WF_HZ));
  private curBins: Uint8Array | null = null;
  private curTs = 0;
  private raf = 0;
  private lastTs = 0;
  private scrollAccum = 0;
  private visHandler?: () => void;
  private hiddenSnap: ImageData | null = null;

  constructor(private ws: WsService, private api: ApiService, private config: ConfigService,
              private zone: NgZone) {}

  ngOnInit(): void {
    this.buildLut();
    this.subs.push(this.ws.status$.subscribe((s) => {
      if (!s) return;
      this.bw = BW[s.mode] ?? 50;
      this.period = s.period || 15;
      this.offsetMs = s.t_ms - Date.now();
      if (s.mode !== this.radioMode) {
        this.radioMode = s.mode;
        this.vLo = 0; this.vHi = this.maxHz; this.applyZoom();

        if (s.tx < this.txMin() || s.tx > this.txMax()) {
          this.tx = this.clampTx(1500);
          this.sendTx();
          return;
        }
      }

      if (!this.dragging) {
        const st = this.clampTx(s.tx);
        if (this.pendingTx === null) {
          this.tx = st;
        } else if (st === this.pendingTx || Date.now() - this.pendingTxAt > 1500) {
          this.pendingTx = null;
          this.tx = st;
        }
      }
    }));
    this.subs.push(this.config.config$.subscribe((c) => {
      this.txEven = c.tx_even;
      const sc = c.show_calls ?? true;
      if (sc !== this.showCalls) {
        this.showCalls = sc;
        if (sc) this.syncLabels();
        else { this.labels?.nativeElement.replaceChildren(); this.recentLabels = []; }
      }
      if (this.wfDragging) return;
      const g = c.wf_gain >= 0.4 && c.wf_gain <= 4 ? c.wf_gain : 1;
      const p = PALETTES[c.wf_palette] ? c.wf_palette : 'Classic';
      if (g !== this.gain || p !== this.paletteName) {
        this.gain = g; this.paletteName = p; this.buildLut();
      }
    }));
  }

  ngAfterViewInit(): void {
    const c = this.cv.nativeElement;
    c.width = this.W;
    c.height = this.H;
    this.ctx = c.getContext('2d')!;
    this.ctx.fillStyle = '#04060a';
    this.ctx.fillRect(0, 0, this.W, this.H);

    this.zone.runOutsideAngular(() => {
      this.subs.push(this.ws.spectrum$.subscribe((b) => this.draw(b)));
      this.subs.push(this.ws.spectrumHistory$.subscribe((rows) => {

        for (const r of rows) this.drawBand(r.bins, this.ROWPX, r.t);
      }));
      this.raf = requestAnimationFrame(this.animate);

      this.subs.push(this.ws.decode$.subscribe((d) => this.placeLabel(d, 0)));

      this.syncLabels();
      this.visHandler = () => {
        if (document.hidden) {
          this.hiddenSnap = this.ctx.getImageData(0, 0, this.W, this.H);
        } else {
          if (this.hiddenSnap) { this.ctx.putImageData(this.hiddenSnap, 0, 0); this.hiddenSnap = null; }
          this.lastTs = 0;
          this.syncLabels();
        }
      };
      document.addEventListener('visibilitychange', this.visHandler);
    });
  }

  ngOnDestroy(): void {
    this.subs.forEach((s) => s.unsubscribe());
    if (this.raf) cancelAnimationFrame(this.raf);
    if (this.visHandler) document.removeEventListener('visibilitychange', this.visHandler);
  }

  pct(hz: number): number {
    return ((hz - this.viewMin()) / (this.viewMax() - this.viewMin())) * 100;
  }
  pctw(hz: number): number {
    return Math.min(100, (hz / (this.viewMax() - this.viewMin())) * 100);
  }

  onCanvasClick(ev: MouseEvent): void {
    if (this.dragging) return;
    if (this.suppressClick) { this.suppressClick = false; return; }
    this.setTx(this.hzFromX(ev.clientX));
  }

  onStripDown(ev: PointerEvent): void {
    ev.preventDefault();
    this.dragging = true;
    this.tx = this.snapClamp(this.hzFromX(ev.clientX));
  }

  @HostListener('document:pointermove', ['$event'])
  onMove(ev: PointerEvent): void {
    if (!this.dragging) return;
    this.tx = this.snapClamp(this.hzFromX(ev.clientX));
  }

  @HostListener('document:pointerup')
  onUp(): void {
    if (!this.dragging) return;
    this.dragging = false;
    this.sendTx();
  }

  onWfDown(ev: PointerEvent): void {
    if (ev.pointerType === 'mouse' || this.radioMode === 'WSPR') return;
    const first = this.ptrs.size === 0;
    this.ptrs.set(ev.pointerId, ev.clientX);
    (ev.target as Element).setPointerCapture?.(ev.pointerId);
    if (first) { this.moved = 0; this.suppressClick = false; this.panX = ev.clientX; }
    if (this.ptrs.size === 2) {
      const xs = [...this.ptrs.values()];
      this.pinchD0 = Math.abs(xs[0] - xs[1]) || 1;
      this.pinchSpan0 = this.vHi - this.vLo;
      const r = this.wfcv.nativeElement.getBoundingClientRect();
      this.pinchFrac = Math.min(1, Math.max(0, ((xs[0] + xs[1]) / 2 - r.left) / r.width));
      this.pinchCenterHz = this.vLo + this.pinchFrac * this.pinchSpan0;
      this.suppressClick = true;
    }
  }

  onWfMove(ev: PointerEvent): void {
    if (!this.ptrs.has(ev.pointerId)) return;
    const prev = this.ptrs.get(ev.pointerId)!;
    this.ptrs.set(ev.pointerId, ev.clientX);
    this.moved += Math.abs(ev.clientX - prev);
    const full = this.maxHz;
    if (this.ptrs.size >= 2) {
      const xs = [...this.ptrs.values()];
      const d = Math.abs(xs[0] - xs[1]) || 1;
      let span = Math.min(full, Math.max(this.minSpan, this.pinchSpan0 * this.pinchD0 / d));
      let lo = span >= full ? 0 : Math.min(full - span, Math.max(0, this.pinchCenterHz - this.pinchFrac * span));
      this.vLo = lo; this.vHi = lo + span;
      this.suppressClick = true;
      this.applyZoom();
    } else {
      const span = this.vHi - this.vLo;
      if (span < full) {
        const r = this.wfcv.nativeElement.getBoundingClientRect();
        const dHz = (ev.clientX - this.panX) / r.width * span;
        this.panX = ev.clientX;
        const lo = Math.min(full - span, Math.max(0, this.vLo - dHz));
        this.vLo = lo; this.vHi = lo + span;
        this.applyZoom();
      }
      if (this.moved > 8) this.suppressClick = true;
    }
  }

  onWfUp(ev: PointerEvent): void {
    if (!this.ptrs.delete(ev.pointerId)) return;
    if (this.ptrs.size === 1) this.panX = [...this.ptrs.values()][0];
    if (this.ptrs.size === 0 && this.suppressClick) this.syncLabels();
  }

  private applyZoom(): void {
    const c = this.cv?.nativeElement;
    if (!c) return;
    if (this.radioMode === 'WSPR') { c.style.transform = ''; return; }
    const full = this.maxHz;
    const s = full / (this.vHi - this.vLo);
    c.style.transformOrigin = '0 0';
    c.style.transform = 'scaleX(' + s + ') translateX(' + (-(this.vLo / full) * 100) + '%)';
    this.reflowLabels();
  }

  private reflowLabels(): void {
    if (!this.showCalls) return;
    const cont = this.labels?.nativeElement;
    if (!cont) return;
    for (const node of Array.from(cont.children)) {
      const el = node as HTMLElement;
      const p = this.pct(+(el.dataset['hz'] || '0'));
      el.style.left = p + '%';
      el.style.display = p < 1 || p > 99 ? 'none' : '';
    }
  }

  autoTx(): void {
    this.api.decodes().subscribe((res) => {
      const now = Date.now();
      const horizon = (4 * this.period + 4) * 1000;
      const occ: number[] = [];
      for (const d of res.decodes || []) {
        if (!d.freq) continue;
        if (now - this.utcToMs(d.utc) > horizon) continue;
        occ.push(d.freq);
      }
      const t = this.findClearTx(occ);
      if (t !== null) this.setTx(t);
    });
  }

  private findClearTx(occ: number[]): number | null {
    const lo = Math.max(300, this.txMin());
    const hi = Math.min(2700, this.txMax());
    const bw = this.bw, guard = 30, tMax = hi - bw;
    if (tMax <= lo) return null;
    const center = (lo + hi) / 2;
    const blocked = occ
      .map((f) => [f - bw - guard, f + bw + guard] as [number, number])
      .sort((a, b) => a[0] - b[0]);
    const gaps: [number, number][] = [];
    let cur = lo;
    for (const [bs, be] of blocked) {
      if (bs > cur) gaps.push([cur, Math.min(bs, tMax)]);
      if (be > cur) cur = be;
      if (cur >= tMax) break;
    }
    if (cur < tMax) gaps.push([cur, tMax]);

    let bestT: number | null = null, bestCD = Infinity;
    for (const [a, b] of gaps) {
      if (b <= a) continue;
      const t = Math.max(a, Math.min(b, center));
      const d = Math.abs(t - center);
      if (d < bestCD) { bestCD = d; bestT = t; }
    }
    if (bestT !== null) return this.centered() ? bestT + bw / 2 : bestT;

    let bestHz = lo, bestFar = -1;
    for (let f = lo; f <= tMax; f += 10) {
      let near = Infinity;
      for (const o of occ) near = Math.min(near, Math.abs(o - f));
      if (near > bestFar) { bestFar = near; bestHz = f; }
    }
    return this.centered() ? bestHz + bw / 2 : bestHz;
  }

  private setTx(hz: number): void {
    this.tx = this.snapClamp(hz);
    this.sendTx();
  }

  private sendTx(): void {
    this.pendingTx = this.tx;
    this.pendingTxAt = Date.now();
    this.api.setTxOffset(this.tx).subscribe();
  }

  private hzFromX(clientX: number): number {
    const r = this.wfcv.nativeElement.getBoundingClientRect();
    return this.viewMin() + ((clientX - r.left) / r.width) * (this.viewMax() - this.viewMin());
  }

  private snapClamp(hz: number): number {
    const s = this.snap();
    return this.clampTx(Math.round(hz / s) * s);
  }
  private clampTx(hz: number): number {
    if (this.centered()) {
      return Math.max(this.txMin() + this.bw / 2, Math.min(this.txMax() - this.bw / 2, hz));
    }
    return Math.max(this.txMin(), Math.min(this.txMax() - this.bw, hz));
  }

  private buildLut(): void {
    const pal = PALETTES[this.paletteName] ?? PALETTES['Classic'];
    for (let v = 0; v < 256; v++) {
      const [r, g, b] = pal(Math.min(255, v * this.gain) / 255);
      this.lut[v * 3] = r; this.lut[v * 3 + 1] = g; this.lut[v * 3 + 2] = b;
    }
  }

  onGainInput(ev: Event): void {
    this.wfDragging = true;
    this.gain = +(ev.target as HTMLInputElement).value;
    this.buildLut();
  }
  saveGain(): void {
    this.wfDragging = false;
    this.config.savePartial({ wf_gain: this.gain }).subscribe();
  }

  setPalette(ev: Event): void {
    this.paletteName = (ev.target as HTMLSelectElement).value;
    this.buildLut();
    this.config.savePartial({ wf_palette: this.paletteName }).subscribe();
  }

  private draw(bins: Uint8Array): void {
    this.curBins = bins;
    this.curTs = Date.now() + this.offsetMs;
  }

  private animate = (ts: number): void => {
    this.raf = requestAnimationFrame(this.animate);
    if (this.lastTs === 0) { this.lastTs = ts; return; }
    const dt = Math.min(ts - this.lastTs, 100);
    this.lastTs = ts;
    if (!this.curBins) return;
    this.scrollAccum += dt * (this.H / this.LABEL_MS);
    let px = Math.floor(this.scrollAccum);
    if (px < 1) return;
    if (px > this.H) px = this.H;
    this.scrollAccum -= px;
    const even = this.drawBand(this.curBins, px, this.curTs);
    if (even !== this.curEven) this.zone.run(() => { this.curEven = even; });
  };

  private drawBand(bins: Uint8Array, px: number, tMs: number): boolean {
    const ctx = this.ctx;
    const rh = px;
    ctx.drawImage(ctx.canvas, 0, 0, this.W, this.H - rh, 0, rh, this.W, this.H - rh);
    const row = ctx.createImageData(this.W, rh);
    const n = bins.length || 1;
    const lut = this.lut;
    for (let x = 0; x < this.W; x++) {
      const v = bins[Math.floor((x * n) / this.W)] ?? 0;
      const l = v * 3;
      const r = lut[l], g = lut[l + 1], b = lut[l + 2];
      for (let y = 0; y < rh; y++) {
        const o = (y * this.W + x) * 4;
        row.data[o] = r; row.data[o + 1] = g; row.data[o + 2] = b; row.data[o + 3] = 255;
      }
    }

    const even = Math.floor((tMs / 1000) / this.period) % 2 === 0;
    const [gr, gg2, gb] = even ? [46, 160, 67] : [163, 113, 247];
    for (let y = 0; y < rh; y++) {
      for (let x = 0; x < 4; x++) {
        const o = (y * this.W + x) * 4;
        row.data[o] = gr; row.data[o + 1] = gg2; row.data[o + 2] = gb; row.data[o + 3] = 255;
      }
    }
    ctx.putImageData(row, 0, 0);
    return even;
  }

  private placeLabel(d: Decode, ageMs: number): void {
    try {
      if (!this.showCalls) return;
      if (d.mode === 'WSPR' || !d.freq) return;
      const call = senderOf(d.message);
      if (!call) return;
      const xPct = this.pct(d.freq);
      if (xPct < 1 || xPct > 99) return;
      const remaining = this.LABEL_MS - ageMs;
      if (remaining <= 0) return;
      const cont = this.labels?.nativeElement;
      if (!cont) return;
      if (cont.childElementCount > 200) cont.firstElementChild?.remove();
      const timeMs = Date.now() - ageMs;
      const baseY  = (ageMs / this.LABEL_MS) * this.H;

      this.recentLabels = this.recentLabels.filter((r) => Math.abs(timeMs - r.t) < 2000);
      const xPx = (xPct / 100) * (cont.clientWidth || this.W);
      let startY = baseY;
      for (const r of this.recentLabels)
        if (Math.abs(r.xPx - xPx) < 26 && r.bottomY > startY) startY = r.bottomY;
      const el = document.createElement('b');
      el.textContent = call;
      el.dataset['hz'] = String(d.freq);

      el.style.cssText =
        'position:absolute;top:0;left:' + xPct + '%;writing-mode:vertical-rl;white-space:nowrap;' +
        'font:700 13px ui-monospace,monospace;letter-spacing:.5px;color:#eef4ff;' +
        'transform:translate(-50%,' + startY + 'px);text-shadow:0 0 3px #000,0 0 2px #000';
      cont.appendChild(el);
      this.recentLabels.push({ xPx, bottomY: startY + el.offsetHeight + 12, t: timeMs });
      const targetY = startY + (this.H - baseY);

      el.animate(
        [{ transform: 'translate(-50%,' + startY + 'px)' },
         { transform: 'translate(-50%,' + targetY + 'px)' }],
        { duration: Math.max(1, remaining), easing: 'linear', fill: 'forwards' });
      setTimeout(() => el.remove(), remaining);
    } catch { /* a label must never disturb the decode stream */ }
  }

  private syncLabels(): void {
    const cont = this.labels?.nativeElement;
    if (!cont) return;
    cont.replaceChildren();
    this.recentLabels = [];
    this.api.decodes().subscribe((res) => {
      const now = Date.now();

      const offset = this.period * 1000 + 1500;
      for (const d of (res.decodes || []).slice(-200))
        this.placeLabel(d, Math.max(0, (now - this.utcToMs(d.utc)) - offset));
    });
  }

  private utcToMs(utc: string): number {
    if (!/^\d{6}$/.test(utc)) return Date.now();
    const n = new Date();
    let ms = Date.UTC(n.getUTCFullYear(), n.getUTCMonth(), n.getUTCDate(),
                      +utc.slice(0, 2), +utc.slice(2, 4), +utc.slice(4, 6));
    if (ms - Date.now() > 12 * 3600 * 1000) ms -= 24 * 3600 * 1000;
    return ms;
  }
}

function ramp(stops: number[][], t: number): [number, number, number] {
  for (let i = 1; i < stops.length; i++) {
    if (t <= stops[i][0]) {
      const a = stops[i - 1], b = stops[i];
      const u = (t - a[0]) / (b[0] - a[0] || 1);
      return [Math.round(a[1] + (b[1] - a[1]) * u),
              Math.round(a[2] + (b[2] - a[2]) * u),
              Math.round(a[3] + (b[3] - a[3]) * u)];
    }
  }
  const e = stops[stops.length - 1];
  return [e[1], e[2], e[3]];
}

// HSV -> RGB (h,s,v in 0..1). Lets palettes rotate/oscillate hue procedurally
// instead of interpolating fixed colour stops, which is how the non-linear
// "creative" palettes get their character.
function hsv(h: number, s: number, v: number): [number, number, number] {
  h = ((h % 1) + 1) % 1;
  const i = Math.floor(h * 6) % 6, f = h * 6 - Math.floor(h * 6);
  const p = v * (1 - s), q = v * (1 - f * s), w = v * (1 - (1 - f) * s);
  const c = [[v, w, p], [q, v, p], [p, v, w], [p, q, v], [w, p, v], [v, p, q]][i];
  return [Math.round(c[0] * 255), Math.round(c[1] * 255), Math.round(c[2] * 255)];
}

const PALETTES: Record<string, (t: number) => [number, number, number]> = {
  Classic: (t) => {
    if (t < 0.25) { const u = t / 0.25;          return [0, 0, Math.round(40 + 160 * u)]; }
    if (t < 0.50) { const u = (t - 0.25) / 0.25; return [0, Math.round(190 * u), Math.round(200 - 40 * u)]; }
    if (t < 0.75) { const u = (t - 0.50) / 0.25; return [Math.round(235 * u), Math.round(190 + 50 * u), 0]; }
    const u = (t - 0.75) / 0.25;                  return [Math.round(235 + 20 * u), Math.round(240 - 210 * u), 0];
  },
  Aqua:      (t) => ramp([[0, 0, 8, 24], [0.4, 0, 80, 160], [0.7, 0, 200, 220], [1, 230, 255, 255]], t),
  Fire:      (t) => ramp([[0, 0, 0, 0], [0.3, 120, 0, 0], [0.6, 230, 90, 0], [0.85, 255, 220, 40], [1, 255, 255, 230]], t),
  Phosphor:  (t) => ramp([[0, 0, 8, 0], [0.5, 0, 140, 30], [0.8, 120, 230, 40], [1, 230, 255, 180]], t),
  Viridis:   (t) => ramp([[0, 68, 1, 84], [0.25, 59, 82, 139], [0.5, 33, 145, 140], [0.75, 94, 201, 98], [1, 253, 231, 37]], t),
  Turbo:     (t) => ramp([[0, 48, 18, 59], [0.15, 30, 110, 220], [0.35, 30, 200, 180], [0.5, 120, 230, 70], [0.65, 230, 215, 40], [0.8, 250, 130, 30], [1, 170, 25, 15]], t),
  Neon:      (t) => ramp([[0, 0, 0, 10], [0.3, 120, 0, 160], [0.55, 230, 30, 200], [0.78, 40, 220, 230], [1, 235, 255, 200]], t),
  Copper:    (t) => ramp([[0, 0, 0, 0], [0.35, 90, 45, 25], [0.7, 200, 120, 70], [0.9, 245, 190, 120], [1, 255, 235, 200]], t),
  Plasma:    (t) => ramp([[0, 13, 8, 135], [0.25, 126, 3, 168], [0.5, 204, 71, 120], [0.75, 248, 149, 64], [1, 240, 249, 33]], t),
  // procedural / non-linear: hue rotates or oscillates with intensity (sqrt
  // brightness keeps a dark noise floor), and Contour hard-bands into topo lines.
  Prism:     (t) => hsv(0.66 - t * 1.1, 0.95, Math.pow(t, 0.55)),
  Aurora:    (t) => hsv(0.55 + 0.25 * Math.sin(t * Math.PI * 2), 0.8, Math.pow(t, 0.6)),
  Contour:   (t) => { if (t < 0.05) return [3, 6, 10]; const b = Math.min(7, Math.floor(t * 8)); return hsv((0.58 + b * 0.12) % 1, 0.82, 0.6 + 0.055 * b); },
  Grayscale: (t) => { const v = Math.round(255 * t); return [v, v, v]; },
};

// SPDX-License-Identifier: GPL-3.0-or-later

import { Component, OnInit, AfterViewInit, OnDestroy, ViewChild, ElementRef, HostListener, NgZone, ChangeDetectorRef, Input } from '@angular/core';
import { CommonModule } from '@angular/common';
import { HttpClient } from '@angular/common/http';
import { Subscription } from 'rxjs';
import { DecodeStore, Spot } from './decode-store';
import { gridToLatLon } from './maidenhead';
import { ApiService } from './api.service';
const BANDS: ReadonlyArray<[number, number, string]> = [
  [1800000, 2000000, '160m'], [3500000, 4000000, '80m'], [5250000, 5450000, '60m'],
  [7000000, 7300000, '40m'], [10100000, 10150000, '30m'], [14000000, 14350000, '20m'],
  [18068000, 18168000, '17m'], [21000000, 21450000, '15m'], [24890000, 24990000, '12m'],
  [28000000, 29700000, '10m'], [50000000, 54000000, '6m'], [144000000, 148000000, '2m'],
  [420000000, 450000000, '70cm'],
];
function bandFor(hz: number): string {
  for (const [lo, hi, name] of BANDS) if (hz >= lo && hz <= hi) return name;
  return '';
}

const MODE_COLORS: Record<string, string> = {
  FT8: '#3fb950', FT4: '#1f6feb', FT2: '#d2a8ff', WSPR: '#f0883e',
};

interface TimeFilter { label: string; ms: number; }
interface MapLabel { n: string; x: number; y: number; }
interface MapExtras { borders: string[]; countries: MapLabel[]; states: MapLabel[]; }
type City = [string, number, number, string, string];
interface Hover { call: string; mode: string; grid: string; snr: number; place: string; x: number; y: number; }

@Component({
  selector: 'app-maps',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="maps">
      <div class="toolbar">
        <span class="title">{{ label }}
          <em *ngIf="source === 'heardBy' && !pskActive"> — PSK Reporter off (set callsign + enable in Settings)</em>
          <span class="next" *ngIf="source === 'heardBy' && pskActive">update in {{ countdown(nextAt()) }}</span>
        </span>
        <div class="filters">
          <span class="lbl">Window</span>
          <button *ngFor="let f of filters" [class.active]="f.ms === windowMs" (click)="setWindow(f.ms)">{{ f.label }}</button>
        </div>
        <div class="filters" *ngIf="bands().length">
          <span class="lbl">Band</span>
          <button [class.active]="bandFilter === ''" (click)="setBand('')">All</button>
          <button *ngFor="let b of bands()" [class.active]="bandFilter === b" (click)="setBand(b)">{{ b }}</button>
        </div>
        <span class="count">{{ shown.length }} stations</span>
        <div class="zoomctl">
          <button (click)="zoomBtn(1.4)">+</button>
          <button (click)="zoomBtn(1/1.4)">−</button>
          <button (click)="reset()">reset</button>
        </div>
        <div class="legend">
          <span class="lg" *ngFor="let m of legend"><i [style.background]="color(m)"></i>{{ m }}</span>
        </div>
      </div>
      <div class="mapbox">
        <svg #svg viewBox="0 0 360 180" preserveAspectRatio="xMidYMid meet"
             (pointerdown)="onPointerDown($event)">
          <rect x="0" y="0" width="360" height="180" fill="#0a1018"></rect>
          <g [attr.transform]="transform">
            <path *ngFor="let d of countryPaths" [attr.d]="d" fill="#15212e"
                  stroke="#2c4257" [attr.stroke-width]="0.18 / scale"></path>
            <path *ngFor="let d of borderPaths" [attr.d]="d" fill="none"
                  stroke="#24364a" [attr.stroke-width]="0.12 / scale" pointer-events="none"></path>
            <ng-container *ngIf="scale >= 2">
              <text *ngFor="let l of countryLabels" [attr.x]="l.x" [attr.y]="l.y"
                    [attr.font-size]="4.6 / scale" text-anchor="middle" fill="#7d93a8"
                    pointer-events="none" font-family="system-ui, sans-serif">{{ l.n }}</text>
            </ng-container>
            <ng-container *ngIf="scale >= 4">
              <text *ngFor="let l of stateLabels" [attr.x]="l.x" [attr.y]="l.y"
                    [attr.font-size]="3.2 / scale" text-anchor="middle" fill="#5d7488"
                    pointer-events="none" font-family="system-ui, sans-serif">{{ l.n }}</text>
            </ng-container>
            <g *ngFor="let s of shown; trackBy: trackPin">
              <circle [attr.cx]="s.lon! + 180" [attr.cy]="90 - s.lat!"
                      [attr.r]="pinR / scale" [attr.fill]="color(s.mode)" fill-opacity="0.85"
                      stroke="#04060a" [attr.stroke-width]="0.12 / scale" pointer-events="none"></circle>
              <circle class="pin" [attr.cx]="s.lon! + 180" [attr.cy]="90 - s.lat!"
                      [attr.r]="hitR / scale" fill="transparent" pointer-events="all"
                      (pointerdown)="onPinDown(s, $event)"
                      (pointerenter)="onPinEnter(s, $event)" (pointerleave)="onPinLeave($event)"></circle>
            </g>
          </g>
        </svg>
        <div class="zoomhint" [class.show]="hintVisible">Ctrl + scroll to zoom</div>
        <div class="tip" *ngIf="hovered" [style.left.px]="hovered.x" [style.top.px]="hovered.y">
          <div class="call">{{ hovered.call }}</div>
          <div class="row"><i [style.background]="color(hovered.mode)"></i>{{ hovered.mode }} · {{ hovered.grid }} · {{ hovered.snr }} dB</div>
          <div class="place" *ngIf="hovered.place">{{ hovered.place }}</div>
        </div>
      </div>
    </div>`,
  styles: [`
    .maps { display:flex; flex-direction:column; gap:10px; }
    .toolbar { display:flex; align-items:center; gap:14px; flex-wrap:wrap; }
    .title { font:600 13px system-ui, sans-serif; color:#e6edf3; flex-basis:100%; }
    .title em { color:#f0883e; font-style:normal; font-weight:400; font-size:12px; }
    .title .next { color:#8aa3b3; font:400 11px ui-monospace, monospace; margin-left:10px; }
    .filters, .zoomctl { display:flex; align-items:center; gap:6px; }
    .lbl { color:#6b7a86; font-size:11px; text-transform:uppercase; }
    .filters button, .zoomctl button { background:#13202c; color:#cdd9e3; border:1px solid #1c2a38;
        border-radius:4px; padding:5px 10px; cursor:pointer; font:12px ui-monospace, monospace; }
    .filters button.active { background:#1f6feb; color:#fff; border-color:#1f6feb; }
    .zoomctl button { min-width:30px; }
    .count { color:#8aa3b3; font:12px ui-monospace, monospace; }
    .legend { display:flex; gap:10px; flex-wrap:wrap; margin-left:auto; }
    .lg { display:flex; align-items:center; gap:4px; font:11px ui-monospace, monospace; color:#9fb0bd; }
    .lg i { width:10px; height:10px; border-radius:50%; display:inline-block; }
    .mapbox { position:relative; border:1px solid #1c2530; border-radius:8px; overflow:hidden; background:#0a1018; }
    svg { width:100%; height:auto; max-height:74vh; display:block; touch-action:none; cursor:grab; }
    svg:active { cursor:grabbing; }
    .pin { cursor:pointer; }
    .zoomhint { position:absolute; inset:0; display:flex; align-items:center; justify-content:center;
                background:rgba(4,6,10,.4); color:#e6edf3; font:600 14px system-ui, sans-serif;
                opacity:0; transition:opacity .25s; pointer-events:none; z-index:6; }
    .zoomhint.show { opacity:1; }
    .tip { position:absolute; z-index:5; pointer-events:none; transform:translate(12px, -8px);
           background:rgba(11,17,24,.95); border:1px solid #2c4257; border-radius:6px;
           padding:7px 10px; max-width:240px; box-shadow:0 6px 20px rgba(0,0,0,.45); }
    .tip .call { font:600 13px ui-monospace, monospace; color:#7fd1ff; }
    .tip .row { display:flex; align-items:center; gap:5px; font:11px ui-monospace, monospace; color:#cdd9e3; margin-top:3px; }
    .tip .row i { width:8px; height:8px; border-radius:50%; display:inline-block; }
    .tip .place { font:11px system-ui, sans-serif; color:#9fb0bd; margin-top:3px; }
  `],
})
export class MapsComponent implements OnInit, AfterViewInit, OnDestroy {
  @ViewChild('svg', { static: true }) svgEl!: ElementRef<SVGSVGElement>;
  @Input() source: 'heard' | 'heardBy' = 'heard';
  @Input() label = '';
  pskActive = false;

  countryPaths: string[] = [];
  borderPaths: string[] = [];
  countryLabels: MapLabel[] = [];
  stateLabels: MapLabel[] = [];
  shown: Spot[] = [];
  hovered: Hover | null = null;
  private hoveredKey = '';
  windowMs = 0;
  bandFilter = '';
  scale = 1; tx = 0; ty = 0;
  pinR = 1;
  hitR = 2;
  hintVisible = false;
  private hintTimer?: ReturnType<typeof setTimeout>;

  readonly filters: TimeFilter[] = [
    { label: '1m', ms: 60_000 }, { label: '5m', ms: 300_000 },
    { label: '10m', ms: 600_000 }, { label: '30m', ms: 1_800_000 },
    { label: 'All', ms: 0 },
  ];
  readonly legend = ['FT8', 'FT4', 'FT2', 'WSPR'];

  private subs: Subscription[] = [];
  private timer?: ReturnType<typeof setInterval>;
  private pointers = new Map<number, { x: number; y: number }>();
  private lastDist = 0;
  private lastMid = { x: 0, y: 0 };
  private wheelHandler = (e: WheelEvent) => this.onWheel(e);
  private cities: City[] = [];
  private placeCache = new Map<string, string>();
  private gridPos = new Map<string, [number, number]>();
  private regionNames = typeof Intl !== 'undefined' && (Intl as any).DisplayNames
    ? new Intl.DisplayNames(['en'], { type: 'region' }) : null;

  private rxSpots: Spot[] = [];
  private rxTimer?: ReturnType<typeof setInterval>;
  private tickTimer?: ReturnType<typeof setInterval>;
  private pollFn?: () => void;
  private lastPollMs = 0;
  queryAt = 0;
  uploadAt = 0;

  constructor(private store: DecodeStore, private http: HttpClient, private api: ApiService,
              private zone: NgZone, private cdr: ChangeDetectorRef) {}

  ngOnInit(): void {
    this.http.get<any>('world.geo.json').subscribe((geo) => (this.countryPaths = buildPaths(geo)));
    this.http.get<MapExtras>('map-extras.json').subscribe((ex) => {
      this.borderPaths = ex.borders;
      this.countryLabels = ex.countries;
      this.stateLabels = ex.states;
    });
    this.http.get<City[]>('cities.json').subscribe((c) => {
      this.cities = c;
      this.refresh();
    });
    this.refresh();
    if (this.source === 'heard') {
      this.subs.push(this.store.added$.subscribe(() => this.refresh()));
      this.subs.push(this.store.reset$.subscribe(() => this.refresh()));
    } else {
      const poll = () => this.api.pskReports().subscribe((r) => {
        this.lastPollMs = Date.now();
        this.pskActive = r.enabled;

        this.queryAt  = r.next_query  > 0 ? Date.now() + (r.next_query  - r.now) * 1000 : 0;
        this.uploadAt = r.next_upload > 0 ? Date.now() + (r.next_upload - r.now) * 1000 : 0;
        this.rxSpots = r.reports
          .map((x) => {
            const ll = gridToLatLon(x.locator || '');
            return {
              utc: '', dt: 0, message: '',
              sender: x.receiver, grid: (x.locator || '').toUpperCase() || undefined,
              mode: x.mode, snr: x.snr, freq: x.freq,
              band: bandFor(x.freq),
              t: x.t * 1000,
              lat: ll ? ll[0] : undefined, lon: ll ? ll[1] : undefined,
            } as Spot;
          })
          .filter((s) => s.lat != null);
        this.refresh();
      });
      this.pollFn = poll;
      poll();
      this.rxTimer = setInterval(poll, 60_000);

      this.tickTimer = setInterval(() => {

        const overdue = (at: number) => at > 0 && Date.now() - at > 2000;
        if (this.pskActive && (overdue(this.queryAt) || overdue(this.uploadAt)) &&
            Date.now() - this.lastPollMs > 10_000) {
          poll();
        }
      }, 1000);
    }
    this.timer = setInterval(() => this.refresh(), 5000);
  }

  nextAt(): number {
    const c = [this.queryAt, this.uploadAt].filter((x) => x > 0);
    return c.length ? Math.min(...c) : 0;
  }

  countdown(at: number): string {
    if (!at) return '—';
    const s = Math.round((at - Date.now()) / 1000);
    if (s <= 0) return 'due…';
    return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
  }

  @HostListener('document:visibilitychange')
  onVisibility(): void {
    if (document.visibilityState === 'visible' && this.source === 'heardBy' && this.pollFn) {
      this.pollFn();
    }
  }

  ngAfterViewInit(): void {

    this.zone.runOutsideAngular(() => {
      this.svgEl.nativeElement.addEventListener('wheel', this.wheelHandler, { passive: false });
    });
  }

  ngOnDestroy(): void {
    this.svgEl.nativeElement.removeEventListener('wheel', this.wheelHandler);
    this.subs.forEach((s) => s.unsubscribe());
    if (this.timer !== undefined) clearInterval(this.timer);
    if (this.rxTimer !== undefined) clearInterval(this.rxTimer);
    if (this.tickTimer !== undefined) clearInterval(this.tickTimer);
    if (this.hintTimer !== undefined) clearTimeout(this.hintTimer);
  }

  get transform(): string { return `translate(${this.tx},${this.ty}) scale(${this.scale})`; }
  color(mode: string): string { return MODE_COLORS[mode] ?? '#9aa7b0'; }
  setWindow(ms: number): void { this.windowMs = ms; this.refresh(); }
  setBand(b: string): void { this.bandFilter = b; this.refresh(); }

  bands(): string[] {
    const src = this.source === 'heard' ? this.store.list() : this.rxSpots;
    const present = new Set(src.map((s) => s.band).filter((b) => !!b));
    const order = ['160m', '80m', '60m', '40m', '30m', '20m', '17m', '15m', '12m', '10m', '6m', '2m', '70cm'];
    return order.filter((b) => present.has(b));
  }

  onPinEnter(s: Spot, ev: PointerEvent): void {
    if (ev.pointerType === 'mouse') this.showTip(s, ev);
  }

  onPinLeave(ev: PointerEvent): void {
    if (ev.pointerType === 'mouse') { this.hovered = null; this.hoveredKey = ''; }
  }

  onPinDown(s: Spot, ev: PointerEvent): void {
    if (ev.pointerType === 'mouse') return;
    ev.preventDefault();
    ev.stopPropagation();
    const key = s.sender + '|' + (s.grid ?? '');
    if (this.hovered && this.hoveredKey === key) {
      this.hovered = null;
      this.hoveredKey = '';
      return;
    }
    this.showTip(s, ev);
  }

  private showTip(s: Spot, ev: PointerEvent): void {
    const box = (ev.currentTarget as Element).closest('.mapbox') ?? this.svgEl.nativeElement.parentElement!;
    const r = (box as Element).getBoundingClientRect();
    const touch = ev.pointerType !== 'mouse';

    const x = Math.max(4, Math.min(ev.clientX - r.left - (touch ? 75 : 0), r.width - 160));
    const y = touch ? Math.max(4, ev.clientY - r.top - 92)
                    : Math.max(4, ev.clientY - r.top - 10);
    this.hoveredKey = s.sender + '|' + (s.grid ?? '');
    this.hovered = {
      call: s.sender.replace(/[<>]/g, ''),
      mode: s.mode,
      grid: s.grid ?? '',
      snr: s.snr,
      place: this.placeFor(s),
      x, y,
    };
  }

  private placeFor(s: Spot): string {
    if (s.lat == null || s.lon == null) return '';
    const key = s.grid ?? `${s.lat},${s.lon}`;
    const cached = this.placeCache.get(key);
    if (cached !== undefined) return cached;
    const place = this.nearestCity(s.lat, s.lon);
    this.placeCache.set(key, place);
    return place;
  }

  private nearestCity(lat: number, lon: number): string {
    if (!this.cities.length) return '';
    const coslat = Math.cos((lat * Math.PI) / 180);
    let best: City | null = null;
    let bestD = Infinity;
    for (const c of this.cities) {
      let dlon = Math.abs(c[2] - lon);
      if (dlon > 180) dlon = 360 - dlon;
      const d = (c[1] - lat) * (c[1] - lat) + dlon * coslat * dlon * coslat;
      if (d < bestD) { bestD = d; best = c; }
    }
    if (!best || Math.sqrt(bestD) * 111 > 300) return '';
    if (best[3] === 'US' && best[4]) return `${best[0]}, ${best[4]}`;
    const country = this.regionNames ? (this.regionNames.of(best[3]) ?? best[3]) : best[3];
    return `${best[0]}, ${country}`;
  }

  private onWheel(e: WheelEvent): void {

    if (!e.ctrlKey && !e.metaKey) {
      this.hintVisible = true;
      if (this.hintTimer) clearTimeout(this.hintTimer);
      this.hintTimer = setTimeout(() => {
        this.hintVisible = false;
        this.cdr.detectChanges();
      }, 1100);
      this.cdr.detectChanges();
      return;
    }
    e.preventDefault();
    const vb = this.toVB(e.clientX, e.clientY);
    const f = Math.pow(1.0015, -e.deltaY);
    this.zoomAt(vb.x, vb.y, f);
    this.cdr.detectChanges();
  }

  onPointerDown(e: PointerEvent): void {
    e.preventDefault();
    this.hovered = null;
    this.hoveredKey = '';
    this.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (this.pointers.size === 2) { this.lastDist = this.pinchDist(); this.lastMid = this.pinchMid(); }
  }

  @HostListener('document:pointermove', ['$event'])
  onPointerMove(e: PointerEvent): void {
    if (!this.pointers.has(e.pointerId)) return;
    const prev = this.pointers.get(e.pointerId)!;
    this.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (this.pointers.size === 1) {
      this.panPx(e.clientX - prev.x, e.clientY - prev.y);
    } else if (this.pointers.size === 2) {
      const dist = this.pinchDist(), mid = this.pinchMid();
      if (this.lastDist > 0) {
        const vb = this.toVB(mid.x, mid.y);
        this.zoomAt(vb.x, vb.y, dist / this.lastDist);
        this.panPx(mid.x - this.lastMid.x, mid.y - this.lastMid.y);
      }
      this.lastDist = dist; this.lastMid = mid;
    }
  }

  @HostListener('document:pointerup', ['$event'])
  @HostListener('document:pointercancel', ['$event'])
  onPointerUp(e: PointerEvent): void {
    this.pointers.delete(e.pointerId);
    if (this.pointers.size < 2) this.lastDist = 0;
  }

  @HostListener('window:resize')
  onResize(): void { this.updatePinSizes(); }

  private updatePinSizes(): void {
    const w = this.svgEl.nativeElement.clientWidth;
    if (!w || w < 50) return;
    const coarse = typeof window.matchMedia === 'function' && window.matchMedia('(pointer: coarse)').matches;
    this.pinR = (coarse ? 5.5 : 3.5) * 360 / w;
    this.hitR = (coarse ? 15 : 8) * 360 / w;
  }

  zoomBtn(f: number): void { this.zoomAt(180, 90, f); }
  reset(): void { this.scale = 1; this.tx = 0; this.ty = 0; }

  private toVB(cx: number, cy: number): { x: number; y: number } {
    const svg = this.svgEl.nativeElement;
    const ctm = svg.getScreenCTM();
    if (!ctm) return { x: 180, y: 90 };
    const p = new DOMPoint(cx, cy).matrixTransform(ctm.inverse());
    return { x: p.x, y: p.y };
  }

  private panPx(dxPx: number, dyPx: number): void {
    const ctm = this.svgEl.nativeElement.getScreenCTM();
    if (!ctm) return;
    if (dxPx || dyPx) this.hovered = null;
    this.tx += dxPx / ctm.a;
    this.ty += dyPx / ctm.d;
    this.clamp();
  }

  private zoomAt(vx: number, vy: number, f: number): void {
    this.hovered = null;
    const ns = Math.max(1, Math.min(60, this.scale * f));
    const k = ns / this.scale;
    this.tx = vx - (vx - this.tx) * k;
    this.ty = vy - (vy - this.ty) * k;
    this.scale = ns;
    this.clamp();
  }

  private clamp(): void {
    this.tx = Math.min(0, Math.max(360 - 360 * this.scale, this.tx));
    this.ty = Math.min(0, Math.max(180 - 180 * this.scale, this.ty));
  }

  private pinchDist(): number {
    const p = [...this.pointers.values()];
    return Math.hypot(p[0].x - p[1].x, p[0].y - p[1].y);
  }
  private pinchMid(): { x: number; y: number } {
    const p = [...this.pointers.values()];
    return { x: (p[0].x + p[1].x) / 2, y: (p[0].y + p[1].y) / 2 };
  }

  trackPin(_: number, s: Spot): string { return (s.sender || '') + '|' + (s.grid || ''); }

  private refresh(): void {
    this.updatePinSizes();
    let src = this.source === 'heard'
      ? this.store.since(this.windowMs)
      : this.rxSpots.filter((s) => this.windowMs <= 0 || s.t >= Date.now() - this.windowMs);
    if (this.bandFilter) src = src.filter((s) => s.band === this.bandFilter);
    const byStation = new Map<string, Spot>();
    for (const s of src) {
      if (s.lat == null || s.lon == null) continue;
      const key = s.sender || s.grid!;
      const prev = byStation.get(key);
      if (!prev || s.t > prev.t) byStation.set(key, s);
    }
    this.shown = [...byStation.values()].map((s) => {
      if (!s.grid || s.grid.length !== 4) return s;
      const p = this.snapToLand(s.grid);
      return p ? { ...s, lat: p[0], lon: p[1] } : s;
    });
  }

  private snapToLand(grid: string): [number, number] | null {
    if (!this.cities.length) return null;
    const cached = this.gridPos.get(grid);
    if (cached) return cached;
    const c = gridToLatLon(grid);
    if (!c) return null;
    const [clat, clon] = c;
    const lat0 = clat - 0.5, lon0 = clon - 1;
    let best: [number, number] | null = null;
    let bestD = Infinity;
    for (const city of this.cities) {
      if (city[1] < lat0 || city[1] > lat0 + 1 || city[2] < lon0 || city[2] > lon0 + 2) continue;
      const d = (city[1] - clat) ** 2 + (city[2] - clon) ** 2;
      if (d < bestD) { bestD = d; best = [city[1], city[2]]; }
    }
    const pos: [number, number] = best ?? [clat, clon];
    this.gridPos.set(grid, pos);
    return pos;
  }
}

function buildPaths(geo: any): string[] {
  const paths: string[] = [];
  for (const f of geo?.features ?? []) {
    const g = f.geometry;
    if (!g) continue;
    const polys = g.type === 'Polygon' ? [g.coordinates]
                : g.type === 'MultiPolygon' ? g.coordinates : [];
    for (const poly of polys) {
      for (const ring of poly) {
        let d = '';
        for (let i = 0; i < ring.length; i++) {
          const lon = ring[i][0], lat = ring[i][1];
          d += (i ? 'L' : 'M') + (lon + 180).toFixed(2) + ',' + (90 - lat).toFixed(2) + ' ';
        }
        if (d) paths.push(d + 'Z');
      }
    }
  }
  return paths;
}

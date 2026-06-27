// SPDX-License-Identifier: GPL-3.0-or-later

import { Component, OnInit, OnDestroy, ViewChild, ElementRef, NgZone, ChangeDetectorRef, HostListener } from '@angular/core';
import { CommonModule } from '@angular/common';
import { HttpClient } from '@angular/common/http';
import { Subscription } from 'rxjs';
import { ApiService } from './api.service';
import { WsService } from './ws.service';
import { ConfigService } from './config.service';
import { formatDistance } from './maidenhead';
import { PotaPark, PotaParkDetail, Status } from './models';

@Component({
  selector: 'app-pota',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="pota">
      <div class="bar">
        <span class="title">Parks On The Air</span>
        <span class="loc" *ngIf="haveCenter">📍 {{ center.lat | number:'1.3-3' }}, {{ center.lon | number:'1.3-3' }}<span class="src" *ngIf="posSrc && posSrc !== 'gps'">{{ posSrc }}</span></span>
        <span class="loc nofix" *ngIf="!haveCenter">no position</span>
        <button class="refresh wide" (click)="refreshParks()" [disabled]="refreshingParks"
                title="re-download the offline park database">{{ refreshingParks ? '…' : '⬇ parks' }}</button>
      </div>

      <div class="flash" *ngIf="flash" [class.err]="flashErr">{{ flash }}</div>

      <div class="active" *ngFor="let a of activeParks">
        <div class="acttext"><b>Activating {{ a.ref }}</b> — {{ a.name }}</div>
        <button class="spotbtn" (click)="spot(a.ref)" [disabled]="spotting || !rigReady()">
          {{ spotting ? 'spotting…' : 'Self-spot ' + bandMode() }}
        </button>
      </div>

      <div class="empty" *ngIf="!loading && !parks.length && haveCenter">No POTA parks in view.</div>
      <div class="empty" *ngIf="!haveCenter && !loading">No position — set your grid in Settings (or connect GPS).</div>

      <div class="grid" *ngIf="haveCenter">
        <button class="maptoggle" (click)="mapHidden = !mapHidden" [attr.aria-expanded]="!mapHidden">
          {{ mapHidden ? '▸ Show map' : '▾ Hide map' }}
        </button>
        <div class="mapwrap" [hidden]="mapHidden">
          <svg #svg class="map" viewBox="-105 -105 210 210" preserveAspectRatio="xMidYMid slice"
               (pointerdown)="onPointerDown($event)">
            <g [attr.transform]="transform">
              <path *ngFor="let d of geoCountry" [attr.d]="d" class="geoc" [attr.stroke-width]="0.9/scale"></path>
              <path *ngFor="let d of geoState" [attr.d]="d" class="geos" [attr.stroke-width]="0.5/scale"></path>
              <text *ngFor="let l of geoLabels" [attr.x]="l.x" [attr.y]="l.y" class="geolbl"
                    [class.big]="l.big" [attr.font-size]="(l.big ? 7 : 5)/scale">{{ l.n }}</text>
              <ng-container *ngIf="scale >= 0.5">
                <text *ngFor="let c of geoCities" [attr.x]="c.x + 2/scale" [attr.y]="c.y" class="geocity"
                      [attr.font-size]="4.5/scale">{{ c.n }}</text>
              </ng-container>
              <line x1="-105" y1="0" x2="105" y2="0" class="axis" [attr.stroke-width]="0.5/scale"></line>
              <line x1="0" y1="-105" x2="0" y2="105" class="axis" [attr.stroke-width]="0.5/scale"></line>
              <circle class="me" cx="0" cy="0" [attr.r]="3.5/scale" [attr.stroke-width]="1/scale"></circle>
              <g *ngFor="let p of parks; trackBy: trackPark" (click)="onParkTap(p, $event)" class="park"
                 [class.on]="isActive(p.reference)" [class.done]="isActivated(p.reference)"
                 [class.hovered]="hoverPark?.reference === p.reference"
                 [attr.transform]="'translate(' + px(p) + ',' + py(p) + ')'">
                <circle class="hit" [attr.r]="9/scale"></circle>
                <circle class="dot" [attr.r]="dotR()" [attr.stroke-width]="1/scale"></circle>
                <text *ngIf="scale >= 0.55" [attr.x]="5/scale" [attr.y]="2.5/scale" [attr.font-size]="6/scale">{{ p.reference }}</text>
              </g>
            </g>
          </svg>
          <div class="zoomctl">
            <button (click)="zoomBtn(1.4)" title="zoom in">+</button>
            <button (click)="zoomBtn(0.714)" title="zoom out">−</button>
            <button (click)="resetView()" title="reset view">⌂</button>
          </div>
        </div>

        <div class="list" [class.full]="mapHidden">
          <div class="park-row" *ngFor="let p of parks; trackBy: trackPark" [class.on]="isActive(p.reference)" [class.done]="isActivated(p.reference)">
            <div class="pref">{{ p.reference }}</div>
            <div class="pname">{{ p.name }}</div>
            <div class="pdist">{{ fmtDist(p.dist_km) }} {{ bearing(p) }}</div>
            <button class="ptoggle" [class.on]="isActive(p.reference)" (click)="toggle(p); $event.stopPropagation()">
              {{ isActive(p.reference) ? '✓ Active' : 'Activate' }}
            </button>
          </div>
        </div>
      </div>

      <div class="tip" *ngIf="hoverPark" [style.left.px]="tipLeft" [style.top.px]="tipTop">
        <div class="tipname">{{ hoverPark.name }}</div>
        <div class="tipref">{{ hoverPark.reference }} · {{ fmtDist(hoverPark.dist_km) }} {{ bearing(hoverPark) }}</div>
        <ng-container *ngIf="detail(hoverPark.reference) as d">
          <div class="tipline" *ngIf="d.parktype || d.location">{{ d.parktype }}<span *ngIf="d.location"> · {{ d.location }}</span></div>
          <div class="tipline" *ngIf="d.activations">{{ d.activations | number }} activation{{ d.activations === 1 ? '' : 's' }}<span *ngIf="d.qsos"> · {{ d.qsos | number }} QSOs</span></div>
          <div class="tipline">grid {{ d.grid }} · {{ d.lat | number:'1.3-3' }}, {{ d.lon | number:'1.3-3' }}</div>
        </ng-container>
      </div>
    </div>`,
  styles: [`
    .pota { padding:4px; }
    .bar { display:flex; align-items:center; gap:12px; margin-bottom:10px; }
    .title { font:600 14px system-ui, sans-serif; color:#e6edf3; }
    .loc { font:12px ui-monospace, monospace; color:#7fd1ff; }
    .loc.nofix { color:#6b7a86; }
    .refresh { margin-left:auto; background:#13202c; color:#8aa3b3; border:1px solid #1c2a38;
               border-radius:6px; width:30px; height:30px; cursor:pointer; }
    .refresh.wide { width:auto; padding:0 10px; font:600 11px system-ui, sans-serif; white-space:nowrap; }
    .flash { padding:8px 12px; border-radius:6px; margin-bottom:10px; background:#11243a;
             border:1px solid #1f6feb; color:#cfe3ff; font:12px system-ui, sans-serif; }
    .flash.err { background:#3a0d12; border-color:#f85149; color:#ffb3ae; }
    .active { display:flex; align-items:center; gap:12px; padding:10px 12px; margin-bottom:12px;
              background:#0c1a12; border:1px solid #2a5a36; border-radius:8px; flex-wrap:wrap; }
    .acttext { font:13px system-ui, sans-serif; color:#cdeccd; }
    .acttext b { color:#7fe3a0; }
    .spotbtn { margin-left:auto; background:#1f6feb; color:#fff; border:0; border-radius:6px;
               padding:8px 16px; font:700 12px system-ui, sans-serif; cursor:pointer; }
    .spotbtn:disabled { opacity:.5; cursor:default; }
    .empty { padding:20px; text-align:center; color:#6b7a86; font:13px system-ui, sans-serif; }
    .grid { display:block; }
    .mapwrap { position:relative; width:100%; }
    .map { width:100%; height:58vh; min-height:300px; background:#0b1118; border:1px solid #1c2530;
           border-radius:10px; display:block; touch-action:none; cursor:grab; }
    .map:active { cursor:grabbing; }
    .zoomctl { position:absolute; right:8px; bottom:8px; display:flex; flex-direction:column; gap:4px; }
    .zoomctl button { width:28px; height:28px; background:rgba(19,32,44,.85); color:#cdd9e3;
                      border:1px solid #2a3a4a; border-radius:6px; cursor:pointer;
                      font:700 14px ui-monospace, monospace; line-height:1; }
    .zoomctl button:hover { border-color:#2f80ed; }
    .axis { stroke:#162130; }
    .me { fill:#7fd1ff; stroke:#0b1118; }
    .park { cursor:pointer; }
    .park .dot { fill:#f0883e; stroke:#0b1118; }
    .park .hit { fill:transparent; }
    .park text { fill:#9fb0bd; font-family:ui-monospace, monospace; }
    .park.hovered .dot { fill:#ffd166; }
    .park.on .dot { fill:#3fb950; }
    .park.on text { fill:#7fe3a0; }
    .park.done .dot { fill:#2f80ed; }
    .park.done text { fill:#7fd1ff; }
    .park-row.done .pname { color:#7fd1ff; }
    .tip { position:fixed; z-index:60; pointer-events:none; max-width:280px;
           background:#0d141d; border:1px solid #2a3a4a; border-radius:8px; padding:8px 10px;
           box-shadow:0 6px 24px rgba(0,0,0,.5); }
    .tipname { font:600 13px system-ui, sans-serif; color:#e6edf3; }
    .tipref { font:12px ui-monospace, monospace; color:#f0b35e; margin-top:2px; }
    .tipline { font:11px system-ui, sans-serif; color:#9fb0bd; margin-top:3px; }
    .list { display:flex; flex-direction:column; gap:4px; max-height:40vh; overflow-y:auto; margin-top:12px; }
    .list.full { max-height:80vh; }
    .maptoggle { width:100%; margin-bottom:8px; background:#13202c; color:#9fb0bd;
                 border:1px solid #1c2a38; border-radius:6px; padding:7px 10px;
                 font:600 12px system-ui, sans-serif; text-align:left; cursor:pointer; }
    .maptoggle:hover { border-color:#2f80ed; color:#cdd9e3; }
    .src { margin-left:6px; padding:1px 5px; border-radius:4px; background:#13202c; color:#8aa3b3;
           font:600 10px ui-monospace, monospace; }
    .geoc { fill:none; stroke:#1d3a5c; }
    .geos { fill:none; stroke:#16293c; }
    .geolbl { fill:#3c536b; font-family:system-ui, sans-serif; }
    .geolbl.big { fill:#46607c; font-weight:600; letter-spacing:.5px; }
    .geocity { fill:#54707f; font-family:system-ui, sans-serif; }
    .park-row { display:grid; grid-template-columns: 78px 1fr auto auto; gap:10px; align-items:center;
                padding:8px 10px; border:1px solid #15202b; border-radius:6px; cursor:default; }
    .ptoggle { background:#13202c; color:#9fb0bd; border:1px solid #2a3a4a; border-radius:6px;
               padding:7px 13px; font:600 11px system-ui, sans-serif; cursor:pointer; white-space:nowrap; }
    .ptoggle:hover { border-color:#2f80ed; color:#cdd9e3; }
    .ptoggle.on { background:#0c2a16; color:#7fe3a0; border-color:#2a6a3a; }
    .park-row:hover { border-color:#2f80ed; }
    .park-row.on { background:#0c1a12; border-color:#2a5a36; }
    .pref { font:700 13px ui-monospace, monospace; color:#f0b35e; }
    .park-row.on .pref { color:#7fe3a0; }
    .pname { font:13px system-ui, sans-serif; color:#dfe7ee; min-width:0;
             overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
    .pdist { font:12px ui-monospace, monospace; color:#8aa3b3; white-space:nowrap; }
    @media (max-width: 820px) { .map { height:44vh; } }
  `],
})
export class PotaComponent implements OnInit, OnDestroy {
  parks: PotaPark[] = [];
  center = { lat: 0, lon: 0 };
  haveCenter = false;
  loading = false;
  mapHidden = false;
  activeParks: { ref: string; name: string }[] = [];
  spotting = false;
  flash = '';
  flashErr = false;
  hoverPark: PotaPark | null = null;
  hoverX = 0; hoverY = 0;
  private activeRefs = new Set<string>();
  private activatedRefs = new Set<string>();
  private apiActivatedRefs = new Set<string>();
  private tapTimer?: ReturnType<typeof setTimeout>;
  private detailCache = new Map<string, PotaParkDetail | null>();
  private projScale = 1.2;
  private fetchTimer?: ReturnType<typeof setTimeout>;

  scale = 1; tx = 0; ty = 0;
  private pointers = new Map<number, { x: number; y: number }>();
  private lastDist = 0;
  private lastMid = { x: 0, y: 0 };
  private moved = false;
  private svgNode?: SVGSVGElement;
  private wheelHandler = (e: WheelEvent) => this.onWheel(e);

  private hoverRaf = 0;
  private pendingHover?: MouseEvent;
  private moveHandler = (e: MouseEvent) => {
    this.pendingHover = e;
    if (this.hoverRaf) return;
    this.hoverRaf = requestAnimationFrame(() => {
      this.hoverRaf = 0;
      const ev = this.pendingHover;
      if (ev) this.zone.run(() => this.onMapHover(ev));
    });
  };
  private leaveHandler = () => { if (this.hoverPark) this.zone.run(() => this.onHoverOut()); };
  @ViewChild('svg') set svgRef(el: ElementRef<SVGSVGElement> | undefined) {
    const node = el?.nativeElement;
    if (node === this.svgNode) return;
    if (this.svgNode) {
      this.svgNode.removeEventListener('wheel', this.wheelHandler);
      this.svgNode.removeEventListener('mousemove', this.moveHandler);
      this.svgNode.removeEventListener('mouseleave', this.leaveHandler);
    }
    this.svgNode = node;
    if (node) this.zone.runOutsideAngular(() => {
      node.addEventListener('wheel', this.wheelHandler, { passive: false });
      node.addEventListener('mousemove', this.moveHandler);
      node.addEventListener('mouseleave', this.leaveHandler);
    });
  }
  private status: Status | null = null;
  private subs: Subscription[] = [];
  private flashTimer?: ReturnType<typeof setTimeout>;

  constructor(private api: ApiService, private ws: WsService, private config: ConfigService,
              private http: HttpClient, private zone: NgZone, private cdr: ChangeDetectorRef) {}

  posSrc = '';
  geoCountry: string[] = [];
  geoState: string[] = [];
  geoCities: { n: string; x: number; y: number }[] = [];
  geoLabels: { n: string; x: number; y: number; big: boolean }[] = [];
  private cities: [string, number, number, string, string][] = [];
  private worldLines: [number, number][][] = [];
  private stateLines: [number, number][][] = [];
  private placeLabels: { n: string; lat: number; lon: number; big: boolean }[] = [];
  private geoCenter = { lat: 999, lon: 999 };

  private units = 'km';

  fmtDist(km: number): string { return formatDistance(km, this.units); }

  get transform(): string { return `translate(${this.tx},${this.ty}) scale(${this.scale})`; }

  ngOnInit(): void {
    this.subs.push(this.config.config$.subscribe((c) => { this.units = c.units; }));
    this.subs.push(this.ws.logsChanged$.subscribe(() => this.refreshActive()));
    this.api.potaActivated().subscribe({
      next: (r) => { if (r.ok) this.apiActivatedRefs = new Set((r.refs || []).map((x) => x.toUpperCase())); },
      error: () => {},
    });
    this.subs.push(this.ws.status$.subscribe((s) => {
      this.status = s;

      if (s?.gps?.fix && !this.haveCenter && !this.loading && !this.parks.length) this.load();
    }));
    this.load();
    this.refreshActive();
    this.loadGeo();
  }

  private loadGeo(): void {
    this.http.get<any>('/world.geo.json').subscribe({
      next: (w) => {
        for (const f of w.features || []) {
          const g = f.geometry || {};
          const polys = g.type === 'Polygon' ? [g.coordinates]
                      : g.type === 'MultiPolygon' ? g.coordinates : [];
          for (const poly of polys) for (const ring of poly)
            this.worldLines.push(ring.map((pt: number[]) => [pt[1], pt[0]] as [number, number]));
        }
        this.projectGeo(true);
      }, error: () => {},
    });
    this.http.get<any>('/map-extras.json').subscribe({
      next: (m) => {
        for (const d of m.borders || []) {
          for (const seg of (d as string).split('M')) {
            const nums = (seg.match(/-?\d+(\.\d+)?/g) || []).map(Number);
            const line: [number, number][] = [];
            for (let i = 0; i + 1 < nums.length; i += 2) line.push([90 - nums[i + 1], nums[i] - 180]);
            if (line.length > 1) this.stateLines.push(line);
          }
        }
        for (const s of m.states || []) this.placeLabels.push({ n: s.n, lat: 90 - s.y, lon: s.x - 180, big: false });
        for (const c of m.countries || []) this.placeLabels.push({ n: c.n, lat: 90 - c.y, lon: c.x - 180, big: true });
        this.projectGeo(true);
      }, error: () => {},
    });
    this.http.get<any[]>('/cities.json').subscribe({
      next: (c) => { this.cities = c as any; this.projectGeo(true); }, error: () => {},
    });
  }

  private lx(lon: number): number {
    return (lon - this.center.lon) * Math.cos(this.center.lat * Math.PI / 180) * 111.32 * this.projScale;
  }
  private ly(lat: number): number { return -(lat - this.center.lat) * 111.32 * this.projScale; }

  private projectGeo(force = false): void {
    if (!this.haveCenter) return;
    if (!force && Math.abs(this.center.lat - this.geoCenter.lat) < 0.01 &&
        Math.abs(this.center.lon - this.geoCenter.lon) < 0.01) return;
    this.geoCenter = { ...this.center };
    const span = 8;
    const inBox = (la: number, lo: number) =>
      Math.abs(la - this.center.lat) <= span && Math.abs(lo - this.center.lon) <= span;
    const toPaths = (lines: [number, number][][]): string[] => {
      const out: string[] = [];
      for (const line of lines) {
        let d = '';
        let pen = false;
        for (const [la, lo] of line) {
          if (inBox(la, lo)) {
            d += (pen ? 'L' : 'M') + this.lx(lo).toFixed(1) + ',' + this.ly(la).toFixed(1);
            pen = true;
          } else pen = false;
        }
        if (d) out.push(d);
      }
      return out;
    };
    this.geoCountry = toPaths(this.worldLines);
    this.geoState = toPaths(this.stateLines);
    const near = this.cities
      .filter((c) => inBox(c[1], c[2]))
      .map((c) => ({ n: c[0], x: this.lx(c[2]), y: this.ly(c[1]),
                     d2: (c[1] - this.center.lat) ** 2 + (c[2] - this.center.lon) ** 2 }))
      .sort((a, b) => a.d2 - b.d2);

    const kept: { n: string; x: number; y: number }[] = [];
    for (const c of near) {
      if (kept.length >= 40) break;
      if (kept.some((k) => Math.abs(k.x - c.x) < 34 && Math.abs(k.y - c.y) < 8)) continue;
      kept.push({ n: c.n, x: c.x, y: c.y });
    }
    this.geoCities = kept;
    this.geoLabels = this.placeLabels
      .filter((p) => inBox(p.lat, p.lon))
      .map((p) => ({ n: p.n, x: this.lx(p.lon), y: this.ly(p.lat), big: p.big }));
  }

  refreshingParks = false;
  refreshParks(): void {
    this.refreshingParks = true;
    this.api.potaRefreshParks().subscribe({
      next: (r) => {
        this.refreshingParks = false;
        this.setFlash(r.ok ? `Park database updated — ${(r.parks || 0).toLocaleString()} parks`
                           : `Refresh failed: ${r.error || 'unknown'}`, !r.ok);
        if (r.ok) this.load(this.visibleBox());
      },
      error: () => { this.refreshingParks = false; this.setFlash('Refresh failed (network)', true); },
    });
  }

  ngOnDestroy(): void {
    this.subs.forEach((s) => s.unsubscribe());
    if (this.flashTimer) clearTimeout(this.flashTimer);
    if (this.fetchTimer) clearTimeout(this.fetchTimer);
    if (this.tapTimer) clearTimeout(this.tapTimer);
    if (this.hoverRaf) cancelAnimationFrame(this.hoverRaf);
    if (this.svgNode) {
      this.svgNode.removeEventListener('wheel', this.wheelHandler);
      this.svgNode.removeEventListener('mousemove', this.moveHandler);
      this.svgNode.removeEventListener('mouseleave', this.leaveHandler);
    }
  }

  private browserPos: { lat: number; lon: number } | null = null;

  load(box?: { s: number; w: number; n: number; e: number }): void {
    const initial = !box;
    this.loading = true;
    this.api.potaParks(box, this.browserPos || undefined).subscribe({
      next: (r) => {
        this.loading = false;
        if (!r.ok) { this.haveCenter = false; this.posSrc = ''; return; }
        this.center = { lat: r.lat, lon: r.lon };
        this.posSrc = r.src || 'gps';
        this.haveCenter = true;
        this.parks = r.parks || [];
        if (initial) { this.scale = 1; this.tx = 0; this.ty = 0; }
        this.projectGeo();
      },
      error: () => { this.loading = false; this.setFlash('Could not reach POTA', true); },
    });
  }

  private visibleBox(): { s: number; w: number; n: number; e: number } {
    const ps = this.projScale, c = this.center;
    const xmin = (-105 - this.tx) / this.scale, xmax = (105 - this.tx) / this.scale;
    const ymin = (-105 - this.ty) / this.scale, ymax = (105 - this.ty) / this.scale;
    const cos = Math.cos(c.lat * Math.PI / 180) || 1;
    let n = c.lat + (-ymin / ps) / 111.32, s = c.lat + (-ymax / ps) / 111.32;
    let e = c.lon + (xmax / ps) / (111.32 * cos), w = c.lon + (xmin / ps) / (111.32 * cos);

    const span = 4, bcLat = (n + s) / 2, bcLon = (e + w) / 2;
    n = Math.min(n, bcLat + span); s = Math.max(s, bcLat - span);
    e = Math.min(e, bcLon + span); w = Math.max(w, bcLon - span);

    const minSpan = 0.35;
    if ((n - s) / 2 < minSpan) { n = bcLat + minSpan; s = bcLat - minSpan; }
    const minLonSpan = minSpan / (Math.cos(bcLat * Math.PI / 180) || 1);
    if ((e - w) / 2 < minLonSpan) { e = bcLon + minLonSpan; w = bcLon - minLonSpan; }
    return { s, w, n, e };
  }

  private scheduleFetch(): void {
    if (this.fetchTimer) clearTimeout(this.fetchTimer);
    this.fetchTimer = setTimeout(() => { if (this.haveCenter) this.load(this.visibleBox()); }, 400);
  }

  px(p: PotaPark): number { return this.lx(p.lon); }
  py(p: PotaPark): number { return this.ly(p.lat); }

  dotR(): number {
    const screen = this.scale >= 0.8 ? 4 : this.scale >= 0.4 ? 3 : 2.2;
    return screen / this.scale;
  }

  onParkTap(p: PotaPark, ev: MouseEvent): void {

    const fromMap = !!(ev.target as Element | null)?.closest?.('svg');
    if (fromMap && this.moved) return;
    this.onHover(p, ev);
    if (this.tapTimer) clearTimeout(this.tapTimer);
    this.tapTimer = setTimeout(() => this.onHoverOut(), 4000);
  }

  isActive(ref: string): boolean { return this.activeRefs.has(ref.toUpperCase()); }
  isActivated(ref: string): boolean {
    const u = ref.toUpperCase();
    return this.activatedRefs.has(u) || this.apiActivatedRefs.has(u);
  }
  trackPark(_: number, p: PotaPark): string { return p.reference; }

  private refreshActive(): void {
    this.api.logs().subscribe((r) => {
      this.activeParks = r.logs.filter((l) => l.active && l.park).map((l) => ({ ref: l.park.toUpperCase(), name: l.name }));
      this.activeRefs = new Set(this.activeParks.map((a) => a.ref));
      this.activatedRefs = new Set(r.logs.filter((l) => l.park && l.qsos > 0).map((l) => l.park.toUpperCase()));
    });
  }

  onPointerDown(e: PointerEvent): void {
    this.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (this.pointers.size === 1) this.moved = false;
    if (this.pointers.size === 2) { this.lastDist = this.pinchDist(); this.lastMid = this.pinchMid(); }
  }

  @HostListener('document:pointermove', ['$event'])
  onPointerMove(e: PointerEvent): void {
    if (!this.pointers.has(e.pointerId)) return;
    const prev = this.pointers.get(e.pointerId)!;
    this.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (this.pointers.size === 1) {
      const dx = e.clientX - prev.x, dy = e.clientY - prev.y;
      if (Math.abs(dx) + Math.abs(dy) > 2) this.moved = true;
      this.panPx(dx, dy);
    } else if (this.pointers.size === 2) {
      const dist = this.pinchDist(), mid = this.pinchMid();
      if (this.lastDist > 0) {
        const f = dist / this.lastDist;
        if (f > 0.33 && f < 3) {              // a wilder per-event ratio is a ghost finger, not a pinch
          const vb = this.toVB(mid.x, mid.y);
          this.zoomAt(vb.x, vb.y, f);
          this.panPx(mid.x - this.lastMid.x, mid.y - this.lastMid.y);
        }
      }
      this.lastDist = dist; this.lastMid = mid; this.moved = true;
    }

    if (this.pointers.size === 0) this.scheduleFetch();
  }

  @HostListener('document:pointerup', ['$event'])
  @HostListener('document:pointercancel', ['$event'])
  onPointerUp(e: PointerEvent): void {
    this.pointers.delete(e.pointerId);
    if (this.pointers.size < 2) this.lastDist = 0;
    if (this.pointers.size === 0) this.scheduleFetch();
  }

  private onWheel(e: WheelEvent): void {
    e.preventDefault();
    const vb = this.toVB(e.clientX, e.clientY);
    this.zoomAt(vb.x, vb.y, Math.pow(1.0015, -e.deltaY));
    this.cdr.detectChanges();
    this.scheduleFetch();
  }

  zoomBtn(f: number): void { this.zoomAt(0, 0, f); this.scheduleFetch(); }

  resetView(): void {
    this.scale = 1; this.tx = 0; this.ty = 0;
    if (this.posSrc === 'gps' || this.browserPos) { this.load(); return; }
    if (!navigator.geolocation || !window.isSecureContext) {
      this.load();
      return;
    }
    navigator.geolocation.getCurrentPosition(
      (pos) => this.zone.run(() => {
        this.browserPos = { lat: pos.coords.latitude, lon: pos.coords.longitude };
        this.load();
        this.cdr.detectChanges();
      }),
      () => this.zone.run(() => {
        this.load();
        this.cdr.detectChanges();
      }),
      { timeout: 8000 });
  }

  private toVB(cx: number, cy: number): { x: number; y: number } {
    const ctm = this.svgNode?.getScreenCTM();
    if (!ctm) return { x: 0, y: 0 };
    const p = new DOMPoint(cx, cy).matrixTransform(ctm.inverse());
    return { x: p.x, y: p.y };
  }
  private panPx(dxPx: number, dyPx: number): void {
    const ctm = this.svgNode?.getScreenCTM();
    if (!ctm) return;
    this.tx += dxPx / ctm.a; this.ty += dyPx / ctm.d;
    this.clampView();
  }
  private zoomAt(vx: number, vy: number, f: number): void {
    const ns = Math.max(0.06, Math.min(25, this.scale * f));
    const k = ns / this.scale;
    this.tx = vx - (vx - this.tx) * k;
    this.ty = vy - (vy - this.ty) * k;
    this.scale = ns;
    this.clampView();
  }

  private clampView(): void {

    const lim = 3500 * Math.max(this.scale, 1);
    this.tx = Math.max(-lim, Math.min(lim, this.tx));
    this.ty = Math.max(-lim, Math.min(lim, this.ty));
  }
  private pinchDist(): number {
    const p = [...this.pointers.values()];
    return Math.hypot(p[0].x - p[1].x, p[0].y - p[1].y);
  }
  private pinchMid(): { x: number; y: number } {
    const p = [...this.pointers.values()];
    return { x: (p[0].x + p[1].x) / 2, y: (p[0].y + p[1].y) / 2 };
  }

  bearing(p: PotaPark): string {
    const e = (p.lon - this.center.lon) * Math.cos(this.center.lat * Math.PI / 180);
    const n = p.lat - this.center.lat;
    const dirs = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
    const a = (Math.atan2(e, n) * 180 / Math.PI + 360) % 360;
    return dirs[Math.round(a / 45) % 8];
  }

  onMapHover(ev: MouseEvent): void {
    if (this.pointers.size) return;
    const vb = this.toVB(ev.clientX, ev.clientY);
    const lx = (vb.x - this.tx) / this.scale;
    const ly = (vb.y - this.ty) / this.scale;
    const r = 10 / this.scale;

    const kx = Math.cos(this.center.lat * Math.PI / 180) * 111.32 * this.projScale;
    const ky = 111.32 * this.projScale;
    let best: PotaPark | null = null, bestD2 = r * r;
    for (const p of this.parks) {
      const dx = (p.lon - this.center.lon) * kx - lx;
      const dy = -(p.lat - this.center.lat) * ky - ly;
      const d2 = dx * dx + dy * dy;
      if (d2 <= bestD2) { bestD2 = d2; best = p; }
    }
    if (best) {
      if (best.reference !== this.hoverPark?.reference) this.onHover(best, ev);
      this.hoverX = ev.clientX; this.hoverY = ev.clientY;
    } else if (this.hoverPark) {
      this.onHoverOut();
    }
  }

  onHover(p: PotaPark, ev: MouseEvent): void {
    this.hoverPark = p; this.hoverX = ev.clientX; this.hoverY = ev.clientY;
    if (!this.detailCache.has(p.reference)) {
      this.detailCache.set(p.reference, null);
      this.api.potaPark(p.reference).subscribe((d) => { if (d?.ok) this.detailCache.set(p.reference, d); });
    }
  }
  onHoverOut(): void { this.hoverPark = null; }
  get tipLeft(): number { return Math.max(8, Math.min(this.hoverX + 14, window.innerWidth - 288)); }
  get tipTop(): number { return Math.max(8, Math.min(this.hoverY + 14, window.innerHeight - 148)); }
  detail(ref: string): PotaParkDetail | undefined { return this.detailCache.get(ref) || undefined; }

  rigReady(): boolean { return !!this.status?.rig?.freq_hz; }
  bandMode(): string {
    const m = this.status?.mode || '';
    const f = this.status?.rig?.freq_hz ? (this.status.rig.freq_hz / 1000).toFixed(0) + ' kHz' : '';
    return `${m} ${f}`.trim() || 'current band';
  }

  toggle(p: PotaPark): void {
    this.onHoverOut();
    this.api.logs().subscribe((r) => {
      const ref = p.reference.toUpperCase();
      const existing = r.logs.find((l) => (l.park || '').toUpperCase() === ref);
      if (existing && existing.active) {
        this.api.setLogMeta(existing.file, { active: false }).subscribe(() => this.refreshActive());
        this.setFlash(`Deactivated ${p.reference}`, false);
      } else if (existing) {
        this.api.setLogMeta(existing.file, { active: true }).subscribe(() => this.refreshActive());
        this.setFlash(`Activated ${p.reference}`, false);
      } else {
        this.api.createLog(p.name, p.reference, true).subscribe(() => this.refreshActive());
        this.setFlash(`Created + activated log for ${p.reference}`, false);
      }
    });
  }

  spot(ref: string): void {
    this.spotting = true;
    this.api.potaSpot(ref).subscribe({
      next: (r) => {
        this.spotting = false;
        this.setFlash(r.ok ? `Spotted ${ref} — ${r.mode} ${r.frequency} kHz ✓`
                           : `Spot failed: ${r.error || 'unknown'}`, !r.ok);
      },
      error: () => { this.spotting = false; this.setFlash('Spot failed (network)', true); },
    });
  }

  private setFlash(msg: string, err: boolean): void {
    this.flash = msg; this.flashErr = err;
    if (this.flashTimer) clearTimeout(this.flashTimer);
    this.flashTimer = setTimeout(() => (this.flash = ''), 6000);
  }
}

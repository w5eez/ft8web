// SPDX-License-Identifier: GPL-3.0-or-later

import { Component, OnInit, OnDestroy } from '@angular/core';
import { CommonModule } from '@angular/common';
import { Subscription } from 'rxjs';
import { WsService } from './ws.service';
import { ApiService } from './api.service';
import { Status, ModeInfo, FreqEntry } from './models';

@Component({
  selector: 'app-rig-control',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="panel">
      <div class="swrtrip" *ngIf="status?.tx_inhibit">
        ⚠ TX disabled — SWR {{ status?.swr_trip | number:'1.1-1' }} tripped protection
        <button (click)="clearInhibit()">Clear</button>
      </div>
      <div class="freqrow">
        <div class="freq">{{ (status?.rig?.freq_hz || 0) / 1e6 | number:'1.6-6' }}<span> MHz</span></div>
        <button class="pwrbtn" (click)="showPower = !showPower" [class.open]="showPower"
                title="TX power — press to adjust">{{ displayPower() | number:'1.1-1' }} W</button>
      </div>
      <div class="pwrpanel" *ngIf="showPower">
        <input type="range" min="0.5" [max]="maxPower()" [step]="maxPower() > 10 ? 1 : 0.5"
               [value]="editPower"
               (input)="onPowerInput(+$any($event.target).value)"
               (change)="applyPower(+$any($event.target).value)" />
        <div class="pwrrow">
          <span class="pwrval">{{ editPower | number:'1.1-1' }} W</span>
          <button *ngFor="let p of presets()" (click)="applyPower(p)"
                  [class.active]="powerW() === p">{{ p }}</button>
        </div>
      </div>
      <div class="row">
        <span class="badge">{{ status?.rig?.mode || '—' }}</span>
        <span class="ptt" [class.tx]="status?.rig?.ptt">{{ status?.rig?.ptt ? 'TX' : 'RX' }}</span>
        <span class="smeter" title="S-meter"><i [style.width.%]="smeterPct()"></i></span>
      </div>
      <div class="gpsline" *ngIf="status?.gps?.fix || status?.gps?.auto">
        <span class="gpsgrid" *ngIf="status?.gps?.fix" title="grid from GPS (gpsd)">🛰 {{ status?.gps?.grid }}</span>
        <span class="gpsgrid nofix" *ngIf="!status?.gps?.fix" title="no GPS fix yet">🛰 GPS — no fix</span>
        <span class="gpsauto" *ngIf="status?.gps?.auto" title="station grid is following GPS">AUTO</span>
      </div>
      <div class="txmeters" [class.idle]="!status?.rig?.ptt">
        <span class="swr" [class.warn]="status?.rig?.ptt && (status?.rig?.swr || 0) >= 2"
              [class.bad]="status?.rig?.ptt && (status?.rig?.swr || 0) >= 2.5">
          SWR {{ swrText() }}
        </span>
        <span class="alc" [class.warn]="status?.rig?.ptt && alcPct() >= 25"
              [class.bad]="status?.rig?.ptt && alcPct() >= 50"
              title="ALC action — for clean FT8 keep this near 0% (lower the TX level if it climbs)">
          ALC {{ alcText() }}
        </span>
      </div>
      <div class="label">Mode</div>
      <div class="btns">
        <button *ngFor="let m of modes" [class.active]="m.name === current" (click)="selectMode(m.name)">{{ m.name }}</button>
      </div>
      <div class="label">Band</div>
      <div class="btns">
        <button *ngFor="let b of bands" [class.active]="b.hz === status?.rig?.freq_hz" (click)="tune(b.band)">{{ b.band }}</button>
      </div>
    </div>`,
  styles: [`
    .swrtrip { display:flex; align-items:center; gap:10px; margin-bottom:10px; padding:8px 10px;
               background:#3a0d12; border:1px solid #f85149; border-radius:6px;
               color:#ffb3ae; font:600 12px system-ui, sans-serif; }
    .swrtrip button { margin-left:auto; background:#f85149; color:#2a0606; border:0; border-radius:4px;
                      padding:4px 12px; font:600 11px system-ui, sans-serif; cursor:pointer; }
    .freqrow { display:flex; align-items:center; justify-content:space-between; gap:10px; }
    .freq { font:600 28px/1 ui-monospace, monospace; color:#7fd1ff; }
    .freq span { font-size:13px; color:#6b7a86; }
    .pwrbtn { background:#13202c; color:#9fe3c0; border:1px solid #1c2a38; border-radius:5px;
              padding:7px 10px; font:600 12px ui-monospace, monospace; cursor:pointer; white-space:nowrap; }
    .pwrbtn:hover, .pwrbtn.open { border-color:#2f80ed; }
    .pwrpanel { margin-top:10px; padding:10px; background:#0d141d; border:1px solid #1c2a38; border-radius:6px; }
    .pwrpanel input[type=range] { width:100%; box-sizing:border-box; }
    .pwrrow { display:flex; align-items:center; gap:6px; margin-top:8px; flex-wrap:wrap; }
    .pwrval { font:600 13px ui-monospace, monospace; color:#9fe3c0; margin-right:auto; }
    .pwrrow button { background:#13202c; color:#cdd9e3; border:1px solid #1c2a38; border-radius:4px;
                     padding:4px 9px; cursor:pointer; font:12px ui-monospace, monospace; }
    .pwrrow button.active { background:#1f6feb; color:#fff; border-color:#1f6feb; }
    .row { display:flex; align-items:center; gap:8px; margin:10px 0; }
    .badge { background:#13202c; color:#9fe3c0; padding:2px 8px; border-radius:4px; font:600 12px monospace; }
    .ptt { padding:2px 8px; border-radius:4px; background:#13202c; color:#6b7a86; font:600 12px monospace; }
    .ptt.tx { background:#7a1020; color:#ffe; }
    .smeter { flex:1; height:8px; background:#0d141d; border-radius:4px; overflow:hidden; }
    .smeter i { display:block; height:100%; background:linear-gradient(90deg,#2a8,#fd6); }
    .txmeters { display:flex; align-items:center; gap:10px; margin:8px 0; padding:7px 10px;
                background:#1a0f12; border:1px solid #5a2a30; border-radius:6px; }
    .swr { font:700 13px ui-monospace, monospace; color:#3fb950; white-space:nowrap; }
    .swr.warn { color:#ffd166; }
    .swr.bad { color:#f85149; }
    .alc { font:700 13px ui-monospace, monospace; color:#3fb950; white-space:nowrap; }
    .alc.warn { color:#ffd166; }
    .alc.bad { color:#f85149; }

    .txmeters.idle { background:#0d141d; border-color:#1c2a38; }
    .txmeters.idle .swr, .txmeters.idle .alc { color:#6b7a86; }
    .gpsline { display:flex; align-items:center; gap:8px; margin-top:6px; }
    .gpsgrid { font:700 13px ui-monospace, monospace; color:#7fd1ff; }
    .gpsgrid.nofix { color:#6b7a86; font-weight:400; font-size:12px; }
    .gpsauto { font:600 10px system-ui, sans-serif; color:#0c1a12; background:#3fb950;
               padding:1px 6px; border-radius:4px; letter-spacing:.5px; }
    .label { color:#6b7a86; font-size:11px; text-transform:uppercase; margin:8px 0 4px; }
    .btns { display:flex; flex-wrap:wrap; gap:6px; }
    .btns button { background:#13202c; color:#cdd9e3; border:1px solid #1c2a38; border-radius:4px;
                   padding:6px 10px; cursor:pointer; font:13px monospace; }
    .btns button.active { background:#1f6feb; color:#fff; border-color:#1f6feb; }
    .btns button:hover { border-color:#2f80ed; }
  `],
})
export class RigControlComponent implements OnInit, OnDestroy {
  status: Status | null = null;
  modes: ModeInfo[] = [];
  bands: FreqEntry[] = [];
  current = 'FT8';
  showPower = false;
  editPower = 5;
  private powerTouched = false;
  private subs: Subscription[] = [];

  constructor(private ws: WsService, private api: ApiService) {}

  ngOnInit(): void {
    this.subs.push(this.ws.status$.subscribe((s) => {
      this.status = s;
      if (this.powerTouched && s?.rig?.power_w != null &&
          Math.abs(s.rig.power_w - this.editPower) < 0.05) {
        this.powerTouched = false;
      }
      if (!this.powerTouched && s?.rig?.power_w) this.editPower = s.rig.power_w;
      if (s?.mode && s.mode !== this.current) { this.current = s.mode; this.loadBands(s.mode); }
    }));
    this.subs.push(this.ws.modes$.subscribe((m) => (this.modes = m)));
    this.loadBands(this.current);
  }

  ngOnDestroy(): void { this.subs.forEach((s) => s.unsubscribe()); }

  selectMode(name: string): void {
    this.current = name;
    this.powerTouched = false;
    const band = this.currentBand();
    this.api.frequencies(name).subscribe((r) => {
      this.bands = this.dedupeBands(r.frequencies);
      if (band && this.bands.some((b) => b.band === band)) {
        this.api.tune(name, band).subscribe();
      } else {
        this.api.setMode(name).subscribe();
      }
    });
  }

  tune(band: string): void { this.powerTouched = false; this.api.tune(this.current, band).subscribe(); }

  clearInhibit(): void { this.api.clearTxInhibit().subscribe(); }

  powerW(): number { return this.status?.rig?.power_w ?? 0; }

  displayPower(): number { return this.powerTouched ? this.editPower : this.powerW(); }
  maxPower(): number { return this.status?.rig?.power_max_w ?? 10; }
  presets(): number[] {
    return this.maxPower() > 10 ? [5, 10, 25, 50, 100] : [0.5, 1, 2.5, 5, 10];
  }

  onPowerInput(watts: number): void {
    this.editPower = watts;
    this.powerTouched = true;
  }

  applyPower(watts: number): void {
    this.editPower = watts;
    this.powerTouched = true;
    this.api.setPower(watts).subscribe();

    setTimeout(() => (this.powerTouched = false), 4000);
  }

  private loadBands(mode: string): void {
    this.api.frequencies(mode).subscribe((r) => (this.bands = this.dedupeBands(r.frequencies)));
  }

  private dedupeBands(list: FreqEntry[]): FreqEntry[] {
    const seen = new Set<string>();
    return list.filter((e) => e.band !== '?' && !seen.has(e.band) && !!seen.add(e.band));
  }

  private currentBand(): string {
    const hz = this.status?.rig?.freq_hz;
    return hz ? bandFor(hz) : '';
  }

  alcPct(): number { return Math.max(0, Math.min(100, (this.status?.rig?.alc ?? 0) * 100)); }

  swrText(): string { return this.status?.rig?.ptt ? (this.status?.rig?.swr ?? 0).toFixed(1) : '—'; }
  alcText(): string { return this.status?.rig?.ptt ? Math.round(this.alcPct()) + '%' : '—'; }

  smeterPct(): number {
    const s = this.status?.rig?.strength_db ?? -73;
    return Math.max(0, Math.min(100, ((s + 73) / 73) * 100));
  }
}

const BANDS: ReadonlyArray<[number, number, string]> = [
  [135700, 137800, '2200m'], [472000, 479000, '630m'], [1800000, 2000000, '160m'],
  [3500000, 4000000, '80m'], [5250000, 5450000, '60m'], [7000000, 7300000, '40m'],
  [10100000, 10150000, '30m'], [14000000, 14350000, '20m'], [18068000, 18168000, '17m'],
  [21000000, 21450000, '15m'], [24890000, 24990000, '12m'], [28000000, 29700000, '10m'],
  [50000000, 54000000, '6m'], [70000000, 70500000, '4m'], [144000000, 148000000, '2m'],
  [222000000, 225000000, '1.25m'], [420000000, 450000000, '70cm'],
];

function bandFor(hz: number): string {
  for (const [lo, hi, name] of BANDS) if (hz >= lo && hz <= hi) return name;
  return '';
}

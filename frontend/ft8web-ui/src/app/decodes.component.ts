// SPDX-License-Identifier: GPL-3.0-or-later

import { Component, OnInit, OnDestroy } from '@angular/core';
import { CommonModule } from '@angular/common';
import { Subscription } from 'rxjs';
import { DecodeStore, Spot } from './decode-store';
import { gridDistanceKm, formatDistance } from './maidenhead';
import { QsoService } from './qso.service';
import { ConfigService } from './config.service';
import { ChimeService } from './chime.service';
import { ApiService } from './api.service';
import { WorkedSets } from './models';

@Component({
  selector: 'app-decodes',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="dtools">
      <button class="dtbtn" [class.on]="frozen" (click)="toggleFreeze()"
              [title]="frozen ? 'resume live decodes' : 'freeze the list so it stops scrolling while you read'">
        {{ frozen ? '▶ Live' : '❄ Freeze' }}
      </button>
      <button class="dtbtn" [class.on]="!chimeOn()" (click)="toggleChime()"
              [title]="chimeOn() ? 'mute the QSO-complete chime' : 'unmute the QSO-complete chime'">
        {{ chimeOn() ? '🔔 Chime' : '🔕 Muted' }}
      </button>
      <button class="dtbtn" (click)="clearDecodes()" title="clear the decode list">🗑 Clear</button>
      <span class="dtspacer"></span>
      <button class="dtbtn" [class.on]="hist?.syncing" (click)="syncHistory()" [title]="histTitle()">
        {{ hist?.syncing ? '⟳ syncing…' : '⟳ QRZ' }}
      </button>
    </div>
    <div class="decodes">
      <div class="empty" *ngIf="!decodes.length">listening…</div>
      <table *ngIf="decodes.length">
        <thead>
          <tr><th>UTC</th><th>dB</th><th class="dtc">DT</th><th>Hz</th><th>Band</th><th>Mode</th><th>Dist</th><th>Message</th></tr>
        </thead>
        <tbody>
          <tr *ngFor="let d of decodes; trackBy: trackBy"
              [class.cq]="d.message.startsWith('CQ')" [class.worked]="d.worked"
              (click)="pick(d)" title="click to set as DX (fills the message panel)">
            <td>{{ d.utc }}</td>
            <td [class.weak]="d.snr < -15">{{ d.snr }}</td>
            <td class="dtc">{{ d.dt | number:'1.1-1' }}</td>
            <td>{{ d.freq }}</td>
            <td class="band">{{ d.band }}</td>
            <td class="mode">{{ d.mode }}</td>
            <td class="distc">{{ dist(d) }}</td>
            <td class="msg"><span class="b4" *ngIf="d.worked" title="worked on this band + mode this activation">✓</span><span class="nw nc" *ngIf="d.newCall" title="new callsign — not in your QRZ log">NEW</span><span class="nw ns" *ngIf="d.newSlot" title="worked before — new band/mode slot">BAND</span><span class="nw ng" *ngIf="d.newGrid" title="new grid square (CQ)">GRID</span><span *ngFor="let tk of d.toks" [ngClass]="tk.c">{{ tk.t }} </span></td>
          </tr>
        </tbody>
      </table>
    </div>

    <div class="qsopane" *ngIf="qsoDecodes.length">
      <div class="panehead">My QSO<span *ngIf="qsoCall"> — {{ qsoCall }}</span></div>
      <div class="decodes half">
        <table>
          <tbody>
            <tr *ngFor="let d of qsoDecodes; trackBy: trackBy"
                [class.worked]="d.worked"
                (click)="pick(d)" title="click to resume this QSO at the right step">
              <td>{{ d.utc }}</td>
              <td [class.weak]="d.snr < -15">{{ d.snr }}</td>
              <td class="dtc">{{ d.dt | number:'1.1-1' }}</td>
              <td>{{ d.freq }}</td>
              <td class="band">{{ d.band }}</td>
              <td class="mode">{{ d.mode }}</td>
              <td class="msg"><span class="b4" *ngIf="d.worked" title="worked on this band + mode this activation">✓</span><span class="nw nc" *ngIf="d.newCall" title="new callsign — not in your QRZ log">NEW</span><span class="nw ns" *ngIf="d.newSlot" title="worked before — new band/mode slot">BAND</span><span class="nw ng" *ngIf="d.newGrid" title="new grid square (CQ)">GRID</span><span *ngFor="let tk of d.toks" [ngClass]="tk.c">{{ tk.t }} </span></td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>`,
  styles: [`
    .dtools { display:flex; align-items:center; gap:8px; margin-bottom:6px; }
    .dtbtn { background:#13202c; color:#9fb0bd; border:1px solid #1c2a38; border-radius:6px;
             padding:4px 10px; font:600 11px system-ui, sans-serif; cursor:pointer; white-space:nowrap; }
    .dtbtn:hover { border-color:#2f80ed; color:#e6edf3; }
    .dtbtn.on { background:#241c0a; color:#e3b341; border-color:#3a2f12; }
    .dtspacer { flex:1; }
    .dtnote { font:11px system-ui, sans-serif; color:#3fb950; }
    .b4 { color:#3fb950; font-weight:700; margin-right:5px; }
    tr.worked td:first-child { box-shadow: inset 3px 0 0 #2ea043; }
    .decodes { overflow-y:auto; max-height:34vh; border:1px solid #1c2530; border-radius:6px; }
    .decodes.half { max-height:20vh; }
    .qsopane { margin-top:10px; }
    .panehead { font:600 11px system-ui, sans-serif; color:#8aa3b3; text-transform:uppercase;
                letter-spacing:.6px; margin-bottom:4px; }
    .panehead span { color:#7fd1ff; text-transform:none; }
    .empty { padding:14px; text-align:center; color:#54626e; font:12px ui-monospace, monospace; }
    table { width:100%; border-collapse:collapse; font:13px/1.45 ui-monospace, Menlo, Consolas, monospace; }
    thead th { position:sticky; top:0; background:#0d141d; color:#8aa3b3; text-align:left;
               padding:5px 8px; font-weight:600; }
    td { padding:2px 8px; border-top:1px solid #121a23; white-space:nowrap; }
    .mode { color:#7fb0c8; }
    .distc { color:#8aa3b3; white-space:nowrap; }
    .band { color:#9fe3c0; }
    .msg { white-space:normal; color:#dfe7ee; }
    tr.cq .msg { color:#ffd166; font-weight:600; }

    .mecall { color:#ff6b6b; font-weight:700; }
    .dxcall { color:#3fb950; font-weight:700; }

    .nw { font:700 9px ui-monospace, monospace; padding:1px 4px; border-radius:3px;
          margin-right:4px; letter-spacing:.4px; }
    .nw.nc { background:#3a2e08; color:#ffd166; }
    .nw.ns { background:#0a2733; color:#5bd6e8; }
    .nw.ng { background:#0c2a14; color:#5fd47a; }
    td.weak { color:#6b7a86; }
    tbody tr { cursor:pointer; }
    tr:hover { background:#0e1620; }
    @media (max-width: 760px) {
      .dtc { display:none; }
      table { font:12px/1.4 ui-monospace, Menlo, Consolas, monospace; }
      td { padding:2px 4px; }
      thead th { padding:4px 5px; }
      .nw { margin-right:3px; padding:1px 3px; }
    }
  `],
})
export class DecodesComponent implements OnInit, OnDestroy {
  decodes: Spot[] = [];
  qsoDecodes: Spot[] = [];
  dxCall = '';
  qsoCall = '';
  frozen = false;
  private subs: Subscription[] = [];
  private timer?: ReturnType<typeof setInterval>;
  private pollTimer?: ReturnType<typeof setInterval>;
  private histTimer?: ReturnType<typeof setInterval>;
  private workedSlots = new Set<string>();

  private histCalls = new Set<string>();
  private histSlots = new Set<string>();
  private histGrids = new Set<string>();
  hist: WorkedSets | null = null;

  private myCall = '';
  private myGrid = '';
  private units = 'km';
  private distCache = new Map<string, string>();

  constructor(private store: DecodeStore, private qso: QsoService, config: ConfigService,
              private chime: ChimeService, private api: ApiService) {
    this.subs.push(config.config$.subscribe((c) => {
      this.myCall = c.callsign.toUpperCase();
      if (c.grid !== this.myGrid || c.units !== this.units) this.distCache.clear();
      this.myGrid = c.grid;
      this.units = c.units;
      this.refresh();
    }));
    this.subs.push(qso.frame$.subscribe((f) => {
      this.dxCall = f.dx_call.trim().toUpperCase();
      if (this.dxCall) this.qsoCall = this.dxCall;
      else if (f.selected_tx === 6) this.qsoCall = '';
      this.refresh();
    }));

    this.subs.push(qso.completed$.subscribe(() => { this.qsoCall = ''; this.refresh(); }));
  }

  toggleFreeze(): void { this.frozen = !this.frozen; if (!this.frozen) this.refresh(); }
  toggleChime(): void { this.chime.setEnabled(!this.chime.isEnabled()); }
  chimeOn(): boolean { return this.chime.isEnabled(); }

  pick(d: Spot): void { this.qso.callDecode(d); }

  clearDecodes(): void {
    this.store.clear();
    this.qsoCall = this.dxCall;
  }

  isMine(d: Spot): boolean {
    if (!this.myCall) return false;
    return d.message.toUpperCase().split(/[\s<>]+/).includes(this.myCall);
  }

  private colorTokens(msg: string): { t: string; c: string }[] {
    return msg.trim().split(/\s+/).map((t) => {
      const u = t.replace(/[<>]/g, '').toUpperCase();
      const c = this.myCall && u === this.myCall ? 'mecall'
              : this.qsoCall && u === this.qsoCall ? 'dxcall' : '';
      return { t, c };
    });
  }

  dist(d: Spot): string {
    if (!d.grid || !this.myGrid) return '';
    let v = this.distCache.get(d.grid);
    if (v === undefined) {
      const km = gridDistanceKm(this.myGrid, d.grid);
      v = km === null ? '' : formatDistance(km, this.units);
      this.distCache.set(d.grid, v);
    }
    return v;
  }

  ngOnInit(): void {
    this.refresh();
    this.loadWorked();
    this.loadHistory();

    this.subs.push(this.store.reset$.subscribe(() => this.refresh()));
    this.pollTimer = setInterval(() => this.refresh(), 1000);
    this.timer = setInterval(() => this.loadWorked(), 45_000);
    this.histTimer = setInterval(() => this.loadHistory(), 30 * 60_000);
  }

  ngOnDestroy(): void {
    this.subs.forEach((s) => s.unsubscribe());
    if (this.timer) clearInterval(this.timer);
    if (this.pollTimer) clearInterval(this.pollTimer);
    if (this.histTimer) clearInterval(this.histTimer);
  }

  private refresh(): void {
    if (this.frozen) return;
    const list = this.store.list().slice(0, 300);

    for (const d of list) {
      d.worked = this.isWorked(d);
      d.toks = this.colorTokens(d.message);
      this.computeNew(d);
    }
    this.decodes = list;
    this.qsoDecodes = list.filter((d) => this.relevant(d)).slice(0, 60);
  }

  private loadHistory(): void {
    this.api.worked().subscribe((w) => {
      this.hist = w;
      this.histCalls = new Set(w.calls);
      this.histSlots = new Set(w.slots);
      this.histGrids = new Set(w.grids);
      this.refresh();
    });
  }

  syncHistory(): void {
    if (this.hist) this.hist.syncing = true;
    this.api.syncWorked().subscribe(() => setTimeout(() => this.loadHistory(), 6000));
  }

  histTitle(): string {
    if (!this.hist) return 'QRZ log sync';
    if (!this.hist.have_key) return 'No QRZ logbook key set (Settings)';
    const ago = this.hist.synced ? Math.round((Date.now() / 1000 - this.hist.synced) / 60) : -1;
    return `QRZ log: ${this.hist.qsos} QSOs, ${this.hist.grids.length} grids` +
           (ago >= 0 ? ` · synced ${ago} min ago` : ' · not yet synced') +
           (this.hist.error ? ` · ${this.hist.error}` : '') + ' · click to re-sync';
  }

  private computeNew(d: Spot): void {
    d.newCall = d.newSlot = d.newGrid = false;
    if (!d.message.startsWith('CQ')) return;
    const s = (d.sender || '').toUpperCase();
    if (s && s !== this.myCall && this.histCalls.size) {
      if (!this.histCalls.has(s)) d.newCall = true;
      else if (!this.histSlots.has(`${s}|${(d.band || '').toUpperCase()}|${(d.mode || '').toUpperCase()}`)) d.newSlot = true;
    }
    if (s && s !== this.myCall && d.grid && this.histGrids.size && !this.histGrids.has(d.grid.toUpperCase())) d.newGrid = true;
  }

  private loadWorked(): void {
    this.api.logs().subscribe((r) => {
      const active = r.logs.filter((l) => l.active);
      if (!active.length) { this.workedSlots = new Set(); return; }
      const slots = new Set<string>();
      let remaining = active.length;
      const done = () => { if (--remaining === 0) { this.workedSlots = slots; this.refresh(); } };
      active.forEach((l) => this.api.logQsos(l.file).subscribe({

        next: (q) => q.qsos.forEach((qso) => {
          const c = (qso['call'] || '').toUpperCase();
          if (c) slots.add(this.slotKey(c, qso['band'], qso['submode'] || qso['mode']));
        }),
        complete: done,
        error: done,
      }));
    });
  }

  private slotKey(call: string, band?: string, mode?: string): string {
    return `${call.toUpperCase()}|${(band || '').toUpperCase()}|${(mode || '').toUpperCase()}`;
  }

  isWorked(d: Spot): boolean {
    const s = (d.sender || '').toUpperCase();
    return !!s && s !== this.myCall && this.workedSlots.has(this.slotKey(s, d.band, d.mode));
  }

  private relevant(d: Spot): boolean {
    if (this.isMine(d)) return true;
    if (!this.qsoCall) return false;
    return d.message.toUpperCase().split(/[\s<>]+/).includes(this.qsoCall);
  }

  trackBy(_: number, d: Spot): string { return d.utc + '_' + d.freq + '_' + d.message; }
}

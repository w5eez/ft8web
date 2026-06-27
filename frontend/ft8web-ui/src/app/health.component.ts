// SPDX-License-Identifier: GPL-3.0-or-later

import { Component, OnInit, OnDestroy } from '@angular/core';
import { CommonModule } from '@angular/common';
import { ApiService } from './api.service';
import { DiagStatus } from './models';

@Component({
  selector: 'app-health',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="health" *ngIf="d as h; else loading">
      <div class="grid">

        <div class="card">
          <div class="ch">Raspberry Pi</div>
          <div class="row"><span>CPU temp</span>
            <b [class.warn]="(h.cpu_temp_c||0)>=70" [class.bad]="(h.cpu_temp_c||0)>=80">
              {{ h.cpu_temp_c != null ? (h.cpu_temp_c | number:'1.0-1') + ' °C' : '—' }}</b></div>
          <div class="row"><span>Load (1/5/15m)</span>
            <b [class.warn]="(h.load1||0)>=4">{{ h.load1 | number:'1.2-2' }} / {{ h.load5 | number:'1.2-2' }} / {{ h.load15 | number:'1.2-2' }}</b></div>
          <div class="row"><span>RAM free</span>
            <b [class.warn]="memPct(h)<10">{{ h.mem_avail_mb | number }} / {{ h.mem_total_mb | number }} MB</b></div>
          <div class="row"><span>Disk free</span>
            <b [class.warn]="(h.disk_free_mb||0)<1000">{{ (h.disk_free_mb||0)/1024 | number:'1.1-1' }} GB</b></div>
          <div class="row"><span>Uptime</span><b>{{ up(h.proc_uptime_s) }} <small>(host {{ up(h.sys_uptime_s) }})</small></b></div>
        </div>

        <div class="card">
          <div class="ch">Radio</div>
          <div class="row"><span>rigctld</span>
            <b><span class="dot" [class.ok]="h.rig_connected" [class.no]="!h.rig_connected">●</span>
               {{ h.rig_connected ? 'connected' : 'NO LINK' }}</b></div>
          <div class="row"><span>Dial</span><b>{{ h.rig_freq_hz ? (h.rig_freq_hz/1e6 | number:'1.3-3') + ' MHz' : '—' }}</b></div>
          <div class="row"><span>Mode</span><b>{{ h.mode }}</b></div>
          <div class="row"><span>TX inhibit</span>
            <b [class.bad]="h.tx_inhibit">{{ h.tx_inhibit ? 'TRIPPED (SWR)' : 'clear' }}</b></div>
        </div>

        <div class="card">
          <div class="ch">Audio capture</div>
          <div class="row"><span>Status</span>
            <b><span class="dot" [class.ok]="h.audio_running" [class.no]="!h.audio_running">●</span>
               {{ h.audio_running ? 'running' : 'STOPPED' }}</b></div>
          <div class="row"><span>Level</span>
            <div class="meter"><i [style.width.%]="levelPct(h.audio_level)"
                 [class.warn]="h.audio_clipping"></i></div></div>
          <div class="row"><span>Clipping</span><b [class.bad]="h.audio_clipping">{{ h.audio_clipping ? 'YES' : 'no' }}</b></div>
          <div class="row"><span>ALSA xruns</span><b [class.warn]="h.audio_xruns>0">{{ h.audio_xruns }}</b></div>
          <div class="row"><span>Device</span><b class="dev">{{ h.audio_device }}</b></div>
        </div>

        <div class="card">
          <div class="ch">Decode timing</div>
          <div class="row"><span>Avg / max wall</span>
            <b [class.warn]="(h.decode_max_s||0)>=2.5">{{ h.decode_avg_s | number:'1.2-2' }} / {{ h.decode_max_s | number:'1.2-2' }} s</b></div>
          <div class="row"><span>Last period</span><b>{{ lastDecode(h) }}</b></div>
          <div class="spark" *ngIf="h.decode_recent?.length">
            <i *ngFor="let s of h.decode_recent.slice(-24)" [style.height.%]="barH(s.wall_s)"
               [class.warn]="s.wall_s>=2.5" [title]="s.mode + ' ' + (s.wall_s | number:'1.2-2') + 's, ' + s.count + ' dec'"></i>
          </div>
          <div class="hint">budget ≈ FT8 1.6s / FT4 0.8s before the TX slot</div>
        </div>

        <div class="card">
          <div class="ch">Uploads &amp; links</div>
          <div class="row"><span>Upload queue</span>
            <b [class.warn]="h.up_failed>0">{{ h.up_pending }} pending · <span [class.bad]="h.up_failed>0">{{ h.up_failed }} failed</span> · {{ h.up_done }} done</b></div>
          <div class="row"><span>WS clients</span><b>{{ h.ws_clients }}</b></div>
          <div class="row"><span>GPS</span>
            <b><span class="dot" [class.ok]="h.gps_fix" [class.no]="!h.gps_fix">●</span>
               {{ h.gps_fix ? ('fix · ' + h.gps_grid) : 'no fix' }}</b></div>
        </div>

      </div>
      <div class="foot">auto-refreshing every 3 s<span *ngIf="err" class="e"> · last poll failed</span></div>
    </div>
    <ng-template #loading><div class="empty">{{ err ? 'cannot reach backend' : 'loading…' }}</div></ng-template>`,
  styles: [`
    .health { padding:4px; }
    .grid { display:grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap:14px; }
    .card { background:#0b1118; border:1px solid #1c2530; border-radius:10px; padding:12px 14px; }
    .ch { font:600 11px system-ui, sans-serif; text-transform:uppercase; letter-spacing:.6px;
          color:#8aa3b3; margin-bottom:8px; border-bottom:1px solid #15202b; padding-bottom:6px; }
    .row { display:flex; align-items:center; justify-content:space-between; gap:10px; padding:3px 0;
           font:13px/1.4 ui-monospace, monospace; color:#9fb0bd; }
    .row span { color:#7f93a3; }
    .row b { color:#e6edf3; font-weight:600; text-align:right; }
    .row b small { color:#6b7a86; font-weight:400; }
    .row b.warn { color:#e3b341; }
    .row b.bad  { color:#ff7b72; }
    .dot { font-size:10px; }
    .dot.ok { color:#3fb950; } .dot.no { color:#f85149; }
    .dev { font-size:11px; color:#7fb0c8; word-break:break-all; max-width:160px; }
    .meter { flex:1; max-width:130px; height:8px; background:#0d141d; border:1px solid #1c2530;
             border-radius:4px; overflow:hidden; }
    .meter i { display:block; height:100%; background:#3fb950; }
    .meter i.warn { background:#f85149; }
    .spark { display:flex; align-items:flex-end; gap:2px; height:36px; margin:6px 0 2px; }
    .spark i { flex:1; background:#2f6feb; border-radius:1px 1px 0 0; min-height:2px; }
    .spark i.warn { background:#e3b341; }
    .hint { font:11px system-ui, sans-serif; color:#6b7a86; }
    .foot { margin-top:12px; font:11px system-ui, sans-serif; color:#6b7a86; }
    .foot .e { color:#ff7b72; }
    .empty { padding:34px; text-align:center; color:#6b7a86; }
  `],
})
export class HealthComponent implements OnInit, OnDestroy {
  d: DiagStatus | null = null;
  err = false;
  private timer?: ReturnType<typeof setInterval>;

  constructor(private api: ApiService) {}

  ngOnInit(): void {
    this.poll();
    this.timer = setInterval(() => this.poll(), 3000);
  }
  ngOnDestroy(): void { if (this.timer) clearInterval(this.timer); }

  private poll(): void {
    this.api.diag().subscribe({
      next: (d) => { this.d = d; this.err = false; },
      error: () => { this.err = true; },
    });
  }

  memPct(h: DiagStatus): number {
    return h.mem_total_mb ? (h.mem_avail_mb! / h.mem_total_mb) * 100 : 100;
  }
  barH(wall: number): number { return Math.max(4, Math.min(100, (wall / 3.0) * 100)); }
  levelPct(lvl: number): number {
    if (!lvl || lvl <= 0) return 0;
    const db = 20 * Math.log10(lvl);
    return Math.max(0, Math.min(100, ((db + 60) / 60) * 100));
  }
  lastDecode(h: DiagStatus): string {
    const r = h.decode_recent;
    if (!r?.length) return '—';
    const l = r[r.length - 1];
    return `${l.count} decode${l.count === 1 ? '' : 's'} in ${l.wall_s.toFixed(2)}s`;
  }
  up(s?: number): string {
    if (s == null) return '—';
    const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
    if (d) return `${d}d ${h}h`;
    if (h) return `${h}h ${m}m`;
    return `${m}m`;
  }
}

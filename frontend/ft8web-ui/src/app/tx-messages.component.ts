// SPDX-License-Identifier: GPL-3.0-or-later

import { Component, OnInit, OnDestroy, HostListener } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Subscription } from 'rxjs';
import { ConfigService } from './config.service';
import { QsoService, EMPTY_QSO_FRAME } from './qso.service';
import { ApiService } from './api.service';
import { WsService } from './ws.service';
import { StationConfig, CallInfo, QsoFrame } from './models';

@Component({
  selector: 'app-tx-messages',
  standalone: true,
  imports: [CommonModule, FormsModule],
  template: `
    <div class="panel">
      <div class="head">
        <span class="title">Standard messages</span>
        <span class="opmode">{{ opModeLabel() }}</span>
        <button class="txbtn" [class.armed]="txArmed" [class.live]="txActive"
                (click)="toggleTx()"
                [title]="txArmed ? 'Disarm transmitter'
                         : radioMode === 'WSPR' ? 'Arm beacon: transmits ~25% of 2-minute slots'
                         : 'Arm: send the selected message every TX slot'">
          {{ txActive ? 'TX ●' : txArmed ? 'TX armed' : 'TX' }}
        </button>
      </div>
      <div class="txerr" *ngIf="txError">{{ txError }}</div>

      <ng-container *ngIf="radioMode === 'WSPR'">
        <div class="msg sel">
          <span class="txn">BCN</span>
          <span class="txt">{{ wsprMsg() }}</span>
        </div>
      </ng-container>

      <ng-container *ngIf="radioMode !== 'WSPR'">
      <div class="dxrow">
        <label>DX Call
          <input type="text" [ngModel]="frame.dx_call"
                 (ngModelChange)="qso.setDxCall($any($event).toUpperCase())"
                 autocapitalize="characters" spellcheck="false" />
        </label>
        <label>Grid
          <input type="text" [ngModel]="frame.dx_grid"
                 (ngModelChange)="qso.setDxGrid($any($event).toUpperCase())" spellcheck="false" />
        </label>
        <label>Rpt
          <input type="number" min="-30" max="30" step="1" [ngModel]="frame.report"
                 (ngModelChange)="qso.setReport(+$any($event) || 0)" />
        </label>
      </div>
      <div class="msgs">
        <label class="msg" *ngFor="let m of msgs; let i = index" [class.sel]="selected === i + 1">
          <input type="radio" name="nexttx" [value]="i + 1" [ngModel]="selected"
                 (ngModelChange)="qso.selectTx($any($event))" />
          <span class="txn">Tx{{ i + 1 }}</span>
          <span class="txt">{{ m }}</span>
        </label>
      </div>
      <div class="hint" *ngIf="!cfg.callsign">Set your callsign in Settings to generate real messages.</div>

      <div class="qrzcard" *ngIf="info?.ok">
        <img *ngIf="info!.image" [src]="info!.image" alt="" referrerpolicy="no-referrer" />
        <div class="qrzbody">
          <a class="qrzcall" [href]="qrzUrl()" target="_blank" rel="noopener noreferrer"
             title="open on QRZ.com">{{ qrzCall() }} ↗</a>
          <div class="qrzname">{{ info!.fname }} {{ info!.name }}</div>
          <div class="qrzline" *ngIf="info!.addr1">{{ info!.addr1 }}</div>
          <div class="qrzline">{{ [info!.addr2, info!.state, info!.zip].join(' ') }}</div>
          <div class="qrzline dim">{{ info!.country }}<span *ngIf="info!.grid"> · {{ info!.grid }}</span></div>
        </div>
      </div>
      <div class="qrzmiss" *ngIf="lookupNote">{{ lookupNote }}</div>
      </ng-container>
    </div>`,
  styles: [`
    .head { display:flex; align-items:center; justify-content:space-between; margin-bottom:8px; }
    .title { font:600 13px system-ui, sans-serif; color:#e6edf3; }
    .opmode { font:600 10px ui-monospace, monospace; color:#9fe3c0; background:#13202c;
              padding:2px 7px; border-radius:4px; letter-spacing:.5px; }
    .txbtn { margin-left:auto; background:#2a1d08; color:#f0b35e; border:1px solid #7a5a1e;
             border-radius:5px; padding:6px 14px; font:700 12px ui-monospace, monospace;
             letter-spacing:.5px; cursor:pointer; }
    .txbtn:hover { border-color:#f0883e; }
    .txbtn.armed { background:#5a1118; color:#ffd6d3; border-color:#f85149; }
    .txbtn.live { background:#f85149; color:#2a0606; animation: txpulse 1s infinite; }
    .txbtn:disabled { opacity:.4; cursor:default; }
    @keyframes txpulse { 50% { filter: brightness(1.35); } }
    .txerr { margin-bottom:8px; padding:6px 9px; background:#3a0d12; border:1px solid #f85149;
             border-radius:5px; color:#ffb3ae; font:12px system-ui, sans-serif; }
    .dxrow { display:flex; gap:8px; margin-bottom:10px; }
    .dxrow label { flex:1; display:flex; flex-direction:column; gap:3px;
                   font:10px system-ui, sans-serif; color:#6b7a86; text-transform:uppercase; }
    .dxrow input { width:100%; box-sizing:border-box; background:#13202c; color:#cdd9e3;
                   border:1px solid #1c2a38; border-radius:4px; padding:6px;
                   font:13px ui-monospace, monospace; text-transform:uppercase; }
    .dxrow label:nth-child(3) { max-width:64px; }
    .msgs { display:flex; flex-direction:column; gap:4px; }
    .msg { display:flex; align-items:center; gap:8px; padding:5px 7px; border-radius:5px;
           border:1px solid #15202b; cursor:pointer; }
    .msg:hover { border-color:#2f80ed; }
    .msg.sel { background:#11243a; border-color:#1f6feb; }
    .msg input { accent-color:#1f6feb; }
    .txn { font:600 11px ui-monospace, monospace; color:#8aa3b3; min-width:28px; }
    .txt { font:13px ui-monospace, monospace; color:#dfe7ee; word-break:break-all; }
    .hint { font-size:11px; color:#6b7a86; margin-top:8px; }
    .qrzcard { display:flex; gap:12px; margin-top:12px; padding:10px; background:#0d141d;
               border:1px solid #1c2a38; border-radius:8px; align-items:flex-start; }
    .qrzcard img { width:72px; height:72px; object-fit:cover; border-radius:6px; flex:none;
                   background:#13202c; }
    .qrzbody { min-width:0; }
    .qrzcall { display:inline-block; font:700 13px ui-monospace, monospace; color:#58a6ff;
               text-decoration:none; margin-bottom:2px; }
    .qrzcall:hover { text-decoration:underline; color:#79c0ff; }
    .qrzname { font:600 14px system-ui, sans-serif; color:#e6edf3; margin-bottom:3px; }
    .qrzline { font:12px system-ui, sans-serif; color:#9fb0bd; }
    .qrzline.dim { color:#6b7a86; margin-top:2px; }
    .qrzmiss { font-size:11px; color:#6b7a86; margin-top:8px; font-style:italic; }
  `],
})
export class TxMessagesComponent implements OnInit, OnDestroy {
  cfg: StationConfig = {
    callsign: '', grid: '', psk_enabled: false, op_mode: 'normal', fd_class: '', fd_section: '',
    lotw_user: '', lotw_pass: '', qrz_api_key: '', qrz_user: '', qrz_pass: '',
    pota_user: '', pota_pass: '',
    lotw_upload: false, qrz_upload: false,
    tx_even: true, auto_cq: false, qso_retries: 5, max_cqs: 0, cq_skip: false, find_clear_freq_before_cq: false, tx_level: 50,
    swr_protect: true, swr_max: 2.5, auto_grid: false, units: 'km',
    wf_gain: 1, wf_palette: 'Classic', show_calls: true,
  };
  frame: QsoFrame = EMPTY_QSO_FRAME;
  msgs: string[] = [];
  selected = 6;
  info: CallInfo | null = null;
  lookupNote = '';
  txArmed = false;
  txActive = false;
  txError = '';
  radioMode = 'FT8';
  rigPowerW = 0.5;
  private toggleAtMs = 0;
  private lookupTimer?: ReturnType<typeof setTimeout>;
  private lookedUp = '';
  private subs: Subscription[] = [];

  constructor(public qso: QsoService, private config: ConfigService,
              private api: ApiService, private ws: WsService) {}

  ngOnInit(): void {
    this.subs.push(this.config.config$.subscribe((c) => { this.cfg = c; }));
    this.subs.push(this.qso.frame$.subscribe((f) => {
      this.frame = f;
      this.msgs = f.messages;
      this.selected = f.selected_tx;
      this.scheduleLookup(f.dx_call);
    }));
    this.subs.push(this.ws.status$.subscribe((s) => {
      if (!s) return;
      this.txActive = s.tx_active;

      if (!s.tx_armed || Date.now() - this.toggleAtMs > 1500) this.txArmed = s.tx_armed;

      if (s.mode !== this.radioMode) this.radioMode = s.mode;
      this.rigPowerW = s.rig?.power_w ?? this.rigPowerW;
    }));
  }

  @HostListener('document:keydown', ['$event'])
  onKey(e: KeyboardEvent): void {
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const el = e.target as HTMLElement | null;
    const tag = el?.tagName;
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || el?.isContentEditable) return;

    if (e.key === 'Escape') { if (this.txArmed) this.toggleTx(); else this.qso.abort(); e.preventDefault(); }
    else if (e.key === 'Enter') { this.toggleTx(); e.preventDefault(); }
    else if (e.key >= '1' && e.key <= '6') { this.qso.selectTx(Number(e.key)); e.preventDefault(); }
  }

  toggleTx(): void {
    this.txError = '';
    this.toggleAtMs = Date.now();
    if (this.txArmed) {
      this.txArmed = false;
      this.api.setTx(false).subscribe();
      return;
    }
    this.api.setTx(true).subscribe({
      next: (r) => {
        if (!r.ok) { this.txError = r.error ?? 'TX failed'; this.txArmed = false; return; }
        this.txArmed = true;
      },
      error: () => { this.txError = 'TX failed (network)'; this.txArmed = false; },
    });
  }

  ngOnDestroy(): void {
    this.subs.forEach((s) => s.unsubscribe());
    if (this.lookupTimer !== undefined) clearTimeout(this.lookupTimer);
  }

  private scheduleLookup(call: string): void {
    const c = call.trim().toUpperCase();
    if (c === this.lookedUp) return;
    if (this.lookupTimer !== undefined) clearTimeout(this.lookupTimer);
    if (c.length < 3) { this.info = null; this.lookupNote = ''; this.lookedUp = ''; return; }
    if (!this.cfg.qrz_user) { this.info = null; this.lookupNote = ''; return; }
    this.lookupTimer = setTimeout(() => {
      this.lookedUp = c;
      this.api.lookup(c).subscribe((r) => {
        if (this.frame.dx_call.trim().toUpperCase() !== c) return;
        this.info = r.ok ? r : null;
        this.lookupNote = r.ok ? '' : (r.error?.includes('Not found') ? `${c} not found on QRZ` : r.error ?? '');
      });
    }, 500);
  }

  wsprDbm(): number {
    const valid = [0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40, 43, 47, 50, 53, 57, 60];
    const dbm = Math.round(10 * Math.log10(Math.max(0.001, this.rigPowerW) * 1000));
    return valid.reduce((a, b) => (Math.abs(b - dbm) < Math.abs(a - dbm) ? b : a), 0);
  }

  wsprMsg(): string {
    return `${this.cfg.callsign || 'NOCALL'} ${(this.cfg.grid || '').slice(0, 4)} ${this.wsprDbm()}`;
  }

  opModeLabel(): string {
    return this.cfg.op_mode === 'pota' ? 'POTA' : this.cfg.op_mode === 'fd' ? 'FIELD DAY' : 'NORMAL';
  }

  qrzCall(): string { return (this.info?.call || this.frame.dx_call || '').trim().toUpperCase(); }
  qrzUrl(): string { return 'https://www.qrz.com/db/' + encodeURIComponent(this.qrzCall()); }
}

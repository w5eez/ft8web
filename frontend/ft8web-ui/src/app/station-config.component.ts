// SPDX-License-Identifier: GPL-3.0-or-later

import { Component, OnInit, OnDestroy, Input } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Subscription } from 'rxjs';
import { ConfigService } from './config.service';
import { ApiService } from './api.service';
import { StationConfig } from './models';

@Component({
  selector: 'app-station-config',
  standalone: true,
  imports: [CommonModule, FormsModule],
  template: `
    <div class="sc">
      <ng-container *ngIf="section === 'station'">
      <div class="label">Callsign</div>
      <input type="text" [(ngModel)]="callsign" placeholder="e.g. W1ABC"
             autocapitalize="characters" autocomplete="off" spellcheck="false" />
      <div class="label">Grid locator</div>
      <input type="text" [(ngModel)]="grid" placeholder="e.g. EN34 or EN34lq"
             autocapitalize="characters" autocomplete="off" spellcheck="false" [readonly]="autoGrid" />
      <label class="chk" style="margin-top:6px;">
        <input type="checkbox" [(ngModel)]="autoGrid" />
        AutoGrid — set the grid from GPS
      </label>

      <div class="label">Units</div>
      <select [(ngModel)]="units">
        <option value="km">Kilometers</option>
        <option value="mi">Miles</option>
      </select>

      <div class="label">Operating mode</div>
      <select [(ngModel)]="opMode">
        <option value="normal">Normal</option>
        <option value="pota">POTA (CQ POTA)</option>
        <option value="fd">ARRL Field Day</option>
      </select>
      <div class="fdrow" *ngIf="opMode === 'fd'">
        <label>Class
          <input type="text" [(ngModel)]="fdClass" placeholder="1D"
                 autocapitalize="characters" spellcheck="false" />
        </label>
        <label>Section
          <input type="text" [(ngModel)]="fdSection" placeholder="MN"
                 autocapitalize="characters" spellcheck="false" />
        </label>
      </div>

      <label class="chk" [class.dim]="!callsign.trim()">
        <input type="checkbox" [(ngModel)]="pskEnabled" [disabled]="!callsign.trim()" />
        Enable PSK Reporter
      </label>
      <div class="hint" *ngIf="!callsign.trim()">PSK Reporter stays off until a callsign is set.</div>

      <label class="chk">
        <input type="checkbox" [(ngModel)]="showCalls" />
        Show Calls on Waterfall
      </label>

      <button class="restart" (click)="restartSvc()" [disabled]="restarting">
        {{ restarting ? 'restarting…' : 'Restart ft8web' }}
      </button>
      <button class="shutdown" (click)="shutdownPi()" [disabled]="shuttingDown">
        {{ shuttingDown ? 'shutting down…' : 'Shut Down Pi' }}
      </button>
      </ng-container>

      <ng-container *ngIf="section === 'transmit'">
      <div class="label">TX time slot</div>
      <select [(ngModel)]="txEven">
        <option [ngValue]="true">Even — :00 / :30 (1st)</option>
        <option [ngValue]="false">Odd — :15 / :45 (2nd)</option>
      </select>
      <label class="chk">
        <input type="checkbox" [(ngModel)]="autoCq" />
        Auto CQ — return to calling CQ after a completed QSO
      </label>
      <label class="chk">
        Max CQs (0 = unlimited)
        <input class="swrnum" type="number" min="0" max="99" step="1"
               [(ngModel)]="maxCqs" (click)="$event.preventDefault()" />
      </label>
      <label class="chk">
        <input type="checkbox" [(ngModel)]="cqSkip" />
        Skip a cycle during CQ
      </label>
      <label class="chk">
        <input type="checkbox" [(ngModel)]="findClearFreqBeforeCq" />
        Find clear frequency before CQ
      </label>
      <label class="chk">
        QSO retries
        <input class="swrnum" type="number" min="1" max="20" step="1"
               [(ngModel)]="qsoRetries" (click)="$event.preventDefault()" />
      </label>
      <label class="chk">
        <input type="checkbox" [(ngModel)]="swrProtect" />
        SWR protection — immediately disable TX if SWR exceeds
        <input class="swrnum" type="number" min="1.1" max="10" step="0.1"
               [(ngModel)]="swrMax" [disabled]="!swrProtect"
               (click)="$event.preventDefault()" />
      </label>
      <div class="hint">Trips kill PTT/tuner instantly and latch TX off until cleared from the rig panel.</div>
      </ng-container>

      <ng-container *ngIf="section === 'integrations'">
      <div class="section">LoTW (ARRL)</div>
      <label class="chk">
        <input type="checkbox" [(ngModel)]="lotwUpload" />
        Live upload QSOs to LoTW
      </label>
      <div class="pair">
        <label>LoTW username
          <input type="text" [(ngModel)]="lotwUser" autocomplete="off" spellcheck="false" />
        </label>
        <label>LoTW password
          <input type="password" [(ngModel)]="lotwPass" autocomplete="off"
                 [placeholder]="lotwPassSet ? '•••• saved — blank keeps it' : ''" />
        </label>
      </div>
      <div class="hint">Uploads authenticate via your certificate; credentials are for future confirmation sync.</div>

      <div class="section">QRZ</div>
      <label class="chk">
        <input type="checkbox" [(ngModel)]="qrzUpload" [disabled]="!qrzApiKey.trim() && !qrzApiKeySet" />
        Live upload QSOs to QRZ Logbook
      </label>
      <div class="label">QRZ Logbook API key</div>
      <input type="text" [(ngModel)]="qrzApiKey" autocomplete="off" spellcheck="false"
             [placeholder]="qrzApiKeySet ? '•••• saved — blank keeps it' : 'XXXX-XXXX-XXXX-XXXX'" />
      <div class="pair">
        <label>QRZ username (XML lookups)
          <input type="text" [(ngModel)]="qrzUser" autocomplete="off" spellcheck="false" />
        </label>
        <label>QRZ password
          <input type="password" [(ngModel)]="qrzPass" autocomplete="off"
                 [placeholder]="qrzPassSet ? '•••• saved — blank keeps it' : ''" />
        </label>
      </div>
      <div class="hint">Username/password enable callsign lookups (name, address, photo) on the messages panel.</div>

      <div class="section">POTA</div>
      <div class="pair">
        <label>POTA username
          <input type="text" [(ngModel)]="potaUser" autocomplete="off" spellcheck="false" />
        </label>
        <label>POTA password
          <input type="password" [(ngModel)]="potaPass" autocomplete="off"
                 [placeholder]="potaPassSet ? '•••• saved — blank keeps it' : ''" />
        </label>
      </div>
      </ng-container>

      <div class="row">
        <button (click)="save()">Save</button>
        <span class="saved" *ngIf="saved">saved ✓</span>
        <span class="allnote">saves all settings tabs</span>
      </div>
    </div>`,
  styles: [`
    .label { color:#6b7a86; font-size:11px; text-transform:uppercase; margin:10px 0 4px; }
    .section { color:#8aa3b3; font:600 11px system-ui, sans-serif; text-transform:uppercase;
               letter-spacing:.7px; margin:16px 0 4px; padding-top:10px; border-top:1px solid #1c2530; }
    input[type=text], input[type=password], select { width:100%; box-sizing:border-box; background:#13202c;
        color:#cdd9e3; border:1px solid #1c2a38; border-radius:4px; padding:7px; font:14px ui-monospace, monospace; }
    .sc > input[type=text] { text-transform:uppercase; }
    .sc input::placeholder { text-transform:none; }
    .label .val { float:right; color:#cdd9e3; text-transform:none; }
    .swrnum { width:64px; background:#13202c; color:#cdd9e3; border:1px solid #1c2a38;
              border-radius:4px; padding:4px 6px; font:13px ui-monospace, monospace; }
    input[type=range] { width:100%; box-sizing:border-box; }
    .pair { display:flex; gap:10px; margin-top:8px; }
    .pair label { flex:1; display:flex; flex-direction:column; gap:3px;
                  font:10px system-ui, sans-serif; color:#6b7a86; text-transform:uppercase; }
    .fdrow { display:flex; gap:10px; margin-top:8px; }
    .fdrow label { flex:1; display:flex; flex-direction:column; gap:3px;
                   font:10px system-ui, sans-serif; color:#6b7a86; text-transform:uppercase; }
    .chk { display:flex; align-items:center; gap:8px; margin-top:12px; font:13px system-ui, sans-serif;
           color:#cdd9e3; cursor:pointer; }
    .chk.dim { color:#5b6a76; }
    .hint { font-size:11px; color:#6b7a86; margin-top:6px; }
    .restart { margin-top:16px; background:#2a1d08; color:#f0b35e; border:1px solid #7a5a1e;
               border-radius:5px; padding:7px 14px; font:600 12px system-ui, sans-serif; cursor:pointer; }
    .restart:hover { border-color:#f0883e; }
    .restart:disabled { opacity:.5; cursor:default; }
    .shutdown { margin-top:10px; background:#2a0c0c; color:#f08a8a; border:1px solid #7a2020;
                border-radius:5px; padding:7px 14px; font:600 12px system-ui, sans-serif; cursor:pointer; }
    .shutdown:hover { border-color:#f04e4e; }
    .shutdown:disabled { opacity:.5; cursor:default; }
    .row { display:flex; align-items:center; gap:10px; margin-top:14px; }
    button { background:#1f6feb; color:#fff; border:0; border-radius:5px; padding:8px 18px;
             font:600 13px system-ui, sans-serif; cursor:pointer; }
    button:hover { background:#2f80ed; }
    .saved { color:#3fb950; font:12px ui-monospace, monospace; }
    .allnote { margin-left:auto; font-size:10px; color:#54626e; }
  `],
})
export class StationConfigComponent implements OnInit, OnDestroy {
  @Input() section: string = 'station';
  callsign = '';
  grid = '';
  pskEnabled = false;
  opMode: StationConfig['op_mode'] = 'normal';
  fdClass = '';
  fdSection = '';
  lotwUser = '';
  lotwPass = '';
  qrzApiKey = '';
  qrzUser = '';
  qrzPass = '';
  potaUser = '';
  potaPass = '';
  lotwPassSet = false;
  qrzApiKeySet = false;
  qrzPassSet = false;
  potaPassSet = false;
  lotwUpload = false;
  qrzUpload = false;
  txEven = true;
  autoCq = false;
  qsoRetries = 5;
  maxCqs = 0;
  cqSkip = false;
  findClearFreqBeforeCq = false;
  units = 'km';
  restarting = false;
  shuttingDown = false;

  restartSvc(): void {
    if (!confirm('Restart the ft8web service?')) return;
    this.restarting = true;
    this.api.restart().subscribe({ next: () => {}, error: () => {} });
    setTimeout(() => (this.restarting = false), 8000);
  }

  shutdownPi(): void {
    if (!confirm('Power down the Raspberry Pi? You will need to physically power it back on.')) return;
    this.shuttingDown = true;
    this.api.shutdown().subscribe({ next: () => {}, error: () => {} });
  }
  swrProtect = true;
  swrMax = 2.5;
  autoGrid = false;
  showCalls = true;
  saved = false;
  private sub?: Subscription;

  constructor(private config: ConfigService, private api: ApiService) {}

  ngOnInit(): void {
    this.sub = this.config.config$.subscribe((c) => {
      this.callsign = c.callsign;
      this.grid = c.grid;
      this.pskEnabled = c.psk_enabled;
      this.opMode = c.op_mode;
      this.fdClass = c.fd_class;
      this.fdSection = c.fd_section;
      this.lotwUser = c.lotw_user;
      this.qrzUser = c.qrz_user;
      this.lotwPass = ''; this.qrzApiKey = ''; this.qrzPass = '';
      this.potaUser = c.pota_user;
      this.potaPass = '';
      this.lotwPassSet = !!c.lotw_pass_set;
      this.qrzApiKeySet = !!c.qrz_api_key_set;
      this.qrzPassSet = !!c.qrz_pass_set;
      this.potaPassSet = !!c.pota_pass_set;
      this.lotwUpload = c.lotw_upload;
      this.qrzUpload = c.qrz_upload;
      this.txEven = c.tx_even;
      this.autoCq = c.auto_cq;
      this.qsoRetries = c.qso_retries;
      this.maxCqs = c.max_cqs;
      this.cqSkip = c.cq_skip;
      this.findClearFreqBeforeCq = c.find_clear_freq_before_cq;
      this.swrProtect = c.swr_protect;
      this.swrMax = c.swr_max;
      this.autoGrid = c.auto_grid;
      this.showCalls = c.show_calls;
      this.units = c.units;
    });
  }

  ngOnDestroy(): void { this.sub?.unsubscribe(); }

  save(): void {
    const cfg: Partial<StationConfig> = {
      callsign: this.callsign.trim().toUpperCase(),
      grid: this.grid.trim().toUpperCase(),
      psk_enabled: this.pskEnabled && !!this.callsign.trim(),
      op_mode: this.opMode,
      fd_class: this.fdClass.trim().toUpperCase(),
      fd_section: this.fdSection.trim().toUpperCase(),
      lotw_user: this.lotwUser.trim(),
      qrz_user: this.qrzUser.trim(),
      pota_user: this.potaUser.trim(),
      lotw_upload: this.lotwUpload,
      qrz_upload: this.qrzUpload && (!!this.qrzApiKey.trim() || this.qrzApiKeySet),
      tx_even: this.txEven,
      auto_cq: this.autoCq,
      qso_retries: Math.max(1, Math.min(20, Math.round(+this.qsoRetries) || 5)),
      max_cqs: Math.max(0, Math.min(99, Math.round(+this.maxCqs) || 0)),
      cq_skip: this.cqSkip,
      find_clear_freq_before_cq: this.findClearFreqBeforeCq,
      swr_protect: this.swrProtect,
      swr_max: +this.swrMax || 2.5,
      auto_grid: this.autoGrid,
      show_calls: this.showCalls,
      units: this.units,
    };

    if (this.lotwPass) cfg.lotw_pass = this.lotwPass;
    if (this.qrzApiKey.trim()) cfg.qrz_api_key = this.qrzApiKey.trim();
    if (this.qrzPass) cfg.qrz_pass = this.qrzPass;
    if (this.potaPass) cfg.pota_pass = this.potaPass;
    this.config.save(cfg).subscribe(() => {
      this.saved = true;
      setTimeout(() => (this.saved = false), 2000);
    });
  }
}

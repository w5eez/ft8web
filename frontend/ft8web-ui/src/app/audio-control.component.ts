// SPDX-License-Identifier: GPL-3.0-or-later

import { Component, OnInit, OnDestroy } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Subscription } from 'rxjs';
import { WsService } from './ws.service';
import { ApiService } from './api.service';
import { ConfigService } from './config.service';
import { Status, AudioDevice } from './models';

@Component({
  selector: 'app-audio-control',
  standalone: true,
  imports: [CommonModule, FormsModule],
  template: `
    <div class="ac">
      <div class="label">Audio input</div>
      <select [(ngModel)]="device">
        <option *ngFor="let d of devices" [value]="d.id">{{ d.name }}</option>
      </select>

      <div class="label">Input level <span class="clip" [class.on]="clip">CLIP</span></div>
      <div class="meter"><i [style.left.%]="levelPct()"></i></div>

      <div class="label">Capture gain <span>{{ hasGain ? gain + '%' : 'fixed' }}</span></div>
      <input type="range" min="0" max="100" step="1" [(ngModel)]="gain" [disabled]="!hasGain" />
      <div class="hint" *ngIf="hasGain">Hardware ADC gain — set so the level meter sits ~⅓–½ on band noise (Save to apply, then watch).</div>
      <div class="hint" *ngIf="!hasGain">This device exposes no hardware gain control.</div>

      <div class="label">TX audio level <span>{{ txLevel }}%</span></div>
      <input type="range" min="0" max="100" step="1" [(ngModel)]="txLevel" />
      <div class="hint">Transmit drive — set so the rig's ALC barely moves (FT8 wants a low audio level). Fine control is at the low end.</div>

      <div class="row">
        <button (click)="save()">Save</button>
        <span class="saved" *ngIf="saved">saved ✓</span>
      </div>
    </div>`,
  styles: [`
    .label { color:#6b7a86; font-size:11px; text-transform:uppercase; margin:10px 0 4px; }
    .label span { color:#cdd9e3; float:right; text-transform:none; }
    .label .clip { float:right; font:600 10px ui-monospace, monospace; color:#3a4a55;
                   padding:0 5px; border-radius:3px; letter-spacing:.5px; }
    .label .clip.on { color:#fff; background:#f85149; }
    select, input[type=range] { width:100%; box-sizing:border-box; }
    select { background:#13202c; color:#cdd9e3; border:1px solid #1c2a38; border-radius:4px; padding:6px; }
    .meter { position:relative; width:100%; box-sizing:border-box; height:14px; border-radius:5px; overflow:hidden;
             background:linear-gradient(90deg,#2a8 0%,#3fb950 50%,#ffd166 78%,#f85149 100%); }

    .meter i { position:absolute; top:0; bottom:0; right:0; background:#0d141d; transition:left .08s; }
    .hint { font-size:11px; color:#6b7a86; margin-top:6px; }
    .row { display:flex; align-items:center; gap:10px; margin-top:16px; }
    button { background:#1f6feb; color:#fff; border:0; border-radius:5px; padding:8px 18px;
             font:600 13px system-ui, sans-serif; cursor:pointer; }
    button:hover { background:#2f80ed; }
    .saved { color:#3fb950; font:12px ui-monospace, monospace; }
  `],
})
export class AudioControlComponent implements OnInit, OnDestroy {
  devices: AudioDevice[] = [];
  device = '';
  gain = 50;
  hasGain = false;
  clip = false;
  txLevel = 50;
  saved = false;
  private status: Status | null = null;
  private loadedDevice = '';
  private subs: Subscription[] = [];
  private flashTimer?: ReturnType<typeof setTimeout>;

  constructor(private ws: WsService, private api: ApiService, private config: ConfigService) {}

  ngOnInit(): void {
    this.loadAudio();
    this.subs.push(this.ws.status$.subscribe((s) => {
      this.status = s;
      if (s) { this.hasGain = s.audio.hasGain; this.clip = s.audio.clip; }
    }));
    this.subs.push(this.config.config$.subscribe((c) => (this.txLevel = c.tx_level)));
  }

  ngOnDestroy(): void {
    this.subs.forEach((s) => s.unsubscribe());
    if (this.flashTimer) clearTimeout(this.flashTimer);
  }

  save(): void {
    const applyRest = () => {
      if (this.hasGain) this.api.setGain(this.gain).subscribe();
      this.config.savePartial({ tx_level: this.txLevel }).subscribe();
      this.saved = true;
      if (this.flashTimer) clearTimeout(this.flashTimer);
      this.flashTimer = setTimeout(() => (this.saved = false), 2000);
    };
    if (this.device && this.device !== this.loadedDevice) {
      this.api.setDevice(this.device).subscribe(() => { this.loadedDevice = this.device; applyRest(); });
    } else {
      applyRest();
    }
  }

  private loadAudio(): void {
    this.api.audio().subscribe((a) => {
      this.devices = a.devices;
      this.device = a.device;
      this.loadedDevice = a.device;
      this.hasGain = a.hasGain;
      if (a.captureVolume >= 0) this.gain = a.captureVolume;
    });
  }
  levelPct(): number {
    const lvl = this.status?.audio?.level ?? 0;
    if (lvl <= 0) return 0;
    const db = 20 * Math.log10(lvl);
    return Math.max(0, Math.min(100, ((db + 60) / 60) * 100));
  }
}

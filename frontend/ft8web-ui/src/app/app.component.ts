// SPDX-License-Identifier: GPL-3.0-or-later

import { Component, OnInit, HostListener } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { WaterfallComponent } from './waterfall.component';
import { DecodesComponent } from './decodes.component';
import { RigControlComponent } from './rig-control.component';
import { AudioControlComponent } from './audio-control.component';
import { CycleComponent } from './cycle.component';
import { MapsComponent } from './maps.component';
import { StationConfigComponent } from './station-config.component';
import { TxMessagesComponent } from './tx-messages.component';
import { LogsComponent } from './logs.component';
import { PotaComponent } from './pota.component';
import { HealthComponent } from './health.component';
import { RadiosConfigComponent } from './radios-config.component';
import { RadioService } from './radio.service';
import { Radio, Status } from './models';
import { WsService } from './ws.service';

interface Banner { icon: string; text: string; level: 'error' | 'warn'; }

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [
    CommonModule, FormsModule, WaterfallComponent, DecodesComponent, RigControlComponent,
    AudioControlComponent, CycleComponent, MapsComponent, StationConfigComponent,
    TxMessagesComponent, LogsComponent, PotaComponent, HealthComponent, RadiosConfigComponent,
  ],
  template: `
    <header>
      <div class="brand">
        <h1>ft8web</h1>
        <span class="de">de W5EEZ</span>
      </div>
      <nav class="tabs">
        <button [class.active]="tab==='main'" (click)="tab='main'">MAIN</button>
        <button [class.active]="tab==='maps'" (click)="tab='maps'">MAPS</button>
        <button [class.active]="tab==='logs'" (click)="tab='logs'">LOGS</button>
        <button [class.active]="tab==='pota'" (click)="tab='pota'">POTA</button>
        <button [class.active]="tab==='health'" (click)="tab='health'">HEALTH</button>
      </nav>
      <app-cycle></app-cycle>
      <span class="conn" [class.on]="connected" [title]="connected ? 'connected' : 'disconnected'">
        <span class="dot">{{ connected ? '●' : '○' }}</span><span class="ctext">{{ connected ? 'connected' : 'offline' }}</span>
      </span>
      <select class="radiosel" *ngIf="radios.length > 1" [ngModel]="activeRadio"
              (ngModelChange)="radio.setActive($event)" title="active radio">
        <option *ngFor="let r of radios; let i = index" [ngValue]="i">📻 {{ r.name }}</option>
      </select>
      <button class="gear" (click)="showSettings = true" title="Settings" aria-label="Settings">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor"
             stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="12" cy="12" r="3"></circle>
          <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33
                   1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82
                   1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51
                   1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"></path>
        </svg>
      </button>
    </header>

    <div class="banners" *ngIf="banners.length">
      <div class="banner" *ngFor="let b of banners" [class.warn]="b.level === 'warn'">
        <span class="bicon">{{ b.icon }}</span><span class="btext">{{ b.text }}</span>
      </div>
    </div>

    <main [hidden]="tab !== 'main'">
      <section class="view">
        <app-waterfall></app-waterfall>
        <app-decodes></app-decodes>
      </section>
      <aside class="controls">
        <app-rig-control></app-rig-control>
        <app-tx-messages></app-tx-messages>
      </aside>
    </main>

    <div class="page maps2" [hidden]="tab !== 'maps'">
      <app-maps source="heard" label="Stations heard by this receiver"></app-maps>
      <app-maps source="heardBy" label="Stations hearing me (PSK Reporter)"></app-maps>
    </div>

    <div class="page" [hidden]="tab !== 'logs'">
      <app-logs></app-logs>
    </div>

    <div class="page" [hidden]="tab !== 'pota'">
      <app-pota></app-pota>
    </div>

    <div class="page" *ngIf="tab === 'health'">
      <app-health></app-health>
    </div>

    <div class="overlay" *ngIf="showSettings" (click)="showSettings = false">
      <div class="dialog" (click)="$event.stopPropagation()">
        <div class="dlg-head">
          <span>Settings</span>
          <button (click)="showSettings = false" aria-label="Close">✕</button>
        </div>
        <nav class="dlg-tabs">
          <button [class.active]="settingsTab==='station'" (click)="settingsTab='station'">Station</button>
          <button [class.active]="settingsTab==='transmit'" (click)="settingsTab='transmit'">Transmit</button>
          <button [class.active]="settingsTab==='integrations'" (click)="settingsTab='integrations'">Integrations</button>
          <button [class.active]="settingsTab==='audio'" (click)="settingsTab='audio'">Audio</button>
          <button [class.active]="settingsTab==='radios'" (click)="settingsTab='radios'">Radios</button>
        </nav>
        <app-station-config [hidden]="settingsTab === 'audio' || settingsTab === 'radios'" [section]="settingsTab"></app-station-config>
        <app-audio-control [hidden]="settingsTab !== 'audio'"></app-audio-control>
        <app-radios-config [hidden]="settingsTab !== 'radios'"></app-radios-config>
      </div>
    </div>`,
  styles: [`
    :host { display:block; }
    header { display:flex; align-items:center; gap:16px; padding:10px 16px; flex-wrap:wrap; row-gap:8px;
             background:#0b1118; border-bottom:1px solid #1c2530; }
    h1 { margin:0; font:600 18px/1 system-ui, sans-serif; color:#e6edf3; white-space:nowrap; }
    .brand { display:flex; flex-direction:column; gap:1px; }
    .de { font:italic 600 10px/1 ui-monospace, monospace; color:#f0b35e; letter-spacing:1.5px; }
    .tabs { display:flex; gap:4px; }
    .tabs button { background:transparent; color:#8aa3b3; border:1px solid #1c2a38; border-radius:6px;
                   padding:6px 14px; cursor:pointer; font:600 12px system-ui, sans-serif; letter-spacing:.5px; }
    .tabs button.active { background:#13202c; color:#e6edf3; border-color:#2f80ed; }
    app-cycle { flex:1 1 200px; display:flex; min-width:0; }
    .conn { font:12px ui-monospace, monospace; color:#f55; white-space:nowrap; }
    .ctext { margin-left:5px; }
    .conn.on { color:#3fb950; }
    .banners { display:flex; flex-direction:column; }
    .banner { display:flex; align-items:center; gap:10px; padding:9px 16px;
              background:#4a0f15; color:#ffd9d9; border-bottom:1px solid #6e1a23;
              font:600 13px/1.35 system-ui, sans-serif; }
    .banner.warn { background:#41310c; color:#ffe6b8; border-bottom-color:#664c14; }
    .bicon { font-size:15px; flex:0 0 auto; }
    .btext { min-width:0; }
    .gear { display:flex; align-items:center; justify-content:center; background:transparent;
            color:#8aa3b3; border:1px solid #1c2a38; border-radius:6px; width:34px; height:34px;
            cursor:pointer; padding:0; }
    .gear:hover { color:#e6edf3; border-color:#2f80ed; }
    .radiosel { background:#13202c; color:#e6edf3; border:1px solid #1c2a38; border-radius:6px;
                padding:6px 8px; font:600 12px system-ui, sans-serif; cursor:pointer; max-width:150px; }
    .radiosel:hover { border-color:#2f80ed; }
    main { display:grid; grid-template-columns: 1fr 320px; gap:14px; padding:14px; align-items:start; }
    main[hidden], .page[hidden] { display:none !important; }
    .view { display:flex; flex-direction:column; gap:14px; min-width:0; }
    .controls { display:flex; flex-direction:column; gap:14px; }
    .page { padding:14px; }
    .maps2 { display:flex; flex-direction:column; gap:22px; }
    .overlay { position:fixed; inset:0; background:rgba(0,0,0,.55); display:flex;
               align-items:flex-start; justify-content:center; padding-top:8vh; z-index:50; }
    .dialog { width:min(500px, 94vw); background:#0b1118; border:1px solid #1c2530;
              border-radius:10px; box-shadow:0 12px 40px rgba(0,0,0,.5);
              max-height:86vh; overflow-y:auto; }
    .dlg-tabs { display:flex; gap:4px; padding:10px 14px 0; border-bottom:1px solid #1c2530; }
    .dlg-tabs button { background:transparent; color:#8aa3b3; border:0; border-bottom:2px solid transparent;
                       padding:6px 10px; cursor:pointer; font:600 12px system-ui, sans-serif; }
    .dlg-tabs button.active { color:#e6edf3; border-bottom-color:#1f6feb; }
    .dlg-tabs button:hover { color:#e6edf3; }
    .dlg-head { display:flex; align-items:center; justify-content:space-between; padding:12px 14px;
                border-bottom:1px solid #1c2530; font:600 14px system-ui, sans-serif; color:#e6edf3; }
    .dlg-head button { background:transparent; border:0; color:#8aa3b3; font-size:16px; cursor:pointer; }
    .dlg-head button:hover { color:#e6edf3; }
    .dialog app-station-config { display:block; padding:6px 16px 16px; }
    .dialog app-audio-control { display:block; padding:6px 16px 16px; }
    .dialog app-radios-config { display:block; padding:10px 16px 16px; }
    .dialog app-station-config[hidden], .dialog app-audio-control[hidden],
    .dialog app-radios-config[hidden] { display:none; }
    @media (max-width: 820px) {

      main { display:flex; flex-direction:column; }
      .view, .controls { display:contents; }
      main app-waterfall   { order:1; }
      main app-tx-messages { order:2; }
      main app-decodes     { order:3; }
      main app-rig-control { order:4; }
    }
    @media (max-width: 760px) {
      app-cycle { order:10; flex-basis:100%; }
      .ctext { display:none; }
    }
  `],
})
export class AppComponent implements OnInit {
  connected = false;
  showSettings = false;
  tab: 'main' | 'maps' | 'logs' | 'pota' | 'health' = 'main';
  settingsTab: 'station' | 'transmit' | 'integrations' | 'audio' | 'radios' = 'station';
  radios: Radio[] = [];
  activeRadio = 0;
  banners: Banner[] = [];

  constructor(private ws: WsService, public radio: RadioService) {}

  ngOnInit(): void {
    this.ws.connected$.subscribe((c) => {
      this.connected = c;
      if (!c) this.banners = [];
    });
    this.ws.status$.subscribe((s) => { if (this.connected) this.banners = this.deriveBanners(s); });
    this.radio.radios$.subscribe((r) => (this.radios = r));
    this.radio.active$.subscribe((a) => (this.activeRadio = a));
  }

  private deriveBanners(s: Status | null): Banner[] {
    if (!s) return [];
    const out: Banner[] = [];
    if (s.clock_synced === false)
      out.push({ level: 'error', icon: '⏱', text:
        'System clock is NOT synced — FT8 timing is off and decodes will fail until the time is set (internet/NTP or GPS).' });
    if (s.audio?.error)
      out.push({ level: 'error', icon: '⛔', text: 'Audio device error: ' + s.audio.error });
    else if (s.audio?.silent)
      out.push({ level: 'error', icon: '🔇', text:
        'No audio from the radio — capturing silence. Check the audio device selection (Settings → Audio) and cabling.' });
    if (s.audio?.clip)
      out.push({ level: 'warn', icon: '⚠', text:
        'Input audio is clipping — turn the radio / USB audio level down for clean decodes.' });
    return out;
  }

  @HostListener('document:keydown.escape')
  onEsc(): void { this.showSettings = false; }
}

// SPDX-License-Identifier: GPL-3.0-or-later

import { Component, OnInit, OnDestroy } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Subscription } from 'rxjs';
import { RadioService } from './radio.service';
import { ApiService } from './api.service';
import { Radio, AudioDevice, HamlibRig, SerialPort } from './models';

@Component({
  selector: 'app-radios-config',
  standalone: true,
  imports: [CommonModule, FormsModule],
  template: `
    <div class="radios">
      <div class="rcard" *ngFor="let r of radios; let i = index" [class.on]="active === i">
        <div class="rtop">
          <label class="ract"><input type="radio" name="active" [value]="i" [(ngModel)]="active" /> active</label>
          <input class="rname" [(ngModel)]="r.name" placeholder="name" />
          <button class="del" (click)="remove(i)" aria-label="delete radio" title="delete">✕</button>
        </div>
        <div class="rrow">
          <span>Rig</span>
          <select [(ngModel)]="r.model">
            <option [ngValue]="0">— select Hamlib model —</option>
            <option *ngFor="let m of rigs" [ngValue]="m.model">{{ m.mfg }} {{ m.name }} ({{ m.model }})</option>
          </select>
        </div>
        <div class="rrow">
          <span>CAT port</span>
          <select class="grow" [(ngModel)]="r.serial">
            <option value="">— select serial —</option>
            <option *ngFor="let p of ports" [value]="p.path">{{ p.name }}</option>
            <option *ngIf="r.serial && !knownPort(r.serial)" [value]="r.serial">{{ shortPath(r.serial) }} (not present)</option>
          </select>
          <input class="baud" type="number" [(ngModel)]="r.baud" title="baud" />
        </div>
        <div class="rrow">
          <span>Audio</span>
          <select class="grow" [(ngModel)]="r.device">
            <option value="">— select card —</option>
            <option *ngFor="let d of devices" [value]="d.id">{{ d.name }}</option>
            <option *ngIf="r.device && !knownDevice(r.device)" [value]="r.device">{{ r.device }} (not present)</option>
          </select>
          <label class="pw">max W <input class="pwn" type="number" [(ngModel)]="r.power_max_w" /></label>
        </div>
      </div>

      <div class="ractions">
        <button class="add" (click)="add()">+ Add radio</button>
        <button class="save" (click)="save()">Save radios</button>
        <span class="flash" *ngIf="flash">{{ flash }}</span>
      </div>
    </div>`,
  styles: [`
    .radios { display:flex; flex-direction:column; gap:12px; }
    .rhint { font:12px/1.5 system-ui, sans-serif; color:#8aa3b3; }
    .rcard { border:1px solid #1c2530; border-radius:8px; padding:10px 12px; display:flex;
             flex-direction:column; gap:8px; }
    .rcard.on { border-color:#2f80ed; background:#0c1622; }
    .rtop { display:flex; align-items:center; gap:10px; }
    .ract { font:11px system-ui, sans-serif; color:#9fb0bd; display:flex; align-items:center; gap:4px; white-space:nowrap; }
    .rname { flex:1; background:#0d141d; color:#e6edf3; border:1px solid #1c2a38; border-radius:6px;
             padding:6px 8px; font:600 13px system-ui, sans-serif; }
    .del { background:transparent; color:#8aa3b3; border:1px solid #1c2a38; border-radius:6px;
           width:28px; height:28px; cursor:pointer; }
    .del:hover { color:#f85149; border-color:#f85149; }
    .rrow { display:flex; align-items:center; gap:8px; font:12px ui-monospace, monospace; color:#9fb0bd; }
    .rrow > span { color:#7f93a3; min-width:58px; }
    .rrow input, .rrow select { background:#0d141d; color:#e6edf3; border:1px solid #1c2a38;
                                border-radius:6px; padding:5px 7px; font:12px ui-monospace, monospace; }
    .rrow select { flex:1; min-width:0; }
    .grow { flex:1; }
    .baud { width:74px; flex:none; } .pwn { width:60px; }
    .pw { display:flex; align-items:center; gap:5px; white-space:nowrap; flex:none; }
    .ractions { display:flex; align-items:center; gap:10px; margin-top:4px; }
    .add, .save { border-radius:6px; padding:7px 14px; font:600 12px system-ui, sans-serif; cursor:pointer; }
    .add { background:#13202c; color:#9fb0bd; border:1px solid #2a3a4a; }
    .save { background:#1f6feb; color:#fff; border:1px solid #1f6feb; }
    .flash { font:12px system-ui, sans-serif; color:#3fb950; }
  `],
})
export class RadiosConfigComponent implements OnInit, OnDestroy {
  radios: Radio[] = [];
  active = 0;
  rigs: HamlibRig[] = [];
  ports: SerialPort[] = [];
  devices: AudioDevice[] = [];
  flash = '';
  private subs: Subscription[] = [];
  private flashTimer?: ReturnType<typeof setTimeout>;

  constructor(private radio: RadioService, private api: ApiService) {}

  ngOnInit(): void {
    this.subs.push(this.radio.radios$.subscribe((r) => (this.radios = r.map((x) => ({ ...x })))));
    this.subs.push(this.radio.active$.subscribe((a) => (this.active = a)));
    this.api.hamlibRigs().subscribe((r) =>
      (this.rigs = r.rigs.sort((a, b) => (a.mfg + a.name).localeCompare(b.mfg + b.name))));
    this.api.serialPorts().subscribe((r) => (this.ports = r.ports));
    this.api.audio().subscribe((a) => (this.devices = a.devices));
  }
  ngOnDestroy(): void {
    this.subs.forEach((s) => s.unsubscribe());
    if (this.flashTimer) clearTimeout(this.flashTimer);
  }

  knownPort(p: string): boolean { return this.ports.some((x) => x.path === p); }
  knownDevice(id: string): boolean { return this.devices.some((d) => d.id === id); }
  shortPath(p: string): string { return p.split('/').pop() || p; }

  add(): void {
    this.radios.push({ name: 'New radio', model: 0, serial: '', baud: 38400, device: '', power_max_w: 100 });
  }
  remove(i: number): void {
    this.radios.splice(i, 1);
    if (this.active >= this.radios.length) this.active = Math.max(0, this.radios.length - 1);
  }
  save(): void {
    this.radio.saveList(this.radios, this.active);
    this.flash = 'Saved — radio applied';
    if (this.flashTimer) clearTimeout(this.flashTimer);
    this.flashTimer = setTimeout(() => (this.flash = ''), 2500);
  }
}

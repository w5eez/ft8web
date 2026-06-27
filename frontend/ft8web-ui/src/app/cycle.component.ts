// SPDX-License-Identifier: GPL-3.0-or-later

import { Component, OnInit, OnDestroy, NgZone, ChangeDetectorRef } from '@angular/core';
import { CommonModule } from '@angular/common';
import { Subscription } from 'rxjs';
import { WsService } from './ws.service';
import { QsoService } from './qso.service';

@Component({
  selector: 'app-cycle',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="cycle">
      <span class="clock">{{ clock }}</span>
      <span class="flip" *ngIf="flipped"
            title="transmitting in the opposite slot to work a same-slot station; restores automatically">FLIP</span>
      <div class="bar" [title]="mode + ' T/R ' + period + 's'">
        <i [style.width.%]="progress * 100"></i>
      </div>
      <span class="tleft">{{ mode }} {{ inPeriod | number:'1.1-1' }}/{{ period }}s</span>
    </div>`,
  styles: [`
    .cycle { display:flex; align-items:center; gap:10px; flex:1; max-width:520px; }
    .clock { font:600 14px ui-monospace, monospace; color:#7fd1ff; white-space:nowrap; }
    .flip { font:700 10px ui-monospace, monospace; color:#f0883e; background:#2a1d08;
            border:1px solid #7a5a1e; border-radius:3px; padding:1px 6px; letter-spacing:.5px;
            white-space:nowrap; }
    .bar { flex:1; min-width:90px; height:10px; background:#0d141d; border:1px solid #1c2530;
           border-radius:5px; overflow:hidden; }
    .bar i { display:block; height:100%; background:linear-gradient(90deg,#1f6feb,#3fb950); }
    .tleft { font:12px ui-monospace, monospace; color:#8aa3b3; white-space:nowrap; }
  `],
})
export class CycleComponent implements OnInit, OnDestroy {
  clock = '--:--:--';
  progress = 0;
  inPeriod = 0;
  period = 15;
  mode = 'FT8';
  flipped = false;

  private offsetMs = 0;
  private haveOffset = false;
  private subs: Subscription[] = [];
  private timer?: ReturnType<typeof setInterval>;

  constructor(private ws: WsService, private qso: QsoService,
              private zone: NgZone, private cdr: ChangeDetectorRef) {}

  ngOnInit(): void {
    this.subs.push(this.ws.status$.subscribe((s) => {
      if (!s) return;
      this.offsetMs = s.t_ms - Date.now();
      this.haveOffset = true;
      this.period = s.period || 15;
      this.mode = s.mode;
    }));
    this.subs.push(this.qso.frame$.subscribe((f) => { this.flipped = f.parity.flipped; }));

    this.zone.runOutsideAngular(() => {
      this.timer = setInterval(() => this.tick(), 100);
    });
  }

  ngOnDestroy(): void {
    this.subs.forEach((s) => s.unsubscribe());
    if (this.timer !== undefined) clearInterval(this.timer);
  }

  private tick(): void {
    if (!this.haveOffset) return;
    const serverMs = Date.now() + this.offsetMs;
    this.inPeriod = (serverMs / 1000) % this.period;
    this.progress = this.inPeriod / this.period;
    this.clock = new Date(serverMs).toISOString().substring(11, 19);
    this.cdr.detectChanges();
  }
}

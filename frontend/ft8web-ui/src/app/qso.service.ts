// SPDX-License-Identifier: GPL-3.0-or-later

import { Injectable } from '@angular/core';
import { BehaviorSubject, Subject } from 'rxjs';
import { Spot } from './decode-store';
import { QsoFrame } from './models';
import { ApiService } from './api.service';
import { WsService } from './ws.service';
import { ChimeService } from './chime.service';

export const EMPTY_QSO_FRAME: QsoFrame = {
  type: 'qso', seq: 0, dx_call: '', dx_grid: '', report: 0, rcvd_report: '',
  selected_tx: 6, messages: ['', '', '', '', '', ''], complete: false,
  attempts: 0, retries_max: 5,
  parity: { home: true, effective: true, flipped: false }, last_event: '',
};

@Injectable({ providedIn: 'root' })
export class QsoService {
  readonly frame$ = new BehaviorSubject<QsoFrame>(EMPTY_QSO_FRAME);
  readonly completed$ = new Subject<void>();

  private lastSeq = 0;

  constructor(private api: ApiService, private ws: WsService, private chime: ChimeService) {
    this.ws.qso$.subscribe((f) => this.apply(f));
    this.api.qsoState().subscribe({ next: (f) => this.apply(f), error: () => {} });

    this.ws.connected$.subscribe((c) => { if (!c) this.lastSeq = 0; });
  }

  private apply(f: QsoFrame): void {
    if (f.seq && f.seq < this.lastSeq) return;
    this.lastSeq = f.seq || this.lastSeq;
    this.frame$.next(f);
    if (f.last_event === 'completed') { this.chime.qsoComplete(); this.completed$.next(); }
  }

  callDecode(s: Spot): void {
    this.api.qsoCall({ utc: s.utc, snr: s.snr, freq: s.freq, mode: s.mode, message: s.message })
      .subscribe({ error: () => {} });
  }
  setDxCall(call: string): void { this.api.qsoSet({ dx_call: call }).subscribe({ error: () => {} }); }
  setDxGrid(grid: string): void { this.api.qsoSet({ dx_grid: grid }).subscribe({ error: () => {} }); }
  setReport(report: number): void { this.api.qsoSet({ report }).subscribe({ error: () => {} }); }
  selectTx(n: number): void { this.api.qsoSelectTx(n).subscribe({ error: () => {} }); }
  abort(): void { this.api.qsoAbort().subscribe({ error: () => {} }); }
}

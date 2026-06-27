// SPDX-License-Identifier: GPL-3.0-or-later

import { Injectable } from '@angular/core';
import { BehaviorSubject, Observable, tap } from 'rxjs';
import { ApiService } from './api.service';
import { WsService } from './ws.service';
import { StationConfig } from './models';

const DEFAULTS: StationConfig = {
  callsign: '', grid: '', psk_enabled: false,
  op_mode: 'normal', fd_class: '', fd_section: '',
  lotw_user: '', lotw_pass: '', qrz_api_key: '', qrz_user: '', qrz_pass: '',
  pota_user: '', pota_pass: '',
  lotw_upload: false, qrz_upload: false,
  tx_even: true, auto_cq: false, qso_retries: 5, max_cqs: 0, cq_skip: false, find_clear_freq_before_cq: false, tx_level: 50,
  swr_protect: true, swr_max: 2.5, auto_grid: false, units: 'km',
  wf_gain: 1, wf_palette: 'Classic', show_calls: true,
};

@Injectable({ providedIn: 'root' })
export class ConfigService {
  readonly config$ = new BehaviorSubject<StationConfig>(DEFAULTS);

  private gridLocked = false;
  lockGrid(locked: boolean): void { this.gridLocked = locked; }

  constructor(private api: ApiService, private ws: WsService) {
    this.api.getConfig().subscribe((c) => this.config$.next({ ...DEFAULTS, ...c }));

    this.ws.status$.subscribe((s) => {
      const g = s?.gps?.grid?.toUpperCase();
      const cur = this.config$.value;
      if (cur.auto_grid && !this.gridLocked && g && g !== cur.grid) {
        this.config$.next({ ...cur, grid: g });
      }
    });
  }

  save(cfg: Partial<StationConfig>): Observable<StationConfig> {
    return this.api.setConfig(cfg).pipe(
      tap((c) => {
        const merged = { ...DEFAULTS, ...c };

        if (this.gridLocked) merged.grid = this.config$.value.grid;
        this.config$.next(merged);
      }),
    );
  }

  savePartial(partial: Partial<StationConfig>): Observable<StationConfig> {
    return this.save(partial);
  }
}

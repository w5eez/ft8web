// SPDX-License-Identifier: GPL-3.0-or-later

import { Injectable } from '@angular/core';
import { Subject } from 'rxjs';
import { WsService } from './ws.service';
import { Decode } from './models';
import { gridFromMessage, gridToLatLon, senderOf } from './maidenhead';

export interface Spot extends Decode {
  t: number;
  sender: string;
  grid?: string;
  lat?: number;
  lon?: number;
  worked?: boolean;
  toks?: { t: string; c: string }[];
  newCall?: boolean;
  newSlot?: boolean;
  newGrid?: boolean;
}

@Injectable({ providedIn: 'root' })
export class DecodeStore {
  private spots: Spot[] = [];
  private readonly cap = 3000;

  readonly added$ = new Subject<Spot>();
  readonly reset$ = new Subject<void>();

  constructor(private ws: WsService) {
    this.ws.backlog$.subscribe((list) => {
      const sp = list.map((d) => this.toSpot(d)).reverse();
      const seen = new Set(sp.map(keyOf));
      this.spots = sp.concat(this.spots.filter((s) => !seen.has(keyOf(s)))).slice(0, this.cap);
      this.reset$.next();
    });
    this.ws.decode$.subscribe((d) => {
      const s = this.toSpot(d);
      this.spots.unshift(s);
      if (this.spots.length > this.cap) this.spots.pop();
      this.added$.next(s);
    });
  }

  list(): Spot[] { return this.spots; }

  clear(): void { this.spots = []; this.reset$.next(); }

  since(ms: number): Spot[] {
    if (ms <= 0) return this.spots;
    const cutoff = Date.now() - ms;
    return this.spots.filter((s) => s.t >= cutoff);
  }

  private toSpot(d: Decode): Spot {
    let sender: string;
    let grid: string | undefined;
    if (d.mode === 'WSPR') {

      const t = d.message.trim().split(/\s+/);
      sender = (t[0] ?? '').replace(/[<>]/g, '');
      grid = t[1] && /^[A-R]{2}[0-9]{2}([A-X]{2})?$/i.test(t[1]) ? t[1].toUpperCase() : undefined;
    } else {
      sender = senderOf(d.message);
      grid = gridFromMessage(d.message) ?? undefined;
    }
    const ll = grid ? gridToLatLon(grid) : null;
    return {
      ...d,
      t: timeFromUtc(d.utc),
      sender,
      grid,
      lat: ll ? ll[0] : undefined,
      lon: ll ? ll[1] : undefined,
    };
  }
}

function keyOf(s: Spot): string {
  return s.utc + '|' + s.freq + '|' + s.message;
}

function timeFromUtc(utc: string): number {
  if (!/^\d{6}$/.test(utc)) return Date.now();
  const now = new Date();
  const h = +utc.slice(0, 2), m = +utc.slice(2, 4), s = +utc.slice(4, 6);
  let t = Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), now.getUTCDate(), h, m, s);
  if (t - Date.now() > 60000) t -= 86400000;
  return t;
}

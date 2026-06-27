// SPDX-License-Identifier: GPL-3.0-or-later

import { Injectable } from '@angular/core';
import { Subject, BehaviorSubject } from 'rxjs';
import { Decode, Status, ModeInfo, QsoFrame, SpectrumHistoryRow } from './models';

@Injectable({ providedIn: 'root' })
export class WsService {
  private ws?: WebSocket;
  private reconnectAttempts = 0;
  private reconnectTimer?: ReturnType<typeof setTimeout>;
  private heartbeatTimer?: ReturnType<typeof setInterval>;

  readonly decode$          = new Subject<Decode>();
  readonly backlog$         = new Subject<Decode[]>();
  readonly spectrum$        = new Subject<Uint8Array>();
  readonly status$          = new BehaviorSubject<Status | null>(null);
  readonly modes$           = new BehaviorSubject<ModeInfo[]>([]);
  readonly connected$       = new BehaviorSubject<boolean>(false);
  readonly qso$             = new Subject<QsoFrame>();
  readonly spectrumHistory$ = new Subject<SpectrumHistoryRow[]>();
  readonly logsChanged$     = new Subject<void>();

  constructor() {
    this.connect();

    document.addEventListener('visibilitychange', () => {
      if (!document.hidden && this.ws?.readyState !== WebSocket.OPEN && !this.reconnectTimer) this.connect();
    });
  }

  private url(): string {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    return `${proto}://${location.host}/ws`;
  }

  private connect(): void {
    const ws = new WebSocket(this.url());
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => { this.reconnectAttempts = 0; this.connected$.next(true); this.startHeartbeat(); };
    ws.onerror = () => ws.close();
    ws.onclose = () => {
      if (this.ws !== ws) return;
      ws.onopen = ws.onerror = ws.onclose = ws.onmessage = null;
      this.stopHeartbeat();
      this.connected$.next(false);
      if (this.reconnectTimer) return;

      const delay = Math.min(5000, 1000 * 2 ** this.reconnectAttempts++);
      this.reconnectTimer = setTimeout(() => { this.reconnectTimer = undefined; this.connect(); }, delay);
    };
    ws.onmessage = (ev) => this.onMessage(ev);
    this.ws = ws;
  }

  private startHeartbeat(): void {
    this.stopHeartbeat();
    this.sendPing();
    this.heartbeatTimer = setInterval(() => this.sendPing(), 1000);
  }
  private stopHeartbeat(): void {
    if (this.heartbeatTimer) { clearInterval(this.heartbeatTimer); this.heartbeatTimer = undefined; }
  }
  private sendPing(): void {
    if (this.ws?.readyState === WebSocket.OPEN) {
      try { this.ws.send('{"type":"ping"}'); } catch {   }
    }
  }

  private onMessage(ev: MessageEvent): void {
    if (ev.data instanceof ArrayBuffer) {
      const bytes = new Uint8Array(ev.data);

      if (bytes[0] === 0x02) {
        const dv = new DataView(ev.data as ArrayBuffer);
        const nBins = dv.getUint16(1, true);
        const rows = dv.getUint16(3, true);
        const out: SpectrumHistoryRow[] = [];
        let off = 5;
        for (let i = 0; i < rows && off + 8 + nBins <= bytes.length; i++) {
          const t = Number(dv.getBigUint64(off, true)); off += 8;
          out.push({ t, bins: bytes.subarray(off, off + nBins) }); off += nBins;
        }
        this.spectrumHistory$.next(out);
        return;
      }
      if (bytes.length > 1 && bytes[0] === 0x01) {
        this.spectrum$.next(bytes.subarray(1));
      }
      return;
    }
    let msg: any;
    try { msg = JSON.parse(ev.data as string); } catch { return; }
    switch (msg.type) {
      case 'decode':  this.decode$.next(msg as Decode); break;
      case 'decodes': this.backlog$.next(msg.decodes as Decode[]); break;
      case 'status':  this.status$.next(msg as Status); break;
      case 'modes':   this.modes$.next(msg.modes as ModeInfo[]); break;
      case 'qso':     this.qso$.next(msg as QsoFrame); break;
      case 'logs_changed': this.logsChanged$.next(); break;
    }
  }
}

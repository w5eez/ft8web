// SPDX-License-Identifier: GPL-3.0-or-later

import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable, timeout } from 'rxjs';
import { FreqEntry, AudioDevice, StationConfig, PskReports, LogInfo, Qso, CallInfo, UploadStatus, PotaPark, PotaParkDetail, DiagStatus, Radio, HamlibRig, SerialPort, WorkedSets, QsoFrame, Decode } from './models';

interface FrequenciesResponse { frequencies: FreqEntry[]; modes: string[]; }
interface AudioResponse {
  running: boolean; device: string; level: number;
  captureVolume: number; hasGain: boolean; clip: boolean; devices: AudioDevice[];
}

@Injectable({ providedIn: 'root' })
export class ApiService {
  constructor(private http: HttpClient) {}

  setMode(mode: string): Observable<unknown> {
    return this.http.post('/api/mode', { mode });
  }

  tune(mode: string, band: string, region = 'ALL'): Observable<unknown> {
    return this.http.post('/api/tune', { mode, band, region });
  }

  setPower(watts: number): Observable<unknown> {
    return this.http.post('/api/rig/power', { watts });
  }

  clearTxInhibit(): Observable<unknown> {
    return this.http.post('/api/rig/tx-clear', {});
  }

  restart(): Observable<{ ok: boolean }> {
    return this.http.post<{ ok: boolean }>('/api/restart', {});
  }

  shutdown(): Observable<{ ok: boolean }> {
    return this.http.post<{ ok: boolean }>('/api/shutdown', {});
  }

  setTx(on: boolean): Observable<{ ok: boolean; error?: string; armed: boolean }> {
    return this.http.post<{ ok: boolean; error?: string; armed: boolean }>('/api/tx', { on });
  }

  setGain(pct: number): Observable<unknown> {
    return this.http.post('/api/audio', { gain: Math.round(pct) });
  }

  setDevice(device: string): Observable<unknown> {
    return this.http.post('/api/audio', { device });
  }

  setTxOffset(tx: number): Observable<unknown> {
    return this.http.post('/api/offsets', { tx: Math.round(tx) });
  }

  audio(): Observable<AudioResponse> {
    return this.http.get<AudioResponse>('/api/audio');
  }

  frequencies(mode: string): Observable<FrequenciesResponse> {
    return this.http.get<FrequenciesResponse>(`/api/frequencies?mode=${encodeURIComponent(mode)}`);
  }

  getConfig(): Observable<StationConfig> {
    return this.http.get<StationConfig>('/api/config');
  }

  setConfig(cfg: Partial<StationConfig>): Observable<StationConfig> {
    return this.http.post<StationConfig>('/api/config', cfg);
  }

  pskReports(): Observable<PskReports> {
    return this.http.get<PskReports>('/api/pskreporter/reports');
  }

  logs(): Observable<{ logs: LogInfo[]; counter: number }> {
    return this.http.get<{ logs: LogInfo[]; counter: number }>('/api/logs');
  }

  resetQsoCounter(): Observable<{ ok: boolean }> {
    return this.http.post<{ ok: boolean }>('/api/logs/counter/reset', {});
  }

  createLog(name: string, park: string, active = true): Observable<{ ok: boolean; logs: LogInfo[] }> {
    return this.http.post<{ ok: boolean; logs: LogInfo[] }>('/api/logs', { name, park, active });
  }

  deleteLog(file: string): Observable<{ ok: boolean }> {
    return this.http.delete<{ ok: boolean }>(`/api/logs/${encodeURIComponent(file)}`);
  }

  setLogMeta(file: string, meta: Partial<Pick<LogInfo, 'name' | 'park' | 'active' | 'archived'>>): Observable<{ ok: boolean }> {
    return this.http.post<{ ok: boolean }>(`/api/logs/${encodeURIComponent(file)}/meta`, meta);
  }

  logQsos(file: string): Observable<{ qsos: Qso[] }> {
    return this.http.get<{ qsos: Qso[] }>(`/api/logs/${encodeURIComponent(file)}/qsos`);
  }

  worked(): Observable<WorkedSets> { return this.http.get<WorkedSets>('/api/worked'); }
  syncWorked(): Observable<{ ok: boolean }> { return this.http.post<{ ok: boolean }>('/api/worked/sync', {}); }

  addQso(file: string, qso: Qso): Observable<{ ok: boolean }> {
    return this.http.post<{ ok: boolean }>(`/api/logs/${encodeURIComponent(file)}/qsos`, qso);
  }

  updateQso(file: string, idx: number, qso: Qso): Observable<{ ok: boolean }> {
    return this.http.post<{ ok: boolean }>(`/api/logs/${encodeURIComponent(file)}/qsos/${idx}`, qso);
  }

  deleteQso(file: string, idx: number): Observable<{ ok: boolean }> {
    return this.http.delete<{ ok: boolean }>(`/api/logs/${encodeURIComponent(file)}/qsos/${idx}`);
  }

  qsoState() { return this.http.get<QsoFrame>('/api/qso'); }
  qsoCall(d: { utc: string; snr: number; freq: number; mode: string; message: string }) {
    return this.http.post<{ ok: boolean }>('/api/qso/call', d);
  }
  qsoSet(p: { dx_call?: string; dx_grid?: string; report?: number }) {
    return this.http.post<{ ok: boolean }>('/api/qso/call', p);
  }
  qsoSelectTx(n: number) { return this.http.post<{ ok: boolean }>('/api/qso/tx', { n }); }
  qsoAbort() { return this.http.post<{ ok: boolean }>('/api/qso/abort', {}); }

  lookup(call: string): Observable<CallInfo> {
    return this.http.get<CallInfo>(`/api/lookup/${encodeURIComponent(call)}`);
  }

  uploads(): Observable<UploadStatus> {
    return this.http.get<UploadStatus>('/api/uploads');
  }

  diag(): Observable<DiagStatus> {
    return this.http.get<DiagStatus>('/api/diag');
  }

  decodes(): Observable<{ decodes: Decode[] }> {
    return this.http.get<{ decodes: Decode[] }>('/api/decodes');
  }

  getRadios(): Observable<{ radios: Radio[]; active: number }> {
    return this.http.get<{ radios: Radio[]; active: number }>('/api/radios');
  }
  saveRadios(body: { radios?: Radio[]; active?: number }): Observable<{ radios: Radio[]; active: number }> {
    return this.http.post<{ radios: Radio[]; active: number }>('/api/radios', body);
  }
  hamlibRigs(): Observable<{ rigs: HamlibRig[] }> {
    return this.http.get<{ rigs: HamlibRig[] }>('/api/hamlib/rigs');
  }
  serialPorts(): Observable<{ ports: SerialPort[] }> {
    return this.http.get<{ ports: SerialPort[] }>('/api/serial/ports');
  }

  potaParks(box?: { s: number; w: number; n: number; e: number }, center?: { lat: number; lon: number }):
      Observable<{ ok: boolean; error?: string; lat: number; lon: number; src?: string; parks: PotaPark[] }> {
    const q: string[] = [];
    if (box) q.push(`s=${box.s.toFixed(4)}`, `w=${box.w.toFixed(4)}`, `n=${box.n.toFixed(4)}`, `e=${box.e.toFixed(4)}`);
    if (center) q.push(`lat=${center.lat.toFixed(5)}`, `lon=${center.lon.toFixed(5)}`);
    const url = '/api/pota/parks' + (q.length ? '?' + q.join('&') : '');
    return this.http.get<{ ok: boolean; error?: string; lat: number; lon: number; src?: string; parks: PotaPark[] }>(url)
      .pipe(timeout({ first: 20000 }));
  }

  potaActivated(): Observable<{ ok: boolean; refs: string[] }> {
    return this.http.get<{ ok: boolean; refs: string[] }>('/api/pota/activated')
      .pipe(timeout({ first: 50000 }));
  }

  potaRefreshParks(): Observable<{ ok: boolean; error?: string; parks?: number }> {
    return this.http.post<{ ok: boolean; error?: string; parks?: number }>('/api/pota/refresh-parks', {})
      .pipe(timeout({ first: 130000 }));
  }

  potaSpot(reference: string, comments?: string):
      Observable<{ ok: boolean; error?: string; frequency: string; mode: string }> {
    return this.http.post<{ ok: boolean; error?: string; frequency: string; mode: string }>(
      '/api/pota/spot', { reference, comments }).pipe(timeout({ first: 20000 }));
  }

  potaPark(reference: string): Observable<PotaParkDetail> {
    return this.http.get<PotaParkDetail>('/api/pota/park/' + encodeURIComponent(reference))
      .pipe(timeout({ first: 20000 }));
  }

  potaUploadLog(file: string):
      Observable<{ ok: boolean; error?: string; uploaded?: number; message?: string }> {
    return this.http.post<{ ok: boolean; error?: string; uploaded?: number; message?: string }>(
      `/api/logs/${encodeURIComponent(file)}/pota-upload`, {}).pipe(timeout({ first: 100000 }));
  }
}

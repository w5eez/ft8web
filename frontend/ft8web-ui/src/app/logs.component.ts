// SPDX-License-Identifier: GPL-3.0-or-later

import { Component, OnInit, OnDestroy } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Subscription } from 'rxjs';
import { ApiService } from './api.service';
import { ConfigService } from './config.service';
import { WsService } from './ws.service';
import { gridDistanceKm, formatDistance } from './maidenhead';
import { LogInfo, Qso, UploadStatus } from './models';

@Component({
  selector: 'app-logs',
  standalone: true,
  imports: [CommonModule, FormsModule],
  template: `
    <div class="logs">
      <div class="bar">
        <span class="title">Logbooks</span>
        <span class="qsocount" title="QSOs logged since last reset">
          <b>{{ counter }}</b> QSO{{ counter === 1 ? '' : 's' }}
          <button (click)="resetCounter()" aria-label="reset QSO counter" title="reset counter (e.g. new activation)">↺</button>
        </span>
        <span class="upstat" *ngIf="up && (up.lotw_enabled || up.qrz_enabled)"
              [title]="up.recent.join('\n')">
          uploads:
          <em *ngIf="up.lotw_enabled">LoTW</em><em *ngIf="up.qrz_enabled">QRZ</em>
          <b *ngIf="up.pending" class="pend">{{ up.pending }} pending</b>
          <b *ngIf="up.failed" class="fail">{{ up.failed }} failed</b>
          <b *ngIf="!up.pending && !up.failed" class="okk">✓ idle</b>
        </span>
        <button class="sweepbtn" *ngIf="sweepCount() > 0" (click)="archiveUploaded()"
                title="archive all logs fully uploaded to POTA">⤓ Archive uploaded ({{ sweepCount() }})</button>
        <button class="newbtn" (click)="showNew = !showNew">+ New log</button>
      </div>

      <div class="newform" *ngIf="showNew">
        <input type="text" placeholder="Name (e.g. Sept POTA trip)" [(ngModel)]="newName" />
        <input type="text" class="park" placeholder="POTA park (e.g. US-1234)" [(ngModel)]="newPark"
               autocapitalize="characters" spellcheck="false" />
        <button (click)="createLog()">Create</button>
        <div class="hint" *ngIf="isPota()">POTA mode: create one log per park you're activating and mark them all active — each QSO is written to every active log with its park reference.</div>
      </div>

      <div class="flash" *ngIf="flash" [class.err]="flashErr">{{ flash }}</div>

      <div class="cards" *ngIf="current.length">
        <div class="card" *ngFor="let l of current" [class.sel]="l.file === selected?.file"
             [class.activecard]="l.active" (click)="select(l)">
          <div class="cardtop">
            <span class="name">{{ l.name }}</span>
            <span class="park" *ngIf="l.park">{{ l.park }}</span>
          </div>
          <div class="cardmid">
            <span class="count">{{ l.qsos }} QSO{{ l.qsos === 1 ? '' : 's' }}</span>
            <span class="potabadge" *ngIf="l.park && l.qsos"
                  [class.done]="potaPending(l) === 0">
              {{ potaPending(l) === 0 ? '✓ on POTA' : potaPending(l) + ' to upload' }}
            </span>
            <span class="date">{{ l.created * 1000 | date:'MMM d, y' }}</span>
          </div>
          <div class="cardbtm" (click)="$event.stopPropagation()">
            <label class="switch" title="active logs receive every logged QSO">
              <input type="checkbox" [checked]="l.active" (change)="toggleActive(l)" />
              <span>active</span>
            </label>
            <span class="spacer"></span>
            <button class="iconbtn pota" *ngIf="l.park" [class.done]="potaPending(l) === 0"
                    [disabled]="uploadingFile === l.file || potaPending(l) === 0"
                    (click)="uploadToPota(l)"
                    [title]="potaPending(l) ? ('upload ' + potaPending(l) + ' new QSO(s) to POTA') : 'all QSOs uploaded to POTA'">
              {{ uploadingFile === l.file ? '…' : (potaPending(l) ? 'POTA ⤴' : '✓ POTA') }}
            </button>
            <a class="iconbtn dl" [href]="dlUrl(l.file)" download
               aria-label="download ADIF" title="download ADIF (.adi)">ADI</a>
            <a class="iconbtn dl" [href]="cbrUrl(l.file)" download
               aria-label="download Cabrillo" title="download Cabrillo (.cbr)">CBR</a>
            <button class="iconbtn" (click)="archive(l)" aria-label="archive log" title="archive (hide from current list)">⤓</button>
            <button class="iconbtn danger" (click)="removeLog(l)" aria-label="delete log" title="delete log">✕</button>
          </div>
        </div>
      </div>
      <div class="empty" *ngIf="!current.length && !archivedLogs.length">No logs yet — create one to start logging QSOs.</div>
      <div class="empty" *ngIf="!current.length && archivedLogs.length">No current logs — your archived logs are tucked below.</div>

      <div class="archbox" *ngIf="archivedLogs.length">
        <button class="archhead" (click)="archivedOpen = !archivedOpen">
          <span class="chev">{{ archivedOpen ? '▾' : '▸' }}</span>
          <span class="albl">Archived</span>
          <span class="asum">{{ archivedCount }} park{{ archivedCount === 1 ? '' : 's' }} · {{ archivedQsos }} QSO{{ archivedQsos === 1 ? '' : 's' }}</span>
        </button>
        <div class="archbody" *ngIf="archivedOpen">
          <input class="archfilter" type="text" placeholder="filter archived (park / name)…"
                 [(ngModel)]="archiveFilter" spellcheck="false" />
          <ng-container *ngFor="let g of archivedGroups(); trackBy: trackMonth">
            <div class="archmonth">{{ g.month }}</div>
            <div class="archrow" *ngFor="let l of g.logs; trackBy: trackFile" [class.sel]="l.file === selected?.file">
              <span class="apark" *ngIf="l.park">{{ l.park }}</span>
              <span class="aname">{{ l.name }}</span>
              <span class="acount">{{ l.qsos }} Q</span>
              <span class="apota" *ngIf="l.park && l.qsos && potaPending(l) === 0" title="all QSOs on POTA">✓</span>
              <span class="adate">{{ l.created * 1000 | date:'MMM d' }}</span>
              <span class="spacer"></span>
              <button class="iconbtn" (click)="select(l)" title="view QSOs">view</button>
              <button class="iconbtn" (click)="unarchive(l)" title="unarchive — back to current logs">↩</button>
              <button class="iconbtn" (click)="reactivate(l)" title="unarchive and set active">★</button>
              <a class="iconbtn dl" [href]="dlUrl(l.file)" download title="download ADIF (.adi)">ADI</a>
              <button class="iconbtn danger" (click)="removeLog(l)" title="delete log">✕</button>
            </div>
          </ng-container>
          <div class="archempty" *ngIf="!archivedGroups().length">no archived logs match the filter</div>
        </div>
      </div>

      <div class="activity" *ngIf="up && (up.lotw_enabled || up.qrz_enabled || up.recent.length)">
        <div class="acthead">Auto-logging activity</div>
        <div class="actlines" *ngIf="up.recent.length; else noact">
          <div class="actline" *ngFor="let line of up.recent"
               [class.ok]="line.includes('✓')" [class.err]="line.includes('✗')">{{ line }}</div>
        </div>
        <ng-template #noact><div class="actline dim">no activity yet</div></ng-template>
      </div>

      <div class="detail" *ngIf="selected">
        <div class="dethead">
          <span class="title">{{ selected.name }}</span>
          <span class="park" *ngIf="selected.park">{{ selected.park }}</span>
          <span class="spacer"></span>
          <button class="newbtn" (click)="startAdd()" *ngIf="addIdx === null">+ Add QSO</button>
        </div>

        <div class="qsowrap">
        <table>
          <thead>
            <tr><th>Date</th><th>UTC</th><th>Call</th><th>Band</th><th>Mode</th><th>Sent</th><th>Rcvd</th><th>Grid</th><th>Dist</th><th></th></tr>
          </thead>
          <tbody>
            <tr class="editrow" *ngIf="addIdx === -1">
              <td><input [(ngModel)]="edit['qso_date']" size="8" /></td>
              <td><input [(ngModel)]="edit['time_on']" size="6" /></td>
              <td><input [(ngModel)]="edit['call']" size="8" class="up" /></td>
              <td><input [(ngModel)]="edit['band']" size="4" /></td>
              <td><input [(ngModel)]="edit['mode']" size="4" class="up" /></td>
              <td><input [(ngModel)]="edit['rst_sent']" size="3" /></td>
              <td><input [(ngModel)]="edit['rst_rcvd']" size="3" /></td>
              <td><input [(ngModel)]="edit['gridsquare']" size="5" class="up" /></td>
              <td></td>
              <td class="acts">
                <button class="iconbtn ok" (click)="saveAdd()">✓</button>
                <button class="iconbtn" (click)="cancelEdit()">✕</button>
              </td>
            </tr>
            <ng-container *ngFor="let q of qsos; let i = index">
              <tr *ngIf="editIdx !== i" (click)="startEdit(i)" title="click to edit">
                <td>{{ q['qso_date'] }}</td>
                <td>{{ q['time_on'] }}</td>
                <td class="call">{{ q['call'] }}</td>
                <td>{{ q['band'] }}</td>
                <td>{{ q['submode'] || q['mode'] }}</td>
                <td>{{ q['rst_sent'] }}</td>
                <td>{{ q['rst_rcvd'] }}</td>
                <td>{{ q['gridsquare'] }}</td>
                <td class="dist">{{ qsoDist(q) }}</td>
                <td class="acts">
                  <button class="iconbtn danger" (click)="removeQso(i); $event.stopPropagation()">✕</button>
                </td>
              </tr>
              <tr class="editrow" *ngIf="editIdx === i">
                <td><input [(ngModel)]="edit['qso_date']" size="8" /></td>
                <td><input [(ngModel)]="edit['time_on']" size="6" /></td>
                <td><input [(ngModel)]="edit['call']" size="8" class="up" /></td>
                <td><input [(ngModel)]="edit['band']" size="4" /></td>
                <td><input [(ngModel)]="edit['mode']" size="4" class="up" /></td>
                <td><input [(ngModel)]="edit['rst_sent']" size="3" /></td>
                <td><input [(ngModel)]="edit['rst_rcvd']" size="3" /></td>
                <td><input [(ngModel)]="edit['gridsquare']" size="5" class="up" /></td>
                <td></td>
                <td class="acts">
                  <button class="iconbtn ok" (click)="saveEdit(i)">✓</button>
                  <button class="iconbtn" (click)="cancelEdit()">✕</button>
                </td>
              </tr>
            </ng-container>
            <tr *ngIf="!qsos.length && addIdx === null">
              <td colspan="10" class="emptycell">No QSOs in this log yet.</td>
            </tr>
          </tbody>
        </table>
        </div>
      </div>
    </div>`,
  styles: [`
    .logs { display:flex; flex-direction:column; gap:14px; }
    .bar { display:flex; align-items:center; gap:14px; }
    .title { font:600 15px system-ui, sans-serif; color:#e6edf3; }
    .qsocount { display:flex; align-items:center; gap:6px; background:#13202c; border:1px solid #1c2a38;
                border-radius:6px; padding:4px 10px; font:12px ui-monospace, monospace; color:#9fb0bd; }
    .qsocount b { font-size:16px; color:#7fd1ff; }
    .qsocount button { background:transparent; border:0; color:#8aa3b3; cursor:pointer; font-size:14px;
                       padding:0 2px; }
    .qsocount button:hover { color:#f0883e; }
    .upstat { display:flex; align-items:center; gap:6px; font:11px ui-monospace, monospace; color:#8aa3b3; }
    .upstat em { font-style:normal; background:#13202c; border-radius:3px; padding:1px 6px; color:#9fe3c0; }
    .upstat b { font-weight:600; }
    .upstat .pend { color:#ffd166; }
    .upstat .fail { color:#f85149; }
    .upstat .okk { color:#3fb950; }
    .newbtn { margin-left:auto; background:#1f6feb; color:#fff; border:0; border-radius:5px;
              padding:7px 14px; font:600 12px system-ui, sans-serif; cursor:pointer; }
    .newbtn:hover { background:#2f80ed; }
    .dethead .newbtn { margin-left:0; }
    .newform { display:flex; gap:8px; flex-wrap:wrap; align-items:center; padding:12px;
               background:#0b1118; border:1px solid #1c2530; border-radius:8px; }
    .newform input { background:#13202c; color:#cdd9e3; border:1px solid #1c2a38; border-radius:4px;
                     padding:7px; font:13px ui-monospace, monospace; flex:1; min-width:160px; }
    .newform .park { text-transform:uppercase; max-width:180px; }
    .newform button { background:#1f6feb; color:#fff; border:0; border-radius:5px; padding:8px 16px;
                      font:600 12px system-ui, sans-serif; cursor:pointer; }
    .newform .hint { flex-basis:100%; font-size:11px; color:#8aa3b3; }
    .cards { display:grid; grid-template-columns:repeat(auto-fill, minmax(230px, 1fr)); gap:12px; }
    .card { background:#0b1118; border:1px solid #1c2530; border-radius:10px; padding:12px;
            cursor:pointer; display:flex; flex-direction:column; gap:8px; }
    .card:hover { border-color:#2f80ed; }
    .card.sel { border-color:#1f6feb; box-shadow:0 0 0 1px #1f6feb; }
    .card.activecard { background:linear-gradient(180deg, #0c1a14 0%, #0b1118 60%); }
    .cardtop { display:flex; align-items:center; gap:8px; }
    .name { font:600 14px system-ui, sans-serif; color:#e6edf3; overflow:hidden; text-overflow:ellipsis; }
    .park { font:600 10px ui-monospace, monospace; color:#9fe3c0; background:#13202c;
            padding:2px 7px; border-radius:4px; white-space:nowrap; }
    .cardmid { display:flex; justify-content:space-between; font:12px ui-monospace, monospace; color:#8aa3b3; }
    .cardbtm { display:flex; align-items:center; flex-wrap:wrap; gap:8px; }
    .switch { display:flex; align-items:center; gap:5px; font:11px system-ui, sans-serif;
              color:#9fb0bd; cursor:pointer; }
    .switch input { accent-color:#3fb950; }
    .spacer { flex:1; }
    .iconbtn { display:inline-flex; align-items:center; justify-content:center; width:26px; height:26px;
               background:#13202c; color:#9fb0bd; border:1px solid #1c2a38; border-radius:5px;
               cursor:pointer; text-decoration:none; font-size:13px; }
    .iconbtn:hover { border-color:#2f80ed; color:#e6edf3; }
    .iconbtn.dl { width:auto; padding:0 8px; font:600 10px ui-monospace, monospace; text-decoration:none; }
    .iconbtn.danger:hover { border-color:#f85149; color:#f85149; }
    .iconbtn.ok { color:#3fb950; }
    .iconbtn.pota { width:auto; padding:0 9px; font:600 11px system-ui, sans-serif; white-space:nowrap; }
    .iconbtn.pota:not(:disabled):hover { border-color:#3fb950; color:#7fe3a0; }
    .iconbtn.pota:disabled { opacity:.55; cursor:default; }
    .iconbtn.pota.done { color:#7fe3a0; border-color:#23502f; opacity:.8; }
    .potabadge { font:600 10px system-ui, sans-serif; color:#e3b341; background:#241c0a;
                 border:1px solid #3a2f12; border-radius:10px; padding:1px 8px; }
    .potabadge.done { color:#7fe3a0; background:#0c2a16; border-color:#23502f; }
    .flash { font:600 12px system-ui, sans-serif; color:#7fe3a0; background:#0c2a16;
             border:1px solid #23502f; border-radius:8px; padding:8px 12px; }
    .flash.err { color:#ffb4ab; background:#2a1113; border-color:#5a2326; }
    .empty { padding:34px; text-align:center; color:#6b7a86; background:#0b1118;
             border:1px dashed #1c2530; border-radius:10px; }
    .detail { background:#0b1118; border:1px solid #1c2530; border-radius:10px; padding:12px; }
    .activity { background:#0b1118; border:1px solid #1c2530; border-radius:10px; padding:12px; order:99; }
    .acthead { font:600 12px system-ui, sans-serif; color:#8aa3b3; text-transform:uppercase;
               letter-spacing:.6px; margin-bottom:8px; }
    .actlines { max-height:180px; overflow-y:auto; display:flex; flex-direction:column; gap:2px; }
    .actline { font:12px ui-monospace, monospace; color:#9fb0bd; white-space:pre-wrap; }
    .actline.ok { color:#3fb950; }
    .actline.err { color:#f85149; }
    .actline.dim { color:#54626e; font-style:italic; }
    .dethead { display:flex; align-items:center; gap:10px; margin-bottom:10px; }
    .qsowrap { max-height:65vh; overflow-y:auto; }
    table { width:100%; border-collapse:collapse; font:13px ui-monospace, monospace; }
    thead th { text-align:left; color:#8aa3b3; padding:4px 8px; font-weight:600;
               border-bottom:1px solid #1c2530; position:sticky; top:0; background:#0b1118; z-index:1; }
    td { padding:4px 8px; border-bottom:1px solid #121a23; white-space:nowrap; }
    tbody tr { cursor:pointer; }
    tbody tr:hover { background:#0e1620; }
    td.call { color:#7fd1ff; font-weight:600; }
    td.acts { text-align:right; }
    .editrow input { background:#13202c; color:#cdd9e3; border:1px solid #2f80ed; border-radius:3px;
                     padding:3px 4px; font:12px ui-monospace, monospace; width:100%; box-sizing:border-box; }
    .editrow .up { text-transform:uppercase; }
    .dist { color:#8aa3b3; white-space:nowrap; }
    .emptycell { text-align:center; color:#6b7a86; padding:18px; }
    .sweepbtn { margin-left:auto; background:#241c0a; color:#e3b341; border:1px solid #3a2f12; border-radius:5px;
                padding:7px 12px; font:600 11px system-ui, sans-serif; cursor:pointer; white-space:nowrap; }
    .sweepbtn:hover { border-color:#e3b341; color:#f0c869; }
    .sweepbtn + .newbtn { margin-left:8px; }
    .archbox { order:100; background:#0b1118; border:1px solid #1c2530; border-radius:10px; overflow:hidden; }
    .archhead { display:flex; align-items:center; gap:10px; width:100%; background:transparent; border:0;
                color:#9fb0bd; cursor:pointer; padding:11px 14px; font:600 12px system-ui, sans-serif; text-align:left; }
    .archhead:hover { background:#0e1620; }
    .archhead .chev { color:#6b7a86; width:12px; }
    .archhead .albl { color:#cdd9e3; text-transform:uppercase; letter-spacing:.6px; }
    .archhead .asum { color:#6b7a86; font:11px ui-monospace, monospace; }
    .archbody { border-top:1px solid #1c2530; padding:10px 14px 12px; display:flex; flex-direction:column; gap:3px; }
    .archfilter { background:#13202c; color:#cdd9e3; border:1px solid #1c2a38; border-radius:5px;
                  padding:6px 9px; font:12px ui-monospace, monospace; margin-bottom:6px; max-width:280px; }
    .archmonth { color:#6b7a86; font:600 10px system-ui, sans-serif; text-transform:uppercase;
                 letter-spacing:.6px; margin:8px 0 2px; }
    .archrow { display:flex; align-items:center; gap:8px; padding:4px 6px; border-radius:6px;
               font:12px ui-monospace, monospace; color:#9fb0bd; }
    .archrow:hover { background:#0e1620; }
    .archrow.sel { background:#0e1a24; box-shadow:inset 2px 0 0 #1f6feb; }
    .archrow .apark { color:#9fe3c0; background:#13202c; padding:1px 6px; border-radius:4px; font-weight:600;
                      font-size:10px; white-space:nowrap; }
    .archrow .aname { color:#cdd9e3; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; max-width:240px; }
    .archrow .acount { color:#8aa3b3; white-space:nowrap; }
    .archrow .apota { color:#7fe3a0; }
    .archrow .adate { color:#6b7a86; white-space:nowrap; }
    .archrow .iconbtn { width:auto; min-width:26px; padding:0 7px; height:24px; }
    .archempty { color:#6b7a86; font-style:italic; padding:8px 4px; }
    @media (max-width: 700px) {
      table { font-size:11px; }
      td, thead th { padding:3px 4px; }
    }
  `],
})
export class LogsComponent implements OnInit, OnDestroy {
  up: UploadStatus | null = null;
  counter = 0;
  logs: LogInfo[] = [];
  selected: LogInfo | null = null;
  qsos: Qso[] = [];
  showNew = false;
  newName = '';
  newPark = '';
  editIdx: number | null = null;
  addIdx: number | null = null;
  edit: Qso = {};
  uploadingFile: string | null = null;
  flash = '';
  flashErr = false;
  archivedOpen = false;
  archiveFilter = '';
  private flashTimer?: ReturnType<typeof setTimeout>;

  constructor(private api: ApiService, private config: ConfigService, private ws: WsService) {}

  private timers: ReturnType<typeof setInterval>[] = [];
  private sub?: Subscription;
  private cfgSub?: Subscription;
  private myGrid = '';
  private units = 'km';

  qsoDist(q: Qso): string {
    const theirs = (q['gridsquare'] || '').trim();
    const mine = (q['my_gridsquare'] || this.myGrid || '').trim();
    if (!theirs || !mine) return '';
    const km = gridDistanceKm(mine, theirs);
    return km === null ? '' : formatDistance(km, this.units);
  }

  ngOnInit(): void {
    this.cfgSub = this.config.config$.subscribe((c) => { this.myGrid = c.grid; this.units = c.units; });
    this.reload();
    const pollUploads = () => this.api.uploads().subscribe((u) => (this.up = u));
    pollUploads();
    this.timers.push(setInterval(pollUploads, 30_000));

    this.timers.push(setInterval(() => {
      if (this.editIdx === null && this.addIdx === null) this.reload();
    }, 30_000));

    this.sub = this.ws.logsChanged$.subscribe(() => {
      if (this.editIdx === null && this.addIdx === null) this.reload();
    });
  }

  ngOnDestroy(): void {
    this.timers.forEach((t) => clearInterval(t));
    this.sub?.unsubscribe();
    this.cfgSub?.unsubscribe();
    if (this.flashTimer) clearTimeout(this.flashTimer);
  }

  dlUrl(file: string): string { return '/api/logs/' + encodeURIComponent(file) + '/adi'; }
  cbrUrl(file: string): string { return '/api/logs/' + encodeURIComponent(file) + '/cbr'; }

  potaPending(l: LogInfo): number { return Math.max(0, l.qsos - (l.pota_uploaded || 0)); }

  get current(): LogInfo[] { return this.logs.filter((l) => !l.archived); }
  get archivedLogs(): LogInfo[] { return this.logs.filter((l) => l.archived); }
  get archivedCount(): number { return this.archivedLogs.length; }
  get archivedQsos(): number { return this.archivedLogs.reduce((s, l) => s + l.qsos, 0); }

  sweepable(): LogInfo[] {
    return this.current.filter((l) => l.park && l.qsos > 0 && this.potaPending(l) === 0);
  }
  sweepCount(): number { return this.sweepable().length; }

  archiveUploaded(): void {
    const ls = this.sweepable();
    if (!ls.length) return;
    let remaining = ls.length;
    const done = () => { if (--remaining === 0) this.reload(); };
    ls.forEach((l) => this.api.setLogMeta(l.file, { archived: true }).subscribe({ next: done, error: done }));
    this.setFlash(`Archived ${ls.length} uploaded log${ls.length === 1 ? '' : 's'}`, false);
  }

  archive(l: LogInfo): void {
    this.api.setLogMeta(l.file, { archived: true }).subscribe(() => this.reload());
  }
  unarchive(l: LogInfo): void {
    this.api.setLogMeta(l.file, { archived: false }).subscribe(() => this.reload());
  }
  reactivate(l: LogInfo): void {
    this.api.setLogMeta(l.file, { archived: false, active: true }).subscribe(() => this.reload());
  }

  archivedGroups(): { month: string; logs: LogInfo[] }[] {
    const f = this.archiveFilter.trim().toUpperCase();
    const ls = this.archivedLogs
      .filter((l) => !f || (l.name + ' ' + l.park).toUpperCase().includes(f))
      .sort((a, b) => b.created - a.created);
    const groups: { month: string; logs: LogInfo[] }[] = [];
    for (const l of ls) {
      const m = new Date(l.created * 1000).toLocaleDateString(undefined, { year: 'numeric', month: 'long' });
      let g = groups.find((x) => x.month === m);
      if (!g) { g = { month: m, logs: [] }; groups.push(g); }
      g.logs.push(l);
    }
    return groups;
  }

  trackMonth(_: number, g: { month: string }): string { return g.month; }
  trackFile(_: number, l: LogInfo): string { return l.file; }

  uploadToPota(l: LogInfo): void {
    if (this.uploadingFile) return;
    this.uploadingFile = l.file;
    this.api.potaUploadLog(l.file).subscribe({
      next: (r) => {
        this.uploadingFile = null;
        if (r.ok) {
          const n = r.uploaded || 0;
          this.setFlash(n > 0 ? `Uploaded ${n} QSO${n === 1 ? '' : 's'} to POTA ✓`
                              : (r.message || 'Nothing new to upload'), false);
          this.reload();
        } else {
          this.setFlash(`POTA upload failed: ${r.error || 'unknown error'}`, true);
        }
      },
      error: () => { this.uploadingFile = null; this.setFlash('POTA upload failed (network)', true); },
    });
  }

  private setFlash(msg: string, err: boolean): void {
    this.flash = msg;
    this.flashErr = err;
    if (this.flashTimer) clearTimeout(this.flashTimer);
    this.flashTimer = setTimeout(() => (this.flash = ''), 6000);
  }

  isPota(): boolean { return this.config.config$.value.op_mode === 'pota'; }

  resetCounter(): void {
    this.api.resetQsoCounter().subscribe(() => (this.counter = 0));
  }

  reload(keepSelection = true): void {
    this.api.logs().subscribe((r) => {
      this.logs = r.logs;
      this.counter = r.counter ?? 0;
      if (keepSelection && this.selected) {
        this.selected = this.logs.find((l) => l.file === this.selected!.file) ?? null;
        if (!this.selected) this.qsos = [];
      }
    });
  }

  createLog(): void {
    if (!this.newName.trim() && !this.newPark.trim()) return;
    this.api.createLog(this.newName.trim(), this.newPark.trim().toUpperCase()).subscribe(() => {
      this.newName = '';
      this.newPark = '';
      this.showNew = false;
      this.reload();
    });
  }

  select(l: LogInfo): void {
    this.selected = l;
    this.editIdx = this.addIdx = null;
    this.api.logQsos(l.file).subscribe((r) => (this.qsos = r.qsos));
  }

  toggleActive(l: LogInfo): void {
    this.api.setLogMeta(l.file, { active: !l.active }).subscribe(() => this.reload());
  }

  removeLog(l: LogInfo): void {
    if (!confirm(`Delete log "${l.name}" (${l.qsos} QSOs)? The log files on disk will be removed!`)) return;
    this.api.deleteLog(l.file).subscribe(() => {
      if (this.selected?.file === l.file) { this.selected = null; this.qsos = []; }
      this.reload(false);
    });
  }

  startAdd(): void {
    const now = new Date();
    const p = (n: number, w = 2) => String(n).padStart(w, '0');
    this.edit = {
      qso_date: `${now.getUTCFullYear()}${p(now.getUTCMonth() + 1)}${p(now.getUTCDate())}`,
      time_on: `${p(now.getUTCHours())}${p(now.getUTCMinutes())}${p(now.getUTCSeconds())}`,
      call: '', band: '20m', mode: 'FT8', rst_sent: '', rst_rcvd: '', gridsquare: '',
    };
    this.addIdx = -1;
    this.editIdx = null;
  }

  saveAdd(): void {
    if (!this.selected || !this.edit['call']?.trim()) return;
    this.api.addQso(this.selected.file, this.clean(this.edit)).subscribe(() => {
      this.addIdx = null;
      this.select(this.selected!);
      this.reload();
    });
  }

  startEdit(i: number): void {
    this.editIdx = i;
    this.addIdx = null;
    this.edit = { ...this.qsos[i] };
  }

  saveEdit(i: number): void {
    if (!this.selected) return;
    this.api.updateQso(this.selected.file, i, this.clean(this.edit)).subscribe(() => {
      this.editIdx = null;
      this.select(this.selected!);
    });
  }

  cancelEdit(): void { this.editIdx = this.addIdx = null; }

  removeQso(i: number): void {
    if (!this.selected) return;
    if (!confirm(`Delete QSO with ${this.qsos[i]['call'] ?? '?'}?`)) return;
    this.api.deleteQso(this.selected.file, i).subscribe(() => {
      this.select(this.selected!);
      this.reload();
    });
  }

  private clean(q: Qso): Qso {
    const out: Qso = {};
    for (const [k, v] of Object.entries(q)) {
      const val = (v ?? '').trim();
      if (!val) continue;
      out[k] = ['call', 'mode', 'gridsquare'].includes(k) ? val.toUpperCase() : val;
    }
    return out;
  }
}

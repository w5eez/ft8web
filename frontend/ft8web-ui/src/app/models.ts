// SPDX-License-Identifier: GPL-3.0-or-later

export interface Decode {
  utc: string;
  snr: number;
  dt: number;
  freq: number;
  mode: string;
  band: string;
  message: string;
}

export interface RigStatus {
  ok: boolean;
  freq_hz?: number;
  mode?: string;
  passband?: number;
  ptt?: boolean;
  strength_db?: number;
  power_w?: number;
  power_max_w?: number;
  swr?: number;
  alc?: number;
}

export interface AudioStatus {
  running: boolean;
  level: number;
  device: string;
  captureVolume: number;
  hasGain: boolean;
  clip: boolean;
  silent?: boolean;
  error?: string;
}

export interface Status {
  type: 'status';
  rig: RigStatus;
  audio: AudioStatus;
  mode: string;
  period: number;
  rx: number;
  tx: number;
  tx_inhibit: boolean;
  swr_trip: number;
  tx_armed: boolean;
  tx_active: boolean;
  tx_message?: string;
  hb_age_ms?: number;
  gps?: GpsStatus;
  clock_synced?: boolean;
  t_ms: number;
}

export interface PotaPark {
  reference: string;
  name: string;
  lat: number;
  lon: number;
  dist_km: number;
}

export interface PotaParkDetail {
  ok: boolean;
  reference: string;
  name: string;
  grid: string;
  location: string;
  location_desc: string;
  parktype: string;
  website: string;
  lat: number;
  lon: number;
  activations: number;
  attempts: number;
  qsos: number;
}

export interface GpsStatus {
  fix: boolean;
  grid: string;
  lat: number;
  lon: number;
  auto: boolean;
}

export interface ModeInfo {
  name: string;
  period: number;
}

export interface FreqEntry {
  hz: number;
  mode: string;
  band: string;
  region: string;
}

export interface AudioDevice {
  id: string;
  name: string;
}

export interface StationConfig {
  callsign: string;
  grid: string;
  psk_enabled: boolean;
  op_mode: 'normal' | 'pota' | 'fd';
  fd_class: string;
  fd_section: string;
  lotw_user: string;
  lotw_pass: string;
  qrz_api_key: string;
  qrz_user: string;
  qrz_pass: string;
  pota_user: string;
  pota_pass: string;
  lotw_pass_set?: boolean;
  qrz_api_key_set?: boolean;
  qrz_pass_set?: boolean;
  pota_pass_set?: boolean;
  lotw_upload: boolean;
  qrz_upload: boolean;
  tx_even: boolean;
  auto_cq: boolean;
  qso_retries: number;
  max_cqs: number;
  cq_skip: boolean;
  find_clear_freq_before_cq: boolean;
  tx_level: number;
  swr_protect: boolean;
  swr_max: number;
  units: string;
  auto_grid: boolean;
  show_calls: boolean;
  wf_gain: number;
  wf_palette: string;
}

export interface CallInfo {
  ok: boolean;
  error?: string;
  call?: string;
  fname?: string;
  name?: string;
  addr1?: string;
  addr2?: string;
  state?: string;
  zip?: string;
  country?: string;
  grid?: string;
  image?: string;
}

export interface UploadStatus {
  lotw_enabled: boolean;
  qrz_enabled: boolean;
  pending: number;
  failed: number;
  done: number;
  recent: string[];
}

export interface RxReport {
  receiver: string;
  locator: string;
  freq: number;
  mode: string;
  snr: number;
  t: number;
}

export interface Radio {
  name: string;
  model: number;
  serial: string;
  baud: number;
  device: string;
  power_max_w: number;
}

export interface HamlibRig { model: number; mfg: string; name: string; status: string; }
export interface SerialPort { path: string; name: string; }

export interface DiagStatus {
  cpu_temp_c?: number;
  load1?: number; load5?: number; load15?: number;
  mem_total_mb?: number; mem_avail_mb?: number;
  disk_free_mb?: number;
  sys_uptime_s?: number; proc_uptime_s?: number;
  rig_connected: boolean; rig_freq_hz: number; tx_inhibit: boolean;
  audio_running: boolean; audio_device: string; audio_level: number;
  audio_clipping: boolean; audio_xruns: number;
  mode: string;
  decode_recent: { mode: string; wall_s: number; count: number }[];
  decode_avg_s?: number; decode_max_s?: number;
  up_pending: number; up_failed: number; up_done: number;
  ws_clients: number;
  gps_fix: boolean; gps_grid: string;
}

export interface LogInfo {
  file: string;
  name: string;
  park: string;
  active: boolean;
  created: number;
  qsos: number;
  pota_uploaded?: number;
}

export type Qso = Record<string, string>;

export interface QsoParity { home: boolean; effective: boolean; flipped: boolean; }
export interface QsoFrame {
  type: 'qso'; seq: number;
  dx_call: string; dx_grid: string; report: number; rcvd_report: string;
  selected_tx: number; messages: string[];
  complete: boolean; attempts: number; retries_max: number;
  parity: QsoParity; last_event: '' | 'completed' | 'aborted';
}
export interface SpectrumHistoryRow { t: number; bins: Uint8Array; }

export interface PskReports {
  enabled: boolean;
  callsign: string;
  last_query: number;
  now: number;
  next_query: number;
  next_upload: number;
  reports: RxReport[];
}

export interface WorkedSets {
  qsos: number;
  synced: number;
  syncing: boolean;
  have_key: boolean;
  error: string;
  calls: string[];
  slots: string[];
  grids: string[];
}

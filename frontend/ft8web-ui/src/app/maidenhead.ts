// SPDX-License-Identifier: GPL-3.0-or-later

export function gridToLatLon(grid: string): [number, number] | null {
  const g = grid.trim().toUpperCase();
  if (!/^[A-R]{2}[0-9]{2}([A-X]{2})?$/.test(g)) return null;

  let lon = (g.charCodeAt(0) - 65) * 20 - 180;
  let lat = (g.charCodeAt(1) - 65) * 10 - 90;
  lon += (g.charCodeAt(2) - 48) * 2;
  lat += (g.charCodeAt(3) - 48) * 1;

  if (g.length === 6) {
    lon += (g.charCodeAt(4) - 65) * (2 / 24);
    lat += (g.charCodeAt(5) - 65) * (1 / 24);
    lon += (2 / 24) / 2;
    lat += (1 / 24) / 2;
  } else {
    lon += 1;
    lat += 0.5;
  }
  return [lat, lon];
}

export function gridFromMessage(msg: string): string | null {
  const toks = msg.trim().split(/\s+/);
  if (!toks.length) return null;
  const last = toks[toks.length - 1].toUpperCase();
  if (last === 'RR73') return null;
  return /^[A-R]{2}[0-9]{2}$/.test(last) ? last : null;
}

export function senderOf(msg: string): string {
  const t = msg.trim().split(/\s+/);
  if (!t.length) return '';

  const isCall = (s: string) =>
    /\d/.test(s) && /[A-Z]/i.test(s) &&
    !/^[A-R]{2}[0-9]{2}([A-X]{2})?$/i.test(s) &&
    !/^R?[+-]?\d+$/.test(s);
  if (t[0].toUpperCase() === 'CQ') {

    for (let i = 1; i < t.length; i++) if (isCall(t[i])) return t[i].toUpperCase();
    return '';
  }
  return (t.length >= 2 ? t[1] : t[0]).toUpperCase();
}

export function gridDistanceKm(a: string, b: string): number | null {
  const p = gridToLatLon(a), q = gridToLatLon(b);
  if (!p || !q) return null;
  const rad = Math.PI / 180;
  const dLat = (q[0] - p[0]) * rad, dLon = (q[1] - p[1]) * rad;
  const s = Math.sin(dLat / 2) ** 2 +
            Math.cos(p[0] * rad) * Math.cos(q[0] * rad) * Math.sin(dLon / 2) ** 2;
  return 12742 * Math.asin(Math.sqrt(s));
}

export function formatDistance(km: number, units: string): string {
  const mi = units === 'mi';
  return `${Math.round(mi ? km * 0.621371 : km).toLocaleString()} ${mi ? 'mi' : 'km'}`;
}

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate frontend public/map-extras.json for the MAPS tab.

Inputs (vendor/mapdata/, downloaded from Natural Earth / PublicaMundi):
  - ne_50m_admin_1_states_provinces_lines.geojson  -> state/province border lines
  - ../frontend/ft8web-ui/public/world.geo.json    -> country label points
  - us-states.json                                  -> US state label points

Output is pre-projected into the map's 360x180 viewBox (x = lon+180, y = 90-lat):
  { "borders": [svg path strings], "countries": [{n,x,y}], "states": [{n,x,y}] }
"""
import json
import os

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAPDATA = os.path.join(BASE, "vendor", "mapdata")
PUBLIC = os.path.join(BASE, "frontend", "ft8web-ui", "public")

STATE_CODES = {
    "Alabama": "AL", "Alaska": "AK", "Arizona": "AZ", "Arkansas": "AR", "California": "CA",
    "Colorado": "CO", "Connecticut": "CT", "Delaware": "DE", "Florida": "FL", "Georgia": "GA",
    "Hawaii": "HI", "Idaho": "ID", "Illinois": "IL", "Indiana": "IN", "Iowa": "IA",
    "Kansas": "KS", "Kentucky": "KY", "Louisiana": "LA", "Maine": "ME", "Maryland": "MD",
    "Massachusetts": "MA", "Michigan": "MI", "Minnesota": "MN", "Mississippi": "MS",
    "Missouri": "MO", "Montana": "MT", "Nebraska": "NE", "Nevada": "NV", "New Hampshire": "NH",
    "New Jersey": "NJ", "New Mexico": "NM", "New York": "NY", "North Carolina": "NC",
    "North Dakota": "ND", "Ohio": "OH", "Oklahoma": "OK", "Oregon": "OR", "Pennsylvania": "PA",
    "Rhode Island": "RI", "South Carolina": "SC", "South Dakota": "SD", "Tennessee": "TN",
    "Texas": "TX", "Utah": "UT", "Vermont": "VT", "Virginia": "VA", "Washington": "WA",
    "West Virginia": "WV", "Wisconsin": "WI", "Wyoming": "WY",
    "District of Columbia": "DC", "Puerto Rico": "PR",
}

def project(lon, lat):
    return round(lon + 180, 2), round(90 - lat, 2)

def line_paths(geom):
    coords = geom["coordinates"]
    lines = [coords] if geom["type"] == "LineString" else coords
    out = []
    for line in lines:
        d = []
        for i, (lon, lat) in enumerate(line):
            x, y = project(lon, lat)
            d.append(("M" if i == 0 else "L") + f"{x},{y}")
        if len(d) > 1:
            out.append(" ".join(d))
    return out

def rings_of(geom):
    if geom["type"] == "Polygon":
        return [geom["coordinates"][0]]
    if geom["type"] == "MultiPolygon":
        return [poly[0] for poly in geom["coordinates"]]
    return []

def label_point(geom):
    """bbox centre of the largest outer ring (by bbox area)."""
    best, best_area = None, -1.0
    for ring in rings_of(geom):
        xs = [p[0] for p in ring]
        ys = [p[1] for p in ring]
        area = (max(xs) - min(xs)) * (max(ys) - min(ys))
        if area > best_area:
            best_area = area
            best = ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2)
    if best is None:
        return None
    x, y = project(best[0], best[1])
    return x, y

def main():
    borders = []
    lines = json.load(open(os.path.join(MAPDATA, "ne_50m_admin_1_states_provinces_lines.geojson"),
                           encoding="utf-8"))
    for f in lines["features"]:
        if f.get("geometry"):
            borders.extend(line_paths(f["geometry"]))

    countries = []
    world = json.load(open(os.path.join(PUBLIC, "world.geo.json"), encoding="utf-8"))
    for f in world["features"]:
        name = (f.get("properties") or {}).get("name")
        if not name or not f.get("geometry"):
            continue
        pt = label_point(f["geometry"])
        if pt:
            countries.append({"n": name, "x": pt[0], "y": pt[1]})

    states = []
    us = json.load(open(os.path.join(MAPDATA, "us-states.json"), encoding="utf-8"))
    for f in us["features"]:
        name = (f.get("properties") or {}).get("name")
        code = STATE_CODES.get(name or "")
        if not code or not f.get("geometry"):
            continue
        pt = label_point(f["geometry"])
        if pt:
            states.append({"n": code, "x": pt[0], "y": pt[1]})

    out = {"borders": borders, "countries": countries, "states": states}
    path = os.path.join(PUBLIC, "map-extras.json")
    json.dump(out, open(path, "w"), separators=(",", ":"))
    print(f"wrote {path}: {os.path.getsize(path)} bytes | "
          f"{len(borders)} border paths, {len(countries)} country labels, {len(states)} state labels")

if __name__ == "__main__":
    main()

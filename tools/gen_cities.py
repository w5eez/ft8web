#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate frontend public/cities.json for map hover labels ("near City, ST").

Source: GeoNames cities15000 (all cities with population >= 15k, CC BY 4.0).
Cached in vendor/mapdata/cities15000.zip; re-run to regenerate the JSON.

Output: compact array of [name, lat, lon, countryCode, usStateCode]
(usStateCode only for US entries; lat/lon rounded to 2 decimals).
"""
import io
import json
import os
import urllib.request
import zipfile

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(BASE, "vendor", "mapdata", "cities15000.zip")
OUT = os.path.join(BASE, "frontend", "ft8web-ui", "public", "cities.json")
URL = "https://download.geonames.org/export/dump/cities15000.zip"

def main():
    if not os.path.exists(CACHE):
        print("downloading", URL)
        urllib.request.urlretrieve(URL, CACHE)
    txt = zipfile.ZipFile(CACHE).read("cities15000.txt").decode("utf-8")

    out = []
    for line in txt.splitlines():
        f = line.split("\t")
        if len(f) < 15:
            continue
        name, lat, lon, cc, admin1 = f[1], float(f[4]), float(f[5]), f[8], f[10]
        out.append([name, round(lat, 2), round(lon, 2), cc, admin1 if cc == "US" else ""])

    json.dump(out, open(OUT, "w"), ensure_ascii=False, separators=(",", ":"))
    print(f"wrote {OUT}: {os.path.getsize(OUT)} bytes, {len(out)} cities")

if __name__ == "__main__":
    main()

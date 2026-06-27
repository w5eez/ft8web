#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate backend/src/freq_plan_data.hpp from WSJT-X Improved's default
frequency table (models/FrequencyList.cpp :: default_frequency_list).

Keeps ft8web's band/mode defaults faithful to the vendored WSJT-X Improved
source (including FT2). Re-run after updating the vendored wsjtx tree.
"""
import os
import re
import collections

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(BASE, "vendor", "wsjtx-3.1.0", "src", "wsjtx", "models", "FrequencyList.cpp")
OUT = os.path.join(BASE, "backend", "src", "freq_plan_data.hpp")

ENTRY = re.compile(r"\{\s*(\d+)\s*,\s*Modes::(\w+)\s*,\s*IARURegions::(\w+)")

KEEP_MODES = {"FT8", "FT4", "FT2", "WSPR"}

# FT2 overrides to match IU8LMC Decodium 4.0 (where the activity is). Keyed by
# WSJT-X (hz, region) -> Decodium (hz, region). 60m 5.360 deliberately NOT used
# (not a legal US 60m channel; we keep WSJT-X's US-legal 5.357).
FT2_OVERRIDES = {
    ("3578000", "R3"):   ("3568000", "ALL"),
    ("7052000", "ALL"):  ("7062000", "ALL"),
    ("50320000", "ALL"): ("50316000", "ALL"),
}

# Alternate/DXpedition FT8 channels dropped to keep ONE canonical FT8 freq per band.
FT8_DROP = {
    "3567000",
    "7056000",
    "10131000",
    "14090000",
    "18095000",
    "21091000",
    "24911000",
    "28091000",
    "50323000",
}

def main() -> None:
    src = open(SRC, encoding="latin-1").read()
    entries = ENTRY.findall(src)
    kept = []
    for f, m, r in entries:
        if m not in KEEP_MODES:
            continue
        if m == "FT8" and f in FT8_DROP:
            continue
        if m == "FT2" and (f, r) in FT2_OVERRIDES:
            f, r = FT2_OVERRIDES[(f, r)]
        kept.append((f, m, r))
    lines = ['    {%s, "%s", "%s"},' % (f, m, r) for f, m, r in kept]
    hpp = (
        "// SPDX-License-Identifier: GPL-3.0-or-later\n"
        "#pragma once\n"
        "namespace ft8web {\n"
        "struct FreqEntry { long long hz; const char* mode; const char* region; };\n"
        "static const FreqEntry kDefaultFrequencies[] = {\n"
        + "\n".join(lines) + "\n};\n}\n"
    )
    open(OUT, "w").write(hpp)
    by_mode = collections.Counter(m for _, m, _ in kept)
    print(f"wrote {OUT}: {len(kept)} entries "
          f"({len(entries) - len(kept)} dropped)")
    print("by mode:", dict(sorted(by_mode.items())))

if __name__ == "__main__":
    main()

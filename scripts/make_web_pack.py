"""Build the data pack a browser loads on demand.

    python scripts/make_web_pack.py [--tier lite] [--resources resources] [--out build/web]

The library reads a directory with `places.tsv` and, instead of `streets.tsv`,
`streets.manifest.tsv` and `streets/<CC>.tsv`; a country's file is read on the
first call that needs it. This script writes that layout from a tier whose
streets are one file, so the pages fetch the core files at start-up and a
country's street file only when someone asks for that country:

- `<out>/core/`: places, formats, postcodes, street names and types, and
  `streets.manifest.tsv` (country_code, rows, hash);
- `<out>/core/streets/<CC>.tsv`: one file per country with real streets;
- `<out>/files.json`: the core file names and each country's file with its
  size, which the page reads to fetch and place the files.

The street files are the tier's rows split by country, so the data is the
same as the compiled-in copy of that tier.
"""

import argparse
import json
import shutil
from pathlib import Path

from assemble import content_hash, street_lines

CORE = ["places.tsv", "formats.tsv", "postcodes.tsv", "street_names.tsv", "street_types.tsv"]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--tier", default="lite")
    ap.add_argument("--resources", default="resources")
    ap.add_argument("--out", default="build/web")
    a = ap.parse_args()

    src = Path(a.resources) / a.tier
    core = Path(a.out) / "core"
    if core.exists():
        shutil.rmtree(core)
    (core / "streets").mkdir(parents=True)
    for name in CORE:
        if (src / name).exists():
            shutil.copy2(src / name, core / name)

    lines = street_lines(src / "streets.tsv")
    header = next(lines)
    code_col = header.split("\t").index("country_code")
    by_country = {}
    for line in lines:
        by_country.setdefault(line.split("\t", code_col + 1)[code_col], []).append(line)

    manifest = ["country_code\trows\thash\n"]
    streets = {}
    for code in sorted(by_country):
        target = core / "streets" / f"{code}.tsv"
        with open(target, "w", encoding="utf-8", newline="\n") as o:
            o.write(header + "\n")
            o.writelines(line + "\n" for line in by_country[code])
        manifest.append(f"{code}\t{len(by_country[code])}\t{content_hash(target):016x}\n")
        streets[code] = target.stat().st_size
    with open(core / "streets.manifest.tsv", "w", encoding="utf-8", newline="\n") as m:
        m.writelines(manifest)

    index = {"tier": a.tier, "core": [n for n in CORE if (core / n).exists()] + ["streets.manifest.tsv"],
             "streets": streets}
    with open(Path(a.out) / "files.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(index, f, separators=(",", ":"))
    total = sum(p.stat().st_size for p in core.rglob("*") if p.is_file())
    print(f"{a.tier}: {len(streets)} countries, {sum(map(len, by_country.values()))} street rows, "
          f"{total / 1e6:.1f} MB in {a.out}")


if __name__ == "__main__":
    main()

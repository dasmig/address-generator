"""Merge the per-country files of prepare_osm.py and prepare_postcodes.py.

    python scripts/assemble.py --streets build/osm --postcodes build/postcodes --out resources

Writes, for the tiers `lite` and `full`:

- `<out>/lite/streets.tsv`: the countries' street files joined under one
  header (the whole tier is compiled in or read at once);
- `<out>/full/streets/<CC>.tsv`, one file per country, and
  `<out>/full/streets.manifest.tsv` (country_code, rows, hash), which the
  library reads at load; a country's file is read on the first call that
  needs it. `hash` is the file's content hash as the library computes it
  (core::tsv_document::content_hash());
- `<out>/<tier>/postcodes.tsv`, the countries' files joined.
"""

import argparse
from pathlib import Path

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK = (1 << 64) - 1


def content_hash(path):
    """The hash tsv_document computes: FNV-1a over the cells of every row,
    tab-separated and newline-terminated (line ends and BOM do not count)."""
    h = FNV_OFFSET
    with open(path, "rb") as f:
        for line in f:
            for b in line.rstrip(b"\r\n") + b"\n":
                h = ((h ^ b) * FNV_PRIME) & MASK
    return h


def join(src, tier, dest):
    files = sorted(Path(src).glob(f"*.{tier}.tsv"))
    if not files:
        return 0
    n = 0
    with open(dest, "w", encoding="utf-8", newline="\n") as out:
        for i, f in enumerate(files):
            with open(f, encoding="utf-8", newline="") as inp:
                header = inp.readline()
                if i == 0:
                    out.write(header)
                for line in inp:
                    out.write(line)
                    n += 1
    return n


def split_full(src, dest):
    """Copy the countries' `full` street files to dest/streets/<CC>.tsv and
    write dest/streets.manifest.tsv. Returns the number of street rows."""
    files = sorted(Path(src).glob("*.full.tsv"))
    if not files:
        return 0
    out = Path(dest) / "streets"
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.tsv"):
        old.unlink()
    total = 0
    manifest = ["country_code\trows\thash\n"]
    for f in files:
        code = f.name.split(".")[0]
        target = out / f"{code}.tsv"
        n = 0
        with open(f, encoding="utf-8", newline="") as inp, open(target, "w", encoding="utf-8", newline="\n") as o:
            o.write(inp.readline().rstrip("\r\n") + "\n")
            for line in inp:
                o.write(line.rstrip("\r\n") + "\n")
                n += 1
        total += n
        manifest.append(f"{code}\t{n}\t{content_hash(target):016x}\n")
    with open(Path(dest) / "streets.manifest.tsv", "w", encoding="utf-8", newline="\n") as m:
        m.writelines(manifest)
    legacy = Path(dest) / "streets.tsv"
    if legacy.exists():
        legacy.unlink()
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--streets", default="build/osm")
    ap.add_argument("--postcodes", default="build/postcodes")
    ap.add_argument("--out", default="resources")
    a = ap.parse_args()
    for tier in ("lite", "full"):
        d = Path(a.out) / tier
        d.mkdir(parents=True, exist_ok=True)
        if tier == "full":
            s = split_full(a.streets, d)
        else:
            s = join(a.streets, tier, d / "streets.tsv")
        p = join(a.postcodes, tier, d / "postcodes.tsv")
        print(f"{tier}: {s} street rows, {p} postcode rows")


if __name__ == "__main__":
    main()

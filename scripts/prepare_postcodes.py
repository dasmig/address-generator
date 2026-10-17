"""Build the postcode tables from GeoNames postal codes.

    python scripts/prepare_postcodes.py allCountries.txt \
        --cities ../city-generator/resources/full/cities.tsv \
        --formats resources/lite/formats.tsv --regex tests/data/postcode_regex.tsv \
        --out build/postcodes

allCountries.txt is https://download.geonames.org/export/zip/allCountries.zip
(CC BY 4.0). Output: <out>/<CC>.full.tsv and <out>/<CC>.lite.tsv with

  country_code, geonameid, postcode, weight, partial

  geonameid  the place of city-generator's `full` tier the postcode belongs to
             (the place nearest to the postcode's coordinates, with distance
             scaled by the square root of the population, as prepare_osm.py)
  weight     1 (uniform) for now: GeoNames has no address counts
  partial    1 when GeoNames gives only the first part of the postcode (CA,
             GB, NL, IE); the rest is drawn from the country's zip_completion

`lite` keeps the places of cities15000 (population 15,000 or more), at most
LITE_PER_PLACE postcodes each; `full` keeps every postcode. A country whose
GeoNames postcodes are cut short for copyright and have no completion (AR, BR,
CL, CN, MT) is left out: its postcodes come from the pattern.
"""

import argparse
import collections
import csv
import random
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare_osm as po  # noqa: E402

LITE_PER_PLACE = 25
LITE_MIN_POPULATION = 15_000
TRUNCATED_NO_COMPLETION = {"AR", "BR", "CL", "CN", "MT"}
PARTIAL = {"CA", "GB", "NL", "IE", "GG", "JE", "IM"}
LETTERS = "ABCEGHJKLMNPRTVXY"
UK_INWARD = "ABDEFGHJLNPQRSTUWXYZ"


def complete(prefix, completion, rng):
    out = [prefix]
    for c in completion:
        if c == "#": out.append(str(rng.randrange(10)))
        elif c == "N": out.append(str(1 + rng.randrange(9)))
        elif c == "A": out.append(chr(65 + rng.randrange(26)))
        elif c == "L": out.append(LETTERS[rng.randrange(len(LETTERS))])
        elif c == "U": out.append(UK_INWARD[rng.randrange(len(UK_INWARD))])
        elif c == "_": out.append(" ")
        else: out.append(c)
    return "".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("zipfile")
    ap.add_argument("--cities", required=True)
    ap.add_argument("--formats", required=True)
    ap.add_argument("--regex", required=True)
    ap.add_argument("--out", default="build/postcodes")
    ap.add_argument("--only", help="comma-separated country codes (default all)")
    a = ap.parse_args()

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    only = set(a.only.split(",")) if a.only else None

    completions = {}
    with open(a.formats, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            completions[row["country_code"]] = row["zip_completion"]
    regexes = {}
    with open(a.regex, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            regexes[row["country_code"]] = re.compile(row["regex"])

    by_country = collections.defaultdict(list)
    with open(a.zipfile, encoding="utf-8", newline="") as f:
        for r in csv.reader(f, delimiter="\t", quoting=csv.QUOTE_NONE):
            cc = r[0]
            if only and cc not in only:
                continue
            if cc in TRUNCATED_NO_COMPLETION:
                continue
            by_country[cc].append((r[1], float(r[9]), float(r[10])))

    rng = random.Random(1)
    total = {"full": 0, "lite": 0}
    for cc in sorted(by_country):
        places = po.read_places(a.cities, cc)
        if not places:
            print(f"{cc}: no places in city-generator", file=sys.stderr)
            continue
        index = po.Index(places)
        partial = cc in PARTIAL
        rx = regexes.get(cc)
        if partial and not completions.get(cc):
            print(f"{cc}: partial postcodes without a completion, skipped", file=sys.stderr)
            continue
        bad = 0
        per_place = collections.defaultdict(set)
        for code, lat, lon in by_country[cc]:
            code = code.strip()
            if not code:
                continue
            if partial:
                if rx is not None and not rx.fullmatch(complete(code, completions[cc], rng)):
                    bad += 1
                    continue
            elif rx is not None and not rx.fullmatch(code):
                # GeoNames writes some with a country prefix ("L-4968", "AZ 0100")
                stripped = re.sub(r"^[A-Z]{1,2}[ -]?(?=\d)", "", code)
                if not rx.fullmatch(stripped):
                    bad += 1
                    continue
                code = stripped
            i = index.nearest(lat, lon)
            if i is not None:
                per_place[i].add(code)
        if bad:
            print(f"{cc}: {bad} of {len(by_country[cc])} postcodes do not match the format, dropped",
                  file=sys.stderr)
        for tier in ("lite", "full"):
            rows = []
            for i, codes in per_place.items():
                p = places[i]
                if tier == "lite":
                    if p[5] < LITE_MIN_POPULATION:
                        continue
                    chosen = sorted(codes)[:LITE_PER_PLACE]
                else:
                    chosen = sorted(codes)
                for code in chosen:
                    rows.append((p[0], code))
            rows.sort()
            with open(out / f"{cc}.{tier}.tsv", "w", encoding="utf-8", newline="\n") as f:
                f.write("country_code\tgeonameid\tpostcode\tweight\tpartial\n")
                for gid, code in rows:
                    f.write(f"{cc}\t{gid}\t{code}\t1\t{1 if partial else 0}\n")
            total[tier] += len(rows)
    print(f"rows: lite {total['lite']}, full {total['full']}", file=sys.stderr)


if __name__ == "__main__":
    main()

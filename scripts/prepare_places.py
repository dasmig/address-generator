"""Build places.tsv (the places addresses are drawn in) from city-generator's data.

    python scripts/prepare_places.py --cities ../city-generator/resources \
        --out resources

Reads cities/lite/cities.tsv, full/cities.tsv and admin1CodesASCII.txt, all
GeoNames data, and writes <out>/lite/places.tsv and <out>/full/places.tsv:

  geonameid     key; the same ids as city-generator, streets.tsv and postcodes.tsv
  country_code  ISO 3166-1 alpha-2
  name          the place's name (UTF-8)
  region_code   GeoNames admin1 code (an abbreviation in the US, CA, AU ...)
  region        the admin1 name
  population    weights the place when a request does not name one (0 counts as 1)
"""

import argparse
import csv
from pathlib import Path


# Curated (rule 6): GeoNames numbers the first-level divisions of these
# countries; addresses write the abbreviation.
ABBREVIATIONS = {
    "CA": {"01": "AB", "02": "BC", "03": "MB", "04": "NB", "05": "NL", "07": "NS", "08": "ON",
           "09": "PE", "10": "QC", "11": "SK", "12": "YT", "13": "NT", "14": "NU"},
    "AU": {"01": "ACT", "02": "NSW", "03": "NT", "04": "QLD", "05": "SA", "06": "TAS", "07": "VIC", "08": "WA"},
    "BR": {"01": "AC", "02": "AL", "03": "AP", "04": "AM", "05": "BA", "06": "CE", "07": "DF", "08": "ES",
           "11": "MS", "13": "MA", "14": "MT", "15": "MG", "16": "PA", "17": "PB", "18": "PR", "20": "PI",
           "21": "RJ", "22": "RN", "23": "RS", "24": "RO", "25": "RR", "26": "SC", "27": "SP", "28": "SE",
           "29": "GO", "30": "PE", "31": "TO"},
}


def admin1_names(path):
    names = {}
    with open(path, encoding="utf-8", newline="") as f:
        for row in csv.reader(f, delimiter="\t", quoting=csv.QUOTE_NONE):
            if len(row) >= 2:
                names[row[0]] = row[1]
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cities", required=True, help="city-generator/resources")
    ap.add_argument("--out", default="resources")
    a = ap.parse_args()

    root = Path(a.cities)
    admin1 = admin1_names(root / "admin1CodesASCII.txt")
    for tier in ("lite", "full"):
        out = Path(a.out) / tier
        out.mkdir(parents=True, exist_ok=True)
        rows = []
        with open(root / tier / "cities.tsv", encoding="utf-8", newline="") as f:
            for r in csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE):
                cc, code = r["country_code"], r["admin1_code"]
                name = admin1.get(f"{cc}.{code}", "")
                code = ABBREVIATIONS.get(cc, {}).get(code, code)
                rows.append((int(r["geonameid"]), cc, r["name"], code,
                             name, int(r["population"] or 0)))
        rows.sort()
        with open(out / "places.tsv", "w", encoding="utf-8", newline="\n") as f:
            f.write("geonameid\tcountry_code\tname\tregion_code\tregion\tpopulation\n")
            for gid, cc, name, code, region, pop in rows:
                f.write(f"{gid}\t{cc}\t{name}\t{code}\t{region}\t{pop}\n")
        print(f"{tier}: {len(rows)} places")


if __name__ == "__main__":
    main()

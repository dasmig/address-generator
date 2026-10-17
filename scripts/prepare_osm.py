"""Build the street tables of one country from an OpenStreetMap extract.

For each place of city-generator's `full` tier (GeoNames cities500) in the
country, the streets with the most addresses are kept, up to K by the
place's population (see PLAN.md, "address-generator: design").

    pip install osmium
    python scripts/prepare_osm.py PT portugal-latest.osm.pbf \
        --cities ../city-generator/resources/full/cities.tsv --out build/osm

Input:  a Geofabrik country extract (https://download.geofabrik.de/).
Output: <out>/<CC>.full.tsv and <out>/<CC>.lite.tsv, and a statistics line.

Columns: country_code, geonameid, name, class, weight, min_number, max_number
  weight      the number of addresses on the street in the place (1 for a
              residential street with none)
  class       the most common `highway` value of the street's ways, empty if
              the street is known only from addresses
  min/max     the 5th and 95th percentile of the house numbers seen on the street
              (digits only, the first 200 seen), 0 if none

A street belongs to the place nearest to it (distance scaled by the square root of
the population, within MAX_KM), or to the place
its `addr:city` names when one of that name is within NAME_KM.
"""

import argparse
import collections
import csv
import math
import re
import sys
import unicodedata
from pathlib import Path


# (population upper bound exclusive, lite K, full K); see PLAN.md for the table.
CUTS = [
    (5_000, 0, 12),
    (15_000, 0, 24),
    (50_000, 10, 48),
    (250_000, 20, 100),
    (1_000_000, 40, 200),
    (float("inf"), 80, 400),
]
MIN_COUNT = {"lite": 5, "full": 2}

# highway values that carry homes; ways of other classes are not streets.
RESIDENTIAL = {
    "residential", "living_street", "unclassified", "tertiary", "secondary",
    "primary", "pedestrian", "service_street", "road",
}
NUMBER_SAMPLE = 200  # house numbers kept per street, for its range
MAX_KM = 8.0     # a street farther than this from every place is dropped
NAME_KM = 25.0   # an addr:city that names a place this close wins
GRID = 0.25      # degrees, for the nearest-place search


def fold(s):
    s = unicodedata.normalize("NFKD", s.casefold())
    return "".join(c for c in s if not unicodedata.combining(c)).strip()


def read_places(path, cc):
    places = []
    with open(path, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            if row["country_code"] != cc:
                continue
            places.append((int(row["geonameid"]), row["name"], row["asciiname"],
                           float(row["latitude"]), float(row["longitude"]),
                           int(row["population"] or 0)))
    return places


class Index:
    """Nearest place by great-circle distance, through a degree grid."""

    def __init__(self, places):
        self.places = places
        self.grid = collections.defaultdict(list)
        self.names = collections.defaultdict(list)
        for i, p in enumerate(places):
            self.grid[(int(p[3] // GRID), int(p[4] // GRID))].append(i)
            self.names[fold(p[1])].append(i)
            self.names[fold(p[2])].append(i)

    @staticmethod
    def km(lat1, lon1, lat2, lon2):
        x = math.radians(lon2 - lon1) * math.cos(math.radians((lat1 + lat2) / 2))
        y = math.radians(lat2 - lat1)
        return 6371.0 * math.hypot(x, y)

    def nearest(self, lat, lon, city=None):
        gy, gx = int(lat // GRID), int(lon // GRID)
        # The nearest place by distance scaled by the square root of its
        # population: plain nearest-centroid gave Lisbon 94 streets, because
        # the small places around it took the rest (tuned on Portugal: the
        # exponent 0.5 gave Lisbon 2,431 and Porto 1,560 streets).
        best, best_s = None, float("inf")
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                for i in self.grid.get((gy + dy, gx + dx), ()):
                    p = self.places[i]
                    d = self.km(lat, lon, p[3], p[4])
                    if d > MAX_KM:
                        continue
                    score = d / math.sqrt(max(p[5], 500))
                    if score < best_s:
                        best, best_s = i, score
        if city:
            for i in self.names.get(fold(city), ()):
                p = self.places[i]
                if self.km(lat, lon, p[3], p[4]) <= NAME_KM:
                    return i
        return best


class Collector:
    def __init__(self, index):
        self.index = index
        self.count = collections.Counter()           # (place, street) -> addresses
        self.classes = collections.defaultdict(collections.Counter)
        self.numbers = {}                            # (place, street) -> house numbers seen (first NUMBER_SAMPLE)
        self.seen = collections.Counter()
        self.pending = []                            # streets of ways, assigned later

    def address(self, tags, lat, lon):
        street = tags.get("addr:street")
        if not street or not tags.get("addr:housenumber"):
            return
        i = self.index.nearest(lat, lon, tags.get("addr:city") or tags.get("addr:place"))
        if i is None:
            self.seen["address outside every place"] += 1
            return
        key = (i, " ".join(street.split()))
        self.count[key] += 1
        m = re.match(r"\d{1,4}", tags["addr:housenumber"].strip())
        if m:
            seen = self.numbers.setdefault(key, [])
            if len(seen) < NUMBER_SAMPLE:
                seen.append(int(m.group()))
        self.seen["addresses"] += 1

    def way(self, tags, lat, lon):
        name = tags.get("name")
        cls = tags.get("highway")
        if not name or cls not in RESIDENTIAL:
            return
        i = self.index.nearest(lat, lon)
        if i is None:
            self.seen["street way outside every place"] += 1
            return
        key = (i, " ".join(name.split()))
        self.classes[key][cls] += 1
        self.seen["street ways"] += 1


def run(pbf, collector, index="flex_mem"):
    import osmium
    fp = (osmium.FileProcessor(pbf, osmium.osm.NODE | osmium.osm.WAY)
          .with_locations(index)
          .with_filter(osmium.filter.KeyFilter("addr:housenumber", "highway")))
    for o in fp:
        tags = o.tags
        if o.is_node():
            if not o.location.valid():
                continue
            if "addr:housenumber" in tags:
                collector.address(tags, o.location.lat, o.location.lon)
        else:
            nodes = o.nodes
            if len(nodes) == 0:
                continue
            try:
                loc = nodes[len(nodes) // 2].location
                if not loc.valid():
                    continue
            except osmium.InvalidLocationError:
                continue
            if "addr:housenumber" in tags:
                collector.address(tags, loc.lat, loc.lon)
            if "highway" in tags:
                collector.way(tags, loc.lat, loc.lon)


def number_range(seen):
    """The 5th and 95th percentile of the house numbers seen, so that one
    mistyped number does not stretch the street (0, 0 when none)."""
    if not seen:
        return 0, 0
    seen = sorted(seen)
    return seen[int(0.05 * (len(seen) - 1))], seen[int(math.ceil(0.95 * (len(seen) - 1)))]


def build(collector, places, cc, out):
    streets = collections.defaultdict(list)          # place index -> rows
    keys = set(collector.count) | set(collector.classes)
    for key in keys:
        i, name = key
        n = collector.count.get(key, 0)
        cls = collector.classes[key].most_common(1)[0][0] if key in collector.classes else ""
        has = 1 if n > 0 else 0
        if n == 0:
            n = 1                                    # a street with no addresses
        lo, hi = number_range(collector.numbers.get(key, []))
        streets[i].append((name, cls, n, lo, hi, has))

    stats = {}
    for tier, k_index in (("lite", 1), ("full", 2)):
        rows, kept_places = [], 0
        for i, lst in streets.items():
            p = places[i]
            k = next(c[k_index] for c in CUTS if p[5] < c[0])
            if k == 0:
                continue
            # Streets with addresses come first, by count; a street with fewer
            # than the minimum is dropped unless it has no addresses at all
            # (weight 1), which only fill the places left up to K.
            counted = sorted((s for s in lst if s[5] > 0 and s[2] >= MIN_COUNT[tier]),
                             key=lambda s: (-s[2], s[0]))[:k]
            fill = sorted((s for s in lst if s[5] == 0), key=lambda s: s[0])[:k - len(counted)]
            lst = counted + fill
            if lst:
                kept_places += 1
            for name, cls, n, lo, hi, _ in lst:
                rows.append((p[0], name, cls, n, lo, hi))
        rows.sort(key=lambda r: (r[0], -r[3], r[1]))
        path = out / f"{cc}.{tier}.tsv"
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("country_code\tgeonameid\tname\tclass\tweight\tmin_number\tmax_number\n")
            for gid, name, cls, n, lo, hi in rows:
                f.write(f"{cc}\t{gid}\t{name}\t{cls}\t{n}\t{lo}\t{hi}\n")
        size = path.stat().st_size
        eligible = sum(1 for p in places if next(c[k_index] for c in CUTS if p[5] < c[0]) > 0)
        stats[tier] = (len(rows), size, kept_places, eligible)
    return stats


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("country", help="ISO country code, e.g. PT")
    ap.add_argument("pbf", help="Geofabrik extract of that country")
    ap.add_argument("--cities", required=True, help="city-generator resources/full/cities.tsv")
    ap.add_argument("--out", default="build/osm")
    ap.add_argument("--index", default="flex_mem",
                    help="node location index; for extracts too big for memory use sparse_file_array,<path>")
    a = ap.parse_args()

    cc = a.country.upper()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    places = read_places(a.cities, cc)
    if not places:
        sys.exit(f"no places for {cc} in {a.cities}")
    print(f"{cc}: {len(places)} places", file=sys.stderr)

    collector = Collector(Index(places))
    run(a.pbf, collector, a.index)
    print("seen:", dict(collector.seen), file=sys.stderr)
    stats = build(collector, places, cc, out)
    for tier, (rows, size, kept, eligible) in stats.items():
        print(f"{cc} {tier}: {rows} rows, {size / 1e6:.2f} MB, "
              f"{kept} of {eligible} places have streets")


if __name__ == "__main__":
    main()

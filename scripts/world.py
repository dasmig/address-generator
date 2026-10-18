"""Plan and run the street builds for every country.

    python scripts/world.py plan                      # writes scripts/extracts.tsv
    python scripts/world.py run --jobs 6 --mem-gb 40  # downloads and builds what is missing

`plan` reads Geofabrik's index (https://download.geofabrik.de/index-v1-nogeom.json)
and chooses, for each country in city-generator's `full` places, the extract
that covers it: the country's own extract, a bundle that lists several
countries (the Gulf states, ...) or, for territories the index does not tag
with their code, the extract named in OVERRIDES. The result is
`scripts/extracts.tsv` (extract, url, size in bytes, countries), which is
checked in so that a build does not depend on the index.

`run` takes the extracts whose countries have no `<out>/<CC>.full.tsv` yet,
downloads each (resumable, with curl), runs prepare_osm.py on it, and deletes
the download. Several run at once while the estimated memory (about 2.2 times
the extract) stays under --mem-gb. The largest extracts use a disk-backed node
index. Progress goes to <out>/../world.log; a stopped run resumes where it was.
"""

import argparse
import csv
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

INDEX = "https://download.geofabrik.de/index-v1-nogeom.json"
ROOT = Path(__file__).resolve().parent.parent
EXTRACTS = ROOT / "scripts" / "extracts.tsv"

# Extract ids for territories the index does not tag with their ISO code (or
# tags wrongly), and the countries each extract covers beyond its own tag.
OVERRIDES = {
    "guadeloupe": ["GP"], "martinique": ["MQ"], "reunion": ["RE"], "mayotte": ["YT"],
    "guyane": ["GF"], "polynesie-francaise": ["PF"], "new-caledonia": ["NC"],
    "wallis-et-futuna": ["WF"], "tokelau": ["TK"], "pitcairn-islands": ["PN"],
    "hong-kong": ["HK"], "macau": ["MO"], "kosovo": ["XK"], "guernsey-jersey": ["GG", "JE"],
    "isle-of-man": ["IM"], "bermuda": ["BM"], "falklands": ["FK"],
    "christmas-island": ["CX"], "cocos-islands": ["CC"], "norfolk-island": ["NF"],
    "svalbard-janmayen": ["SJ"],
    "malaysia-singapore-brunei": ["MY", "SG", "BN"],
    "gcc-states": ["QA", "AE", "OM", "BH", "KW", "SA"],
}
# Index tags that are wrong for the extract they sit on.
WRONG_TAGS = {"polynesie-francaise": ["VU"], "tokelau": ["VU"], "wallis-et-futuna": ["VU"],
              "pitcairn-islands": ["MH"]}
# Larger than this, the node index lives on disk.
SPARSE_BYTES = 6 * 1024**3


def places_by_country(cities):
    out = {}
    with open(cities, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            out[row["country_code"]] = out.get(row["country_code"], 0) + int(row["population"] or 0)
    return out


def size_of(url):
    req = urllib.request.Request(url, method="HEAD")
    with urllib.request.urlopen(req, timeout=60) as r:
        return int(r.headers.get("Content-Length", 0))


def plan(args):
    pop = places_by_country(args.cities)
    with urllib.request.urlopen(INDEX, timeout=120) as r:
        feats = json.load(r)["features"]
    regions = {f["properties"]["id"]: f["properties"] for f in feats}
    continents = {i for i, p in regions.items() if not p.get("parent")}
    chosen = {}  # country -> region id
    for i, p in regions.items():
        if not p.get("urls", {}).get("pbf"):
            continue
        codes = OVERRIDES.get(i) or ([] if i in WRONG_TAGS else p.get("iso3166-1:alpha2") or [])
        country_level = p.get("parent") in continents or not p.get("parent")
        for c in codes:
            if c not in pop:
                continue
            # A country-level extract wins over a sub-region that repeats its tag.
            if c not in chosen or (country_level and regions[chosen[c]].get("parent") not in continents):
                chosen[c] = i
    by_extract = {}
    for c, i in chosen.items():
        by_extract.setdefault(i, []).append(c)
    rows = []
    for i, codes in sorted(by_extract.items()):
        url = regions[i]["urls"]["pbf"]
        rows.append((i, url, size_of(url), " ".join(sorted(codes))))
        print(f"{i}: {rows[-1][2] / 1e6:.0f} MB {' '.join(sorted(codes))}", file=sys.stderr)
    with open(EXTRACTS, "w", encoding="utf-8", newline="\n") as f:
        f.write("extract\turl\tbytes\tcountries\n")
        for r in rows:
            f.write("\t".join(map(str, r)) + "\n")
    missing = sorted(set(pop) - set(chosen))
    total = sum(r[2] for r in rows)
    print(f"{len(rows)} extracts, {total / 1e9:.1f} GB, {len(chosen)} of {len(pop)} countries; "
          f"no extract for {len(missing)}: {' '.join(missing)}", file=sys.stderr)


def read_plan():
    with open(EXTRACTS, encoding="utf-8", newline="") as f:
        return [dict(r, bytes=int(r["bytes"]), countries=r["countries"].split())
                for r in csv.DictReader(f, delimiter="\t")]


def run_one(job, a, log):
    ccs = ",".join(job["countries"])
    pbf = a.work / (job['extract'].replace('/', '_') + '.osm.pbf')
    t0 = time.time()
    log(f"start {job['extract']} ({job['bytes'] / 1e6:.0f} MB) {ccs}")
    dl = subprocess.run(["curl", "-sSL", "-C", "-", "--retry", "5", "-o", str(pbf), job["url"]],
                        capture_output=True, text=True)
    if dl.returncode != 0:
        log(f"FAILED download {job['extract']}: {dl.stderr.strip()[:200]}")
        return False
    cmd = [sys.executable, str(ROOT / "scripts" / "prepare_osm.py"), ccs, str(pbf),
           "--cities", str(a.cities), "--out", str(a.out)]
    if job["bytes"] > SPARSE_BYTES:
        cmd += ["--index", f"sparse_file_array,{a.work / (job['extract'].replace('/', '_') + '.idx')}"]
    res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    pbf.unlink(missing_ok=True)
    (a.work / (job["extract"].replace("/", "_") + ".idx")).unlink(missing_ok=True)
    if res.returncode != 0:
        log(f"FAILED build {job['extract']}: {res.stderr.strip()[-300:]}")
        return False
    stats = [l for l in (res.stdout + res.stderr).splitlines() if " full:" in l]
    log(f"done {job['extract']} in {(time.time() - t0) / 60:.1f} min; " + "; ".join(stats))
    return True


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    args.out.mkdir(parents=True, exist_ok=True)
    logfile = open(args.out.parent / "world.log", "a", encoding="utf-8", buffering=1)

    def log(msg):
        line = f"{time.strftime('%Y-%m-%d %H:%M:%S')} {msg}"
        print(line, flush=True)
        logfile.write(line + "\n")

    only = set(args.only.split(",")) if args.only else None
    todo = [j for j in read_plan()
            if (only is None or only & set(j["countries"]) or j["extract"] in only)
            and not all((args.out / f"{c}.full.tsv").exists() for c in j["countries"])]
    # Biggest first: they take longest, and the small ones fill the gaps.
    todo.sort(key=lambda j: -j["bytes"])
    log(f"{len(todo)} extracts to build, {sum(j['bytes'] for j in todo) / 1e9:.1f} GB")
    running, failed = [], []
    budget = args.mem_gb * 1024**3
    while todo or running:
        for proc, job, mem in list(running):
            if proc.poll() is not None:
                running.remove((proc, job, mem))
                (failed if proc.returncode else []).append(job["extract"])
        used = sum(m for _, _, m in running)
        started = False
        for job in list(todo):
            mem = int(job["bytes"] * 2.2) + 600 * 1024**2
            if len(running) >= args.jobs or (running and used + mem > budget):
                continue
            todo.remove(job)
            cmd = [sys.executable, str(Path(__file__)), "one", job["extract"], "--out", str(args.out),
                   "--work", str(args.work), "--cities", str(args.cities)]
            running.append((subprocess.Popen(cmd), job, mem))
            used += mem
            started = True
        if not started:
            time.sleep(10)
    log("finished" + (f"; failed: {' '.join(failed)}" if failed else ""))


def one(args):
    job = next(j for j in read_plan() if j["extract"] == args.extract)
    logfile = open(args.out.parent / "world.log", "a", encoding="utf-8", buffering=1)

    def log(msg):
        logfile.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {msg}\n")

    sys.exit(0 if run_one(job, args, log) else 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, fn in (("plan", plan), ("run", run), ("one", one)):
        p = sub.add_parser(name)
        p.set_defaults(fn=fn)
        p.add_argument("--cities", type=Path, default=ROOT.parent / "city-generator" / "resources" / "full" / "cities.tsv")
        if name != "plan":
            p.add_argument("--out", type=Path, default=ROOT / "build" / "osm")
            p.add_argument("--work", type=Path, default=ROOT / "build" / "pbf")
        if name == "run":
            p.add_argument("--jobs", type=int, default=4)
            p.add_argument("--mem-gb", type=float, default=40)
            p.add_argument("--only", help="comma separated country codes or extract ids")
        if name == "one":
            p.add_argument("extract")
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()

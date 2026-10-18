"""Write the coverage table of doc/data.md from the shipped data.

    python scripts/coverage.py            # prints the table
    python scripts/coverage.py --write    # replaces the table in doc/data.md

For each country with real streets: its places in `lite` and `full`, how many
of them have at least one street, the street rows of each tier, and the share
of the country's population (of its places) that lives in a place with streets
in `full`. The table sits between the markers <!-- coverage:begin --> and
<!-- coverage:end --> in doc/data.md.
"""

import argparse
import collections
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BEGIN, END = "<!-- coverage:begin -->", "<!-- coverage:end -->"


def tsv(path):
    with open(path, encoding="utf-8", newline="") as f:
        header = f.readline().rstrip("\r\n").split("\t")
        for line in f:
            yield dict(zip(header, line.rstrip("\r\n").split("\t")))


def table():
    places = {}
    for tier in ("lite", "full"):
        places[tier] = {r["geonameid"]: (r["country_code"], int(r["population"]))
                        for r in tsv(ROOT / "resources" / tier / "places.tsv")}
    stats = collections.defaultdict(lambda: collections.defaultdict(int))
    for tier in ("lite", "full"):
        for gid, (cc, pop) in places[tier].items():
            stats[cc][f"{tier}_places"] += 1
            stats[cc][f"{tier}_pop"] += pop
    with_streets = {"lite": collections.defaultdict(set), "full": collections.defaultdict(set)}
    rows = collections.Counter()
    for r in tsv(ROOT / "resources" / "lite" / "streets.tsv"):
        with_streets["lite"][r["country_code"]].add(r["geonameid"])
        rows["lite", r["country_code"]] += 1
    for f in sorted((ROOT / "resources" / "full" / "streets").glob("*.tsv")):
        for r in tsv(f):
            with_streets["full"][r["country_code"]].add(r["geonameid"])
            rows["full", r["country_code"]] += 1
    out = []
    for cc in sorted(set(with_streets["full"]) | set(with_streets["lite"])):
        s = stats[cc]
        covered_pop = sum(places["full"][g][1] for g in with_streets["full"][cc] if g in places["full"])
        share = covered_pop / s["full_pop"] if s["full_pop"] else 0
        out.append((cc, s["lite_places"], len(with_streets["lite"][cc]), rows["lite", cc],
                    s["full_places"], len(with_streets["full"][cc]), rows["full", cc], share))
    return out


def render(rows):
    lines = [f"{len(rows)} countries have real streets.", "",
             "| Country | `lite` places | with streets | rows | `full` places | with streets | rows | population in a place with streets |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for cc, lp, ls, lr, fp, fs, fr, share in rows:
        lines.append(f"| {cc} | {lp:,} | {ls:,} | {lr:,} | {fp:,} | {fs:,} | {fr:,} | {share:.0%} |")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", action="store_true", help="replace the table in doc/data.md")
    a = ap.parse_args()
    text = render(table())
    if not a.write:
        print(text)
        return
    path = ROOT / "doc" / "data.md"
    doc = path.read_text(encoding="utf-8")
    crlf = "\r\n" in doc
    doc = doc.replace("\r\n", "\n")
    if BEGIN in doc and END in doc:
        head, rest = doc.split(BEGIN, 1)
        tail = rest.split(END, 1)[1]
        doc = head + BEGIN + "\n" + text + "\n" + END + tail
    elif "COVERAGE_PLACEHOLDER" in doc:
        doc = doc.replace("COVERAGE_PLACEHOLDER", BEGIN + "\n" + text + "\n" + END)
    else:
        sys.exit("doc/data.md has neither the coverage markers nor the placeholder")
    path.write_text(doc.replace("\n", "\r\n") if crlf else doc, encoding="utf-8", newline="")


if __name__ == "__main__":
    main()

"""Build formats.tsv from the Google Address Data Service (libaddressinput).

    python scripts/prepare_formats.py [--cache build/formats] [--out resources]

Downloads data/<CC> for every territory the service lists (cached under
--cache), and writes one row per territory:

  country_code   ISO 3166-1 alpha-2
  lines          the local layout: lines joined with '|', fields {A} street
                 and number, {C} city, {S} region, {Z} postcode, {D}
                 dependent locality, {X} sorting code (name and organisation
                 lines are left out)
  lines_latin    the Latin layout for scripts that have one (lfmt), else empty
  require        the fields an address needs (A, C, S, Z, D, X)
  region_style   'code' when the service lists region abbreviations, else 'name'
  zip_pattern    how a postcode is written: '#' a digit, 'N' a digit 1-9, 'A' a
                 capital letter, 'L' one of the letters ABCEGHJKLMNPRTVXY
                 (accepted wherever a letter is restricted), anything else
                 literally; empty when the territory has no postcodes or the
                 pattern could not be derived
  zip_completion  for the territories where the postcode data holds only the
                 first part of a postcode, how the rest is written (same symbols, '_' a space;
                 'U' is a letter allowed in the second half of a UK postcode)
  zip_examples   the service's examples, '~' separated; used when no pattern
                 fits
  number_after   1 when the house number follows the street ("Hauptstrasse 5")
  number_sep     what joins street and number: space ("Hauptstrasse 5") or comma ("Rua X, 5")
  unit_style     the name of the country's unit convention (see UNIT_STYLES)
  number_style   empty for a plain number, or `blocks` (Japan: 3-12-5)
  unit_format    how a unit is written ({n} a number, {f} a floor, {k} a door,
                 {l} a letter, {a|b} a word); empty: no units
  unit_join      where it goes: comma, space, dash, slash or before
  unit_format_latin, unit_join_latin
                 the same in Latin script; empty when the same

The postcode pattern is derived from the first example and kept only when
2,000 postcodes drawn from it all match the service's regular expression,
so the regular expressions are test data and are not shipped.

The columns number_after, number_sep and the unit columns are curated (rule 6): the
service does not say where the house number goes. See doc/data.md.
"""

import argparse
import json
import random
import re
import sys
import urllib.request
from pathlib import Path

BASE = "https://chromium-i18n.appspot.com/ssl-address/data"

# Curated. The house number goes before the street in these territories
# (English-speaking countries, France and its overseas territories, and a
# few others); everywhere else it follows the street.
NUMBER_BEFORE = set("""
US CA GB IE AU NZ FR GP MQ GF RE YT PM BL MF NC PF WF LU SG HK MY PH TH IN
ZA IL KE NG GH UG TZ ZM JM BS BB TT PK LK BD NP
GG JE IM GI MT CY VA SM MC LI AD
""".split())
# Numbers joined to the street with a comma and a space ("Rua X, 12").
NUMBER_COMMA = set("ES PT BR AR CL CO MX PE UY PY VE EC BO CR GT HN NI PA SV DO CU MD RU UA BY".split())
# Territories whose lines have no street-and-number order of their own:
# Japan, Korea and China write large to small and the number is part of {A}.
BLOCK_ADDRESS = set("JP KR CN TW".split())

# Curated. The service's Japanese format leaves the municipality inside {A}; the
# data has it as the place, so it gets a line of its own. Japanese house
# numbers are blocks ("3-12-5": chome, ban, go).
LAYOUTS = {
    "JP": ("\u3012{Z}|{S} {C}|{A}", "{A}|{C}, {S} {Z}"),
}
NUMBER_STYLE = {"JP": "blocks"}

# Curated. How a unit (a flat in a building) is written, as
#   name: (format, join, format in Latin script, join in Latin script)
# Formats: {n} a number 1-40, {f} a floor 1-9, {k} a door 1-4, {l} a letter A-D,
# {a|b} one of the words; the rest literally. Joins: comma ("Street 12, Apt 5"),
# space ("Street 12 lgh 1102"), dash ("Street 12-5"), slash ("Street 12/5") or
# before ("Flat A, 5/F, 12 Street"). The Latin pair is empty when it is the same.
# The library draws {n} first and the rest in order, so the styles of 2.2 and
# earlier (apt, flat, ste, piso, ap_br, app_fr, whg) draw what they did.
# See doc/data.md: the common written form of each country, not checked
# against each postal operator's guide.
UNIT_STYLES = {
    "none": ("", "", "", ""),
    # English
    "apt": ("Apt {n}", "comma", "", ""),
    "flat": ("Flat {n}", "comma", "", ""),
    "ste": ("Suite {n}", "comma", "", ""),
    "flat_bd": ("Flat {f}{l}", "comma", "", ""),            # Bangladesh: Flat 5B
    # Romance languages
    "piso": ("{f}º {l}", "comma", "", ""),                  # Spain, Portugal, Argentina: 3º A
    "ap_br": ("apto {n}", "comma", "", ""),
    "andar": ("{f}º andar", "comma", "", ""),               # Lusophone Africa: 3º andar
    "app_fr": ("Appt {n}", "comma", "", ""),
    "int_it": ("int. {n}", "comma", "", ""),                # Italian interno
    "int_mx": ("Int. {n}", "space", "", ""),                # Mexico: Av. Reforma 123 Int. 5
    "apto": ("Apto. {n}", "comma", "", ""),
    "apto_floor": ("Apto. {f}0{k}", "comma", "", ""),       # Colombia, Uruguay: Apto. 501
    "depto": ("Depto. {f}0{k}", "comma", "", ""),           # Chile
    "dpto": ("Dpto. {f}0{k}", "comma", "", ""),             # Peru, Bolivia, Paraguay
    "ap_ro": ("ap. {n}", "comma", "", ""),                  # Romanian, Albanian
    # Germanic
    "whg": ("Whg. {n}", "comma", "", ""),
    "lgh": ("lgh 1{f}0{k}", "space", "", ""),               # Sweden's lägenhetsnummer
    "bolig": ("H0{f}0{k}", "space", "", ""),                # Norway's bruksenhetsnummer
    "dk": ("{f}. {th|tv|mf}", "comma", "", ""),             # Denmark: 2. th.
    "is": ("íb. {f}0{k}", "comma", "", ""),                 # Iceland
    "dash": ("{n}", "dash", "", ""),                        # Netherlands, the Baltics: 12-5
    # Finnish
    "fi": ("{l} {n}", "space", "", ""),                     # staircase and flat: 12 A 5
    # Slavic and Hungarian
    "slash": ("{n}", "slash", "", ""),                      # Poland: 12/5
    "byt": ("byt {n}", "comma", "", ""),                    # Czechia, Slovakia
    "stanovanje": ("stanovanje {n}", "comma", "", ""),      # Slovenia
    "stan": ("stan {n}", "comma", "", ""),                  # Croatia, Bosnia, Montenegro
    "stan_cyr": ("стан {n}", "comma", "stan {n}", ""),      # Serbia, North Macedonia
    "ap_bg": ("ап. {n}", "comma", "ap. {n}", ""),
    "kv": ("кв. {n}", "comma", "kv. {n}", ""),              # Russian квартира
    "kv_latin": ("kv. {n}", "comma", "", ""),               # Uzbekistan, Turkmenistan
    "em_hu": ("{f}. em. {n}.", "space", "", ""),            # Hungary: 3. em. 12.
    # Other European scripts
    "diam": ("διαμ. {n}", "comma", "Apt {n}", ""),          # Greece
    "diam_cy": ("διαμ. {n}", "comma", "Flat {n}", ""),      # Cyprus
    "daire": ("D:{n}", "space", "", ""),                    # Turkey: No:12 D:5
    "ge": ("ბინა {n}", "comma", "apt. {n}", ""),
    "am": ("բն. {n}", "comma", "apt. {n}", ""),
    "az": ("mənzil {n}", "comma", "", ""),
    "toot": ("{n} тоот", "comma", "{n} toot", ""),          # Mongolia
    # Middle East
    "arab": ("شقة {n}", "comma", "Flat {n}", ""),
    "dira": ("דירה {n}", "comma", "Apt {n}", ""),
    "vahed": ("واحد {n}", "comma", "Unit {n}", ""),         # Iran: plak, vahed
    # East Asia: room numbers are the floor and the door
    "cn": ("{f}0{k}室", "space", "Room {f}0{k}", "comma"),
    "tw": ("{f}樓", "space", "{f}F.", "before"),
    "hk": ("{n}樓{l}室", "space", "Flat {l}, {n}/F", "before"),
    "mo": ("{n}樓{l}", "space", "{n}º andar {l}", "comma"),
    "jp": ("{f}0{k}号室", "space", "{f}0{k}", "dash"),
    "kr": ("{f}0{k}호", "comma", "#{f}0{k}", "comma"),
}
_UNIT_BY_STYLE = {
    "apt": "US CA IE PW MH FM BS JM BB TT ET ER SO SC MU",
    "flat": "GB AU NZ MT FJ NR KI SB TV TO WS PG VU ZM TZ KE UG MW RW SS NG GM IN PK NP BT MV",
    "flat_bd": "BD",
    "piso": "ES PT AR AD GQ",
    "ap_br": "BR",
    "andar": "AO ST MZ CV",
    "app_fr": "FR BE LU MC TN DZ MA GA DJ CM CG CD KM MG CF BI TD HT",
    "int_it": "IT VA SM",
    "int_mx": "MX",
    "apto": "CR DO CU PA SV NI HN GT VE",
    "apto_floor": "CO UY",
    "depto": "CL",
    "dpto": "PE BO PY",
    "ap_ro": "RO MD AL XK",
    "whg": "DE AT CH LI",
    "lgh": "SE",
    "bolig": "NO",
    "dk": "DK",
    "is": "IS",
    "dash": "NL EE LV LT",
    "fi": "FI",
    "slash": "PL",
    "byt": "CZ SK",
    "stanovanje": "SI",
    "stan": "HR BA ME",
    "stan_cyr": "RS MK",
    "ap_bg": "BG",
    "kv": "RU BY UA KZ KG TJ",
    "kv_latin": "UZ TM",
    "em_hu": "HU",
    "diam": "GR",
    "diam_cy": "CY",
    "daire": "TR",
    "ge": "GE",
    "am": "AM",
    "az": "AZ",
    "toot": "MN",
    "arab": "EG LY SD JO IQ SY YE PS LB SA AE BH KW QA OM",
    "dira": "IL",
    "vahed": "IR",
    "cn": "CN",
    "tw": "TW",
    "hk": "HK",
    "mo": "MO",
    "jp": "JP",
    "kr": "KR KP",
}
UNIT = {cc: style for style, codes in _UNIT_BY_STYLE.items() for cc in codes.split()}
assert len(UNIT) == sum(len(c.split()) for c in _UNIT_BY_STYLE.values()), "a country in two styles"


def fetch(code, cache):
    f = cache / f"{code}.json"
    if not f.exists():
        with urllib.request.urlopen(f"{BASE}/{code}", timeout=60) as r:
            f.write_bytes(r.read())
    return json.loads(f.read_text(encoding="utf-8"))


def lines_of(fmt):
    """Translate a libaddressinput format into {A}{C}{S}{Z}{D}{X} lines."""
    out = []
    for line in fmt.split("%n"):
        line = line.replace("%N", "").replace("%O", "")
        line = (line.replace("%A", "{A}").replace("%C", "{C}").replace("%S", "{S}")
                .replace("%Z", "{Z}").replace("%D", "{D}").replace("%X", "{X}"))
        if "{" not in line:
            continue
        out.append(line.strip())
    return "|".join(out)


# Curated where the first example does not give a pattern the service's
# regular expression accepts (letters and first digits are restricted).
PATTERNS = {
    "NL": "N### LL",
    "CA": "L#L #L#",
    "AR": "L####LLL",
    "IE": "A## ####",
}
LETTERS = "ABCEGHJKLMNPRTVXY"
UK_INWARD = "ABDEFGHJLNPQRSTUWXYZ"
# GeoNames gives only the first part of these postcodes (for copyright
# reasons); the rest is drawn from this pattern, which uses the same symbols and '_' for a
# space (a cell does not keep a leading space).
COMPLETIONS = {
    "CA": "_#L#",
    "GB": "_#UU",
    "NL": "_LL",
    "IE": "_####",
    "GG": "_#UU",
    "JE": "_#UU",
    "IM": "_#UU",
}


def pattern_of(example):
    return "".join("#" if c.isdigit() else "A" if c.isalpha() and c.isascii() else c for c in example)


def draw(pattern, rng):
    out = []
    for c in pattern:
        if c == "#": out.append(str(rng.randrange(10)))
        elif c == "N": out.append(str(1 + rng.randrange(9)))
        elif c == "A": out.append(chr(65 + rng.randrange(26)))
        elif c == "L": out.append(LETTERS[rng.randrange(len(LETTERS))])
        elif c == "U": out.append(UK_INWARD[rng.randrange(len(UK_INWARD))])
        else: out.append(c)
    return "".join(out)


def derive(meta, rng, code):
    examples = [e for e in meta.get("zipex", "").split(",") if e]
    rx = meta.get("zip")
    if not examples or not rx:
        return "", examples
    try:
        rx = re.compile(rx)
    except re.error:
        return "", examples
    pat = PATTERNS.get(code) or pattern_of(examples[0])
    if all(rx.fullmatch(draw(pat, rng)) for _ in range(2000)):
        return pat, examples
    return "", examples


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cache", default="build/formats")
    ap.add_argument("--out", default="resources")
    ap.add_argument("--regex-out", default="tests/data/postcode_regex.tsv",
                    help="the service's postcode regular expressions, for the tests")
    a = ap.parse_args()

    cache = Path(a.cache)
    cache.mkdir(parents=True, exist_ok=True)
    index = fetch_index(cache)
    rng = random.Random(1)

    rows, regexes, derived, fallback, none = [], [], 0, 0, 0
    for code in index:
        meta = fetch(code, cache)
        fmt = meta.get("fmt")
        if not fmt:
            # The service gives the territories without a format of their own
            # the default ("%N%n%O%n%A%n%C"), which has no postcode line.
            fmt = "%N%n%O%n%A%n%C"
        lines = lines_of(fmt)
        latin = lines_of(meta["lfmt"]) if meta.get("lfmt") else ""
        lines, latin = LAYOUTS.get(code, (lines, latin))
        require = meta.get("require", "AC")
        sub_keys, sub_names = meta.get("sub_keys", ""), meta.get("sub_names", "")
        style = "code" if sub_names and sub_keys != sub_names else "name"
        pat, examples = derive(meta, rng, code)
        if pat:
            derived += 1
        elif examples:
            fallback += 1
        else:
            none += 1
        before = code in NUMBER_BEFORE
        sep = "comma" if code in NUMBER_COMMA else "space"
        rows.append((code, lines, latin, require, style, pat, COMPLETIONS.get(code, ""), "~".join(examples),
                     "0" if before else "1", sep, UNIT.get(code, "none"), NUMBER_STYLE.get(code, ""),
                     *UNIT_STYLES[UNIT.get(code, "none")]))
        if meta.get("zip"):
            regexes.append((code, meta["zip"]))

    out = Path(a.out)
    for tier in ("lite", "full"):
        d = out / tier
        d.mkdir(parents=True, exist_ok=True)
        with open(d / "formats.tsv", "w", encoding="utf-8", newline="\n") as f:
            f.write("country_code\tlines\tlines_latin\trequire\tregion_style\tzip_pattern\tzip_completion\tzip_examples\t"
                    "number_after\tnumber_sep\tunit_style\tnumber_style\t"
                    "unit_format\tunit_join\tunit_format_latin\tunit_join_latin\n")
            for r in rows:
                f.write("\t".join(r).replace("\n", " ") + "\n")
    rx = Path(a.regex_out)
    rx.parent.mkdir(parents=True, exist_ok=True)
    with open(rx, "w", encoding="utf-8", newline="\n") as f:
        f.write("country_code\tregex\n")
        for code, r in regexes:
            f.write(f"{code}\t{r}\n")
    print(f"{len(rows)} territories: {derived} postcode patterns derived, {fallback} use examples, "
          f"{none} have no postcodes", file=sys.stderr)


def fetch_index(cache):
    f = cache / "index.json"
    if not f.exists():
        with urllib.request.urlopen(BASE, timeout=60) as r:
            f.write_bytes(r.read())
    return json.loads(f.read_text(encoding="utf-8"))["countries"].split("~")


if __name__ == "__main__":
    main()

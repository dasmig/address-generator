# Data and Method

Where the data in `resources/` comes from and how it is built. For using the library, see the [usage guide](usage.md).

[TOC]

## Tiers

A tier is a folder of six TSV files. Both tiers use the places of [city-generator](https://github.com/dasmig/city-generator)'s tiers, so a city drawn there is found here by the same GeoNames id.

| Tier | Places | Folder |
| --- | --- | --- |
| `lite` | 33,521 | `resources/lite`: places of 15,000 people or more (GeoNames `cities15000`), compiled in with `DASMIG_ADDRESS_EMBED` |
| `full` | 231,224 | `resources/full`: places of 500 people or more (GeoNames `cities500`) |

## Tables

| File | Key | What it holds | Source |
| --- | --- | --- | --- |
| `places.tsv` | `geonameid` | The places an address is drawn in | GeoNames, through city-generator |
| `formats.tsv` | `country_code` | Each territory's line layout, postcode format and number placement | libaddressinput, with curated columns |
| `streets.tsv` | `geonameid`, `name` | A place's real streets, weighted by address count | OpenStreetMap |
| `postcodes.tsv` | `geonameid`, `postcode` | A place's postcodes | GeoNames postal codes |
| `street_types.tsv` | `country_code`, `type` | Types of composed streets (`Rue`, `-straße`) | Curated |
| `street_names.tsv` | `country_code`, `name` | Names of composed streets | Curated |

### places.tsv

| Column | Meaning | Source |
| --- | --- | --- |
| `geonameid` | GeoNames id, the key; the same as city-generator's | GeoNames |
| `country_code` | ISO 3166-1 alpha-2 | GeoNames |
| `name` | The place's name (UTF-8) | GeoNames |
| `region_code` | GeoNames admin1 code; an abbreviation in the US and most places that use one | GeoNames; for Canada, Australia and Brazil, where GeoNames numbers the divisions, replaced by the usual abbreviation (curated, `scripts/prepare_places.py`) |
| `region` | The admin1 name | GeoNames `admin1CodesASCII.txt` |
| `population` | Weights the place when a request does not name one (0 counts as 1) | GeoNames |

### formats.tsv

One row per territory (252, every one the Address Data Service lists).

| Column | Meaning | Source |
| --- | --- | --- |
| `lines` | The layout, lines joined by `\|`; `{A}` street and number, `{C}` city, `{S}` region, `{Z}` postcode, `{D}` dependent locality, `{X}` sorting code (the last two are never filled) | libaddressinput `fmt`, without the recipient and organisation lines |
| `lines_latin` | The Latin layout where the local script has one | libaddressinput `lfmt` |
| `require` | The fields an address needs | libaddressinput |
| `region_style` | `code` when the region is written as an abbreviation | libaddressinput: `code` where it lists region abbreviations |
| `zip_pattern` | How a postcode is written: `#` a digit, `N` a digit 1 to 9, `A` a letter, `L` one of `ABCEGHJKLMNPRTVXY`, anything else literally | Derived from the first postcode example; kept only when 2,000 postcodes drawn from it all match the service's regular expression (126 territories); curated for NL, CA, AR and IE, where letters and first digits are restricted |
| `zip_completion` | Where the postcode data holds only the first part, how the rest is written (`_` is a space, `U` a letter allowed in the second half of a UK postcode) | Curated for CA, GB, NL, IE, GG, JE and IM |
| `zip_examples` | The service's examples, `~` separated, drawn from when there is no pattern | libaddressinput `zipex` (56 territories) |
| `number_after` | 1 when the house number follows the street | Curated: before the street in the English-speaking countries, France, Luxembourg and a few others; after it elsewhere |
| `number_sep` | `space` or `comma` between street and number | Curated: a comma in Spanish, Portuguese and most Latin American countries and in Russia |
| `unit_style` | How a unit is written (`apt`, `flat`, `app_fr`, `whg`, `piso`, `ap_br`, `none`) | Curated for 24 countries; territories without one never have a unit |
| `number_style` | Empty or `plain` for a number, `blocks` for Japan's `chome-ban-go` | Curated (Japan only) |

Japan's `lines` and `lines_latin` are curated: the service leaves the municipality inside the address field, so the layouts here give it a line of its own (`scripts/prepare_formats.py`, `LAYOUTS`).

The postcode regular expressions the service gives are kept in `tests/data/postcode_regex.tsv`; the tests check every generated postcode against them for each territory that has one.

### streets.tsv

| Column | Meaning | Source |
| --- | --- | --- |
| `country_code` | ISO 3166-1 alpha-2 | |
| `geonameid` | The place the street belongs to | Assigned (see below) |
| `name` | The street's name | OSM `name` of a `highway` way, or `addr:street` |
| `class` | The most common `highway` value of its ways; empty if known only from addresses | OSM |
| `weight` | The number of addresses on the street in the place (1 when it has none) | OSM `addr:housenumber` nodes and building ways with `addr:street` |
| `min_number`, `max_number` | The 5th and 95th percentile of the house numbers seen (first 200); 0 when none | OSM `addr:housenumber` |
| `name_latin` | The Latin name of a street named in another script (optional; empty when none) | OSM `name:en`, `name:latin`, `int_name` and transliteration tags |

### postcodes.tsv

| Column | Meaning | Source |
| --- | --- | --- |
| `country_code`, `geonameid` | The place a postcode belongs to | GeoNames; assigned (see below) |
| `postcode` | In the country's format | GeoNames postal codes |
| `weight` | 1 (GeoNames has no address counts) | |
| `partial` | 1 when GeoNames holds only the first part (CA, GB, NL, IE, GG, JE, IM), completed from `zip_completion` | GeoNames |

### street_types.tsv, street_names.tsv

Curated for the composed fallback: types with their position (`before`: `Rua X`; `after`: `Oak Street`; `suffix`: `Hauptstraße`) and name stems, for 12 countries and a generic English set (`*`). They are common street words, not a list of real streets, and a composed street is flagged `real_street: false`.

## How a Place Is Assigned

OSM and GeoNames postcodes have coordinates, not GeoNames place ids. `scripts/prepare_osm.py` and `scripts/prepare_postcodes.py` assign each object to the place of `cities500` with the smallest distance divided by the square root of its population, within 8 km. Plain nearest-centroid gave Lisbon 94 streets, because the small places around it took the rest; with the population scaling it has 2,431 (Porto 1,560), tuned on Portugal. A street whose `addr:city` names a place within 25 km goes to that place.

## Generation Model

See [How an Address Is Drawn](usage.md#how-an-address-is-drawn). Places are weighted by population, streets by address count, postcodes by `weight`; each part draws from a stream split from the call seed with dasmig-core, so the result is the same on every platform.

## Cuts

A place keeps its streets with the most addresses, up to K by its population, and drops streets with fewer than the minimum number of addresses unless they have none at all (those only fill the places left up to K):

| Place population | `lite` K | `full` K |
| --- | --- | --- |
| under 5,000 | no places | 12 |
| 5,000 to 15,000 | no places | 24 |
| 15,000 to 50,000 | 10 | 48 |
| 50,000 to 250,000 | 20 | 100 |
| 250,000 to 1 million | 40 | 200 |
| 1 million and over | 80 | 400 |
| Minimum address count | 5 | 2 |

`lite` keeps the postcodes of its places, at most 25 per place; `full` keeps all.

`lite` has one `streets.tsv`, which is compiled in. `full` has `streets/<CC>.tsv` for each country and `streets.manifest.tsv` with the country, row count and content hash of each file; `scripts/assemble.py` writes them.

## Coverage

Real streets exist for the countries listed below, built from the Geofabrik extracts listed in `scripts/extracts.tsv` (`python scripts/world.py run`); the 34 territories without an extract (Gibraltar, Guam, the Caribbean islands and similar) use composed streets. Northern Ireland is not in the United Kingdom extract. Postcodes are in the data for the 121 countries GeoNames covers, except where it holds them truncated (AR, BR, CL, CN, MT), which use the country's pattern.

<!-- coverage:begin -->
212 countries have real streets.

| Country | `lite` places | with streets | rows | `full` places | with streets | rows | population in a place with streets |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| AD | 2 | 2 | 20 | 20 | 20 | 307 | 100% |
| AE | 63 | 60 | 1,127 | 106 | 98 | 5,503 | 98% |
| AF | 54 | 51 | 712 | 319 | 269 | 2,712 | 98% |
| AL | 23 | 23 | 352 | 415 | 260 | 3,122 | 96% |
| AM | 29 | 28 | 469 | 626 | 592 | 4,949 | 96% |
| AO | 103 | 61 | 706 | 190 | 88 | 1,721 | 86% |
| AR | 250 | 250 | 3,823 | 1,020 | 1,008 | 29,611 | 100% |
| AT | 64 | 63 | 940 | 3,039 | 2,999 | 37,485 | 100% |
| AU | 311 | 304 | 3,926 | 4,899 | 4,825 | 80,040 | 99% |
| AZ | 58 | 58 | 791 | 191 | 172 | 5,038 | 99% |
| BA | 23 | 23 | 317 | 299 | 247 | 3,925 | 95% |
| BD | 137 | 133 | 2,184 | 161 | 153 | 5,377 | 99% |
| BE | 223 | 223 | 2,645 | 2,256 | 2,256 | 42,044 | 100% |
| BF | 74 | 28 | 212 | 118 | 41 | 674 | 75% |
| BG | 56 | 56 | 830 | 326 | 304 | 7,679 | 99% |
| BH | 8 | 8 | 150 | 10 | 10 | 790 | 100% |
| BI | 14 | 6 | 58 | 33 | 13 | 226 | 75% |
| BJ | 66 | 25 | 186 | 87 | 28 | 429 | 68% |
| BM | 1 | 0 | 0 | 12 | 12 | 242 | 100% |
| BN | 8 | 8 | 90 | 48 | 47 | 1,009 | 97% |
| BO | 39 | 39 | 694 | 181 | 168 | 4,352 | 100% |
| BR | 2,345 | 2,331 | 33,619 | 5,886 | 5,841 | 226,686 | 100% |
| BS | 4 | 4 | 50 | 28 | 20 | 362 | 99% |
| BT | 4 | 4 | 29 | 31 | 28 | 293 | 98% |
| BW | 25 | 21 | 169 | 139 | 48 | 463 | 79% |
| BY | 57 | 57 | 960 | 312 | 312 | 8,791 | 100% |
| BZ | 4 | 3 | 40 | 138 | 136 | 1,234 | 100% |
| CA | 502 | 501 | 7,074 | 3,176 | 3,144 | 69,331 | 100% |
| CC | 1 | 0 | 0 | 1 | 1 | 10 | 100% |
| CD | 114 | 55 | 1,045 | 118 | 55 | 3,031 | 89% |
| CF | 32 | 10 | 52 | 50 | 12 | 59 | 56% |
| CG | 23 | 11 | 212 | 60 | 17 | 961 | 88% |
| CH | 94 | 94 | 1,062 | 1,895 | 1,887 | 28,121 | 100% |
| CI | 183 | 59 | 471 | 5,136 | 284 | 1,606 | 50% |
| CK | 1 | 0 | 0 | 2 | 2 | 36 | 100% |
| CL | 146 | 146 | 2,190 | 250 | 250 | 12,602 | 100% |
| CM | 87 | 52 | 481 | 145 | 72 | 1,486 | 80% |
| CN | 2,082 | 1,643 | 34,417 | 16,027 | 9,567 | 138,433 | 97% |
| CO | 317 | 315 | 4,761 | 1,177 | 1,142 | 33,276 | 99% |
| CR | 41 | 41 | 460 | 136 | 136 | 3,725 | 100% |
| CU | 140 | 140 | 2,063 | 212 | 210 | 10,452 | 100% |
| CV | 4 | 4 | 60 | 25 | 25 | 410 | 100% |
| CX | 1 | 0 | 0 | 1 | 1 | 12 | 100% |
| CY | 15 | 15 | 190 | 132 | 127 | 2,271 | 100% |
| CZ | 125 | 125 | 1,560 | 2,727 | 1,729 | 25,675 | 92% |
| DE | 1,144 | 1,143 | 14,505 | 11,818 | 11,794 | 210,568 | 100% |
| DJ | 6 | 6 | 67 | 31 | 20 | 252 | 98% |
| DK | 64 | 64 | 864 | 505 | 504 | 10,635 | 100% |
| DO | 49 | 48 | 838 | 213 | 206 | 6,277 | 99% |
| DZ | 294 | 258 | 2,574 | 350 | 292 | 7,456 | 94% |
| EC | 71 | 71 | 1,202 | 542 | 516 | 10,782 | 100% |
| EE | 8 | 8 | 130 | 198 | 198 | 3,091 | 100% |
| EG | 235 | 230 | 4,252 | 254 | 245 | 19,699 | 99% |
| ER | 7 | 6 | 54 | 18 | 9 | 218 | 92% |
| ES | 735 | 691 | 8,900 | 7,393 | 7,221 | 122,392 | 95% |
| ET | 233 | 99 | 477 | 255 | 112 | 892 | 67% |
| FI | 103 | 103 | 1,330 | 882 | 864 | 16,706 | 100% |
| FJ | 7 | 7 | 100 | 16 | 12 | 452 | 96% |
| FK | 1 | 0 | 0 | 1 | 1 | 12 | 100% |
| FM | 1 | 0 | 0 | 61 | 7 | 55 | 65% |
| FO | 1 | 0 | 0 | 35 | 35 | 432 | 100% |
| FR | 691 | 691 | 8,573 | 15,352 | 15,343 | 230,496 | 100% |
| GA | 15 | 12 | 92 | 50 | 26 | 343 | 88% |
| GB | 859 | 841 | 11,477 | 5,903 | 5,741 | 121,299 | 98% |
| GE | 17 | 17 | 280 | 206 | 168 | 2,984 | 98% |
| GF | 5 | 5 | 60 | 18 | 18 | 507 | 100% |
| GG | 1 | 1 | 10 | 12 | 12 | 201 | 100% |
| GH | 102 | 95 | 1,275 | 119 | 110 | 3,698 | 98% |
| GL | 1 | 0 | 0 | 18 | 15 | 204 | 95% |
| GM | 33 | 24 | 126 | 179 | 47 | 298 | 67% |
| GN | 47 | 15 | 233 | 83 | 20 | 585 | 76% |
| GP | 10 | 9 | 100 | 25 | 25 | 832 | 100% |
| GQ | 4 | 3 | 42 | 27 | 24 | 141 | 97% |
| GR | 115 | 115 | 1,610 | 1,945 | 1,870 | 20,510 | 99% |
| GT | 102 | 100 | 1,177 | 340 | 292 | 5,248 | 97% |
| GW | 4 | 2 | 44 | 38 | 6 | 83 | 66% |
| GY | 3 | 3 | 40 | 22 | 22 | 448 | 100% |
| HK | 140 | 125 | 1,453 | 324 | 270 | 4,220 | 99% |
| HN | 24 | 24 | 350 | 502 | 350 | 3,664 | 94% |
| HR | 26 | 25 | 336 | 1,215 | 1,137 | 11,739 | 99% |
| HT | 30 | 30 | 574 | 113 | 108 | 2,783 | 100% |
| HU | 138 | 136 | 1,947 | 1,178 | 1,168 | 23,173 | 99% |
| ID | 438 | 421 | 8,077 | 9,294 | 3,290 | 41,999 | 99% |
| IE | 39 | 39 | 540 | 566 | 560 | 9,320 | 100% |
| IL | 106 | 106 | 1,509 | 725 | 497 | 10,035 | 96% |
| IM | 1 | 1 | 10 | 21 | 21 | 281 | 100% |
| IN | 3,776 | 2,671 | 25,477 | 7,015 | 4,389 | 64,588 | 87% |
| IQ | 83 | 79 | 1,754 | 159 | 150 | 5,303 | 99% |
| IR | 329 | 329 | 5,946 | 1,066 | 1,022 | 36,884 | 100% |
| IS | 6 | 6 | 70 | 50 | 50 | 916 | 100% |
| IT | 653 | 652 | 8,213 | 11,835 | 11,671 | 172,423 | 100% |
| JE | 1 | 1 | 10 | 4 | 4 | 79 | 100% |
| JM | 11 | 11 | 193 | 97 | 96 | 2,131 | 100% |
| JO | 34 | 30 | 438 | 148 | 85 | 1,791 | 93% |
| JP | 1,296 | 1,286 | 19,446 | 2,173 | 2,104 | 51,594 | 100% |
| KE | 122 | 105 | 1,331 | 335 | 241 | 3,780 | 94% |
| KG | 31 | 31 | 381 | 111 | 97 | 2,582 | 98% |
| KH | 46 | 36 | 358 | 334 | 171 | 1,548 | 87% |
| KI | 1 | 1 | 7 | 37 | 20 | 52 | 84% |
| KP | 97 | 31 | 216 | 127 | 34 | 297 | 59% |
| KR | 147 | 147 | 3,631 | 311 | 311 | 19,160 | 100% |
| KW | 18 | 18 | 290 | 28 | 27 | 1,478 | 100% |
| KZ | 83 | 82 | 1,433 | 347 | 306 | 10,507 | 99% |
| LA | 20 | 10 | 141 | 82 | 22 | 408 | 75% |
| LB | 16 | 16 | 330 | 45 | 44 | 1,292 | 100% |
| LI | 1 | 0 | 0 | 13 | 13 | 192 | 100% |
| LK | 60 | 60 | 884 | 89 | 89 | 4,180 | 100% |
| LR | 16 | 12 | 185 | 27 | 14 | 288 | 92% |
| LS | 11 | 11 | 114 | 47 | 29 | 426 | 91% |
| LT | 36 | 36 | 458 | 164 | 164 | 4,011 | 100% |
| LU | 3 | 3 | 40 | 235 | 235 | 3,181 | 100% |
| LV | 15 | 15 | 220 | 147 | 146 | 3,033 | 100% |
| LY | 59 | 56 | 693 | 119 | 106 | 1,612 | 98% |
| MA | 179 | 126 | 1,845 | 480 | 201 | 6,644 | 90% |
| MC | 2 | 2 | 20 | 10 | 10 | 179 | 100% |
| MD | 22 | 22 | 290 | 89 | 89 | 2,679 | 100% |
| ME | 8 | 8 | 100 | 64 | 59 | 948 | 99% |
| MG | 106 | 35 | 354 | 152 | 44 | 681 | 62% |
| MH | 1 | 1 | 10 | 25 | 5 | 43 | 81% |
| MK | 38 | 32 | 422 | 299 | 129 | 2,444 | 83% |
| ML | 59 | 45 | 351 | 92 | 61 | 942 | 94% |
| MM | 175 | 144 | 1,683 | 572 | 237 | 5,264 | 89% |
| MN | 24 | 22 | 169 | 331 | 231 | 786 | 96% |
| MO | 6 | 6 | 105 | 8 | 8 | 416 | 100% |
| MQ | 8 | 8 | 90 | 22 | 22 | 712 | 100% |
| MR | 26 | 16 | 175 | 88 | 32 | 576 | 76% |
| MT | 7 | 6 | 60 | 70 | 70 | 1,353 | 100% |
| MU | 14 | 14 | 184 | 116 | 111 | 2,071 | 99% |
| MV | 1 | 1 | 20 | 34 | 22 | 320 | 93% |
| MW | 23 | 20 | 198 | 38 | 31 | 493 | 96% |
| MX | 643 | 642 | 10,947 | 16,880 | 13,336 | 179,667 | 97% |
| MY | 355 | 351 | 5,589 | 739 | 694 | 29,327 | 99% |
| MZ | 66 | 30 | 402 | 94 | 39 | 1,013 | 77% |
| NA | 19 | 18 | 233 | 93 | 59 | 1,442 | 93% |
| NC | 3 | 3 | 40 | 63 | 60 | 797 | 96% |
| NE | 42 | 18 | 143 | 74 | 30 | 475 | 75% |
| NF | 1 | 0 | 0 | 1 | 1 | 12 | 100% |
| NG | 263 | 179 | 2,430 | 917 | 399 | 7,049 | 91% |
| NI | 37 | 37 | 524 | 167 | 160 | 2,903 | 100% |
| NL | 240 | 240 | 3,060 | 1,943 | 1,939 | 37,987 | 100% |
| NO | 40 | 40 | 630 | 623 | 622 | 10,891 | 100% |
| NP | 46 | 46 | 686 | 83 | 72 | 2,497 | 100% |
| NR | 1 | 0 | 0 | 14 | 5 | 6 | 45% |
| NU | 1 | 0 | 0 | 1 | 1 | 12 | 100% |
| NZ | 58 | 58 | 910 | 726 | 720 | 13,561 | 100% |
| OM | 27 | 27 | 313 | 36 | 33 | 872 | 100% |
| PA | 27 | 27 | 410 | 801 | 497 | 4,041 | 94% |
| PE | 136 | 130 | 2,226 | 2,293 | 1,241 | 21,342 | 96% |
| PF | 3 | 3 | 30 | 45 | 35 | 393 | 94% |
| PG | 13 | 12 | 152 | 52 | 37 | 794 | 91% |
| PH | 525 | 517 | 8,124 | 4,524 | 3,345 | 39,626 | 95% |
| PK | 354 | 332 | 3,671 | 563 | 473 | 8,264 | 99% |
| PL | 363 | 363 | 4,892 | 3,829 | 3,336 | 58,770 | 98% |
| PN | 1 | 0 | 0 | 1 | 1 | 10 | 100% |
| PR | 21 | 21 | 320 | 227 | 227 | 4,644 | 100% |
| PS | 44 | 43 | 522 | 446 | 364 | 3,399 | 95% |
| PT | 179 | 179 | 2,069 | 1,079 | 1,068 | 24,216 | 100% |
| PW | 1 | 0 | 0 | 29 | 19 | 57 | 85% |
| PY | 27 | 27 | 540 | 169 | 156 | 3,956 | 100% |
| QA | 26 | 26 | 352 | 43 | 42 | 1,939 | 99% |
| RE | 14 | 14 | 200 | 26 | 26 | 1,260 | 100% |
| RO | 134 | 134 | 2,070 | 7,149 | 4,925 | 50,806 | 88% |
| RS | 50 | 50 | 760 | 489 | 488 | 9,408 | 100% |
| RU | 1,106 | 1,104 | 17,119 | 5,279 | 5,229 | 141,588 | 100% |
| RW | 45 | 16 | 142 | 50 | 17 | 526 | 70% |
| SA | 89 | 75 | 1,170 | 136 | 101 | 3,347 | 96% |
| SB | 7 | 7 | 72 | 23 | 14 | 267 | 87% |
| SC | 1 | 1 | 10 | 26 | 24 | 166 | 97% |
| SD | 75 | 36 | 386 | 112 | 46 | 651 | 85% |
| SE | 109 | 109 | 1,510 | 1,322 | 1,312 | 23,233 | 100% |
| SG | 65 | 64 | 1,017 | 121 | 109 | 3,117 | 99% |
| SH | 1 | 0 | 0 | 6 | 6 | 74 | 100% |
| SI | 9 | 9 | 130 | 602 | 586 | 5,800 | 99% |
| SJ | 1 | 0 | 0 | 2 | 1 | 12 | 99% |
| SK | 61 | 61 | 730 | 741 | 508 | 8,906 | 94% |
| SL | 14 | 12 | 166 | 88 | 43 | 958 | 86% |
| SN | 70 | 40 | 467 | 152 | 74 | 1,342 | 86% |
| SO | 41 | 30 | 275 | 76 | 53 | 474 | 88% |
| SR | 12 | 12 | 126 | 35 | 34 | 891 | 100% |
| SS | 16 | 15 | 151 | 23 | 22 | 324 | 97% |
| ST | 1 | 1 | 20 | 50 | 11 | 70 | 60% |
| SV | 35 | 35 | 500 | 102 | 102 | 3,603 | 100% |
| SY | 84 | 61 | 716 | 306 | 147 | 1,909 | 92% |
| SZ | 3 | 2 | 40 | 22 | 13 | 299 | 86% |
| TD | 46 | 20 | 144 | 74 | 25 | 240 | 72% |
| TG | 20 | 15 | 187 | 35 | 21 | 518 | 93% |
| TH | 327 | 306 | 3,696 | 1,071 | 736 | 16,244 | 96% |
| TJ | 31 | 26 | 275 | 103 | 65 | 1,038 | 91% |
| TL | 9 | 9 | 70 | 28 | 25 | 241 | 97% |
| TM | 33 | 30 | 322 | 99 | 66 | 1,161 | 93% |
| TN | 126 | 124 | 1,424 | 286 | 249 | 5,482 | 97% |
| TO | 1 | 1 | 10 | 47 | 37 | 188 | 89% |
| TR | 423 | 417 | 6,785 | 3,850 | 2,398 | 39,661 | 98% |
| TV | 1 | 0 | 0 | 12 | 4 | 10 | 74% |
| TW | 67 | 67 | 1,540 | 88 | 88 | 8,000 | 100% |
| TZ | 224 | 103 | 947 | 320 | 136 | 2,986 | 82% |
| UA | 364 | 363 | 5,743 | 5,156 | 4,519 | 69,763 | 98% |
| UG | 141 | 107 | 849 | 212 | 144 | 2,106 | 88% |
| US | 3,398 | 3,398 | 46,015 | 21,763 | 21,745 | 486,368 | 100% |
| UY | 31 | 31 | 450 | 154 | 154 | 3,959 | 100% |
| UZ | 112 | 110 | 1,648 | 220 | 196 | 6,680 | 99% |
| VE | 216 | 214 | 3,447 | 414 | 380 | 12,795 | 100% |
| VI | 2 | 2 | 30 | 7 | 7 | 220 | 100% |
| VN | 282 | 277 | 5,596 | 856 | 790 | 26,618 | 100% |
| WF | 1 | 0 | 0 | 8 | 2 | 3 | 14% |
| WS | 1 | 1 | 10 | 67 | 62 | 311 | 96% |
| XK | 21 | 21 | 320 | 69 | 69 | 2,281 | 100% |
| YE | 25 | 22 | 318 | 316 | 148 | 766 | 98% |
| YT | 6 | 6 | 70 | 66 | 65 | 1,229 | 98% |
| ZA | 304 | 289 | 4,761 | 985 | 765 | 28,205 | 98% |
| ZM | 77 | 46 | 473 | 98 | 52 | 1,442 | 87% |
| ZW | 30 | 28 | 509 | 69 | 42 | 1,748 | 96% |
<!-- coverage:end -->

## Rebuilding the Data

Raw downloads stay out of the repository. From the repository root:

```bash
python scripts/prepare_places.py --cities ../city-generator/resources --out resources
python scripts/prepare_formats.py --out resources           # downloads libaddressinput's data
python scripts/prepare_osm.py PT portugal-latest.osm.pbf \
    --cities ../city-generator/resources/full/cities.tsv --out build/osm   # needs pyosmium
python scripts/world.py run --jobs 6 --mem-gb 40    # every country: downloads each extract, builds, deletes it
python scripts/prepare_postcodes.py allCountries.txt \
    --cities ../city-generator/resources/full/cities.tsv \
    --formats resources/lite/formats.tsv --regex tests/data/postcode_regex.tsv --out build/postcodes
python scripts/assemble.py --streets build/osm --postcodes build/postcodes --out resources
make embed
```

After changing the data, update the known-answer tests (the data hash and the results change).

## Known Limitations

- **Licence.** OpenStreetMap data is under the ODbL, which asks for attribution and share-alike for derived databases. The licence has not been reviewed for this library (owner's decision, 2026-10-17); the OSM notice is in `LICENSE_DATA.txt`.
- **Not real addresses.** The place, street and postcode are real, the number and unit are drawn. An address can coincide with a real one, and no address is checked to exist.
- **Place assignment is approximate.** A street near the boundary of two places can land in the wrong one; the distance scaling favours large cities, and a street longer than 8 km from every place is dropped.
- **Postcode and place can disagree where the postcode comes from the pattern**: Brazil, Argentina, Chile, China and Malta, and any place without GeoNames postcodes.
- **Layouts are the service's, not checked per country.** Spain writes the province after the postcode and city; the data has the autonomous community. Korea, China and Taiwan keep the service's large-to-small layout with a plain number and, as only Japan has street data, a composed street; Japan gets a municipality line and block numbers, but the municipality is the GeoNames place (Latin script) and, in the local script, the streets are Japanese, so a Japanese address mixes scripts and has no ward below the city. With `script = latin` the street is its Latin name where OpenStreetMap has one.
- **Region abbreviations** are only available where GeoNames codes are letters (the US and similar) or the table in `prepare_places.py` has them (Canada, Australia, Brazil); elsewhere the region is written as its name.
- **Street names are written as OSM has them**: abbreviations, local languages and mixed scripts are not normalised. The Latin name (`script = latin`) exists only where a mapper tagged it, so many streets in non-Latin countries keep their local name.
- **`full` streets are loaded per country**, on the first call that needs the country (see [usage](usage.md#street-files-per-country-full)); the first call for a large country takes longer (about 0.4 s for the US with a release build, 3 s unoptimised). Its other tables are read at load.

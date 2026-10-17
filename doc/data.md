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

Real streets exist for the countries listed below; every other country uses composed streets. Postcodes are in the data for the 121 countries GeoNames covers, except where it holds them truncated (AR, BR, CL, CN, MT), which use the country's pattern.

COVERAGE_PLACEHOLDER

## Rebuilding the Data

Raw downloads stay out of the repository. From the repository root:

```bash
python scripts/prepare_places.py --cities ../city-generator/resources --out resources
python scripts/prepare_formats.py --out resources           # downloads libaddressinput's data
python scripts/prepare_osm.py PT portugal-latest.osm.pbf \
    --cities ../city-generator/resources/full/cities.tsv --out build/osm   # needs pyosmium
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
- **Layouts are the service's, not checked per country.** Spain writes the province after the postcode and city; the data has the autonomous community. Korea, China and Taiwan keep the service's large-to-small layout with a plain number and, as only Japan has street data, a composed street; Japan gets a municipality line and block numbers, but the municipality is the GeoNames place (Latin script) and the streets are Japanese, so a Japanese address mixes scripts and has no ward below the city.
- **Region abbreviations** are only available where GeoNames codes are letters (the US and similar) or the table in `prepare_places.py` has them (Canada, Australia, Brazil); elsewhere the region is written as its name.
- **Street names are written as OSM has them**: abbreviations, local languages and mixed scripts are not normalised.
- **`full` streets are loaded per country**, on the first call that needs the country (see [usage](usage.md#street-files-per-country-full)); the first call for a large country takes longer (about 0.4 s for the US with a release build, 3 s unoptimised). Its other tables are read at load.

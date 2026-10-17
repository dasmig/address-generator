# Usage Guide

This guide covers every feature of the address-generator library. For a quick overview, see the [README](../README.md). For the data and how it is built, see [Data and Method](data.md). For the full API reference, run `doxygen Doxyfile` from the repository root and open `doc/api/html/index.html`.

[TOC]

<!-- GitHub-rendered TOC (Doxygen uses [TOC] above instead) -->
<ul>
<li><a href="#quick-start">Quick Start</a></li>
<li><a href="#installation">Installation</a></li>
<li><a href="#loading-resources">Loading Resources</a></li>
<li><a href="#generating">Generating</a></li>
<li><a href="#requests">Requests</a></li>
<li><a href="#how-an-address-is-drawn">How an Address Is Drawn</a></li>
<li><a href="#seeding-and-deterministic-generation">Seeding and Deterministic Generation</a></li>
<li><a href="#extending-data">Extending Data</a></li>
<li><a href="#replay-and-json">Replay and JSON</a></li>
<li><a href="#compiled-in-data">Compiled-in Data</a></li>
<li><a href="#thread-safety">Thread Safety</a></li>
<li><a href="#batch-generation">Batch Generation</a></li>
<li><a href="#error-reference">Error Reference</a></li>
</ul>

## Quick Start

```cpp
#include <dasmig/addressgen.hpp>
#include <iostream>

int main()
{
    auto& gen = dasmig::adg::instance();
    std::cout << gen.get_address(dasmig::address_request{}) << '\n';
}
```

## Installation

Copy `dasmig/addressgen.hpp`, `dasmig/core.hpp` and the `dasmig/core/` directory into your include path, keeping them together. Supply `resources/` next to the program, or define `DASMIG_ADDRESS_EMBED` to compile the `lite` tier in (see [Compiled-in Data](#compiled-in-data)).

## Loading Resources

`instance()` loads the `lite` tier on first use (or `full` if there is no `lite`), from the compiled-in copy or from the first of `resources/`, `../resources`, `address-generator/resources` and `../address-generator/resources` that holds `lite/places.tsv`.

```cpp
dasmig::adg gen;                       // an empty, independent instance
bool ok = gen.load(dasmig::dataset::lite); // false when the tier is not found
gen.load(std::filesystem::path{"path/to/resources/lite"}); // a directory
gen.has_data();
gen.count();       // places loaded
gen.countries();   // ISO codes that have places
gen.has_streets("PT"); // real streets for the country?
```

A tier directory holds six TSV files (see [Data and Method](data.md)); only `places.tsv` is required. Each `load` replaces the data and every layer loaded before.

### Street files per country (`full`)

The `full` tier has no `streets.tsv`. Its streets are one file per country, `streets/<CC>.tsv`, and `streets.manifest.tsv` lists them (country, row count, content hash). Loading reads the manifest only; a country's file is read the first time a call needs it, once, whichever thread gets there first.

```cpp
gen.load(dasmig::dataset::full);
gen.streets_loaded("US");   // false: nothing read yet
auto v = gen.get_address(request, 1);  // reads US.tsv when the draw is in the US
gen.load_streets("BR");     // read a country now, for example before timing or a batch
```

- A file whose hash or row count differs from the manifest is an error (`std::runtime_error`) rather than silently different data.
- `data_hash()` and every result's signature cover the manifest and the layers, not the rows read so far, so a seed replays the same whether or not a country was read before.
- Street layers added with `extend()` are applied to a country's file when it is read, in the order they were added; adding a layer makes the next call read the file again with the layer on top. A layer in `replace` mode leaves a country with only the layer's rows.
- `has_streets(code)` answers from the manifest and does not read a file.

## Generating

```cpp
auto v = gen.get_address(dasmig::address_request{});      // draws a seed from the instance engine
auto w = gen.get_address(dasmig::address_request{}, 1234);  // deterministic; does not touch the engine

v.number;   // 12
v.street;   // "Rua Augusta"
v.unit;     // "3º B", empty when there is none
v.postcode; // "1100-053"
v.city;     // "Lisbon"
v.region;   // as the country writes it ("IL" in the US)
v.country_code;
v.real_street; // true when the street is from the place's own data, false when composed
v.lines();     // {"Rua Augusta, 12, 3º B", "1100-053 Lisbon"}: the country's order, without the recipient
v.formatted(); // "Rua Augusta, 12, 3º B, 1100-053 Lisbon"
v.formatted(" / ");
```

An address converts to `std::string` (the one-line form) and streams with `operator<<`.

## Requests

```cpp
dasmig::address_request r;
r.country = "BR";                                   // a country, or one is drawn by population
r.place = dasmig::place_ref{"Curitiba", "PR"};      // or anchor to a city by name
r.weighted = false;                                 // uniform instead of weighted
r.unit_chance = 0.5;                                // overrides the country's default
r.script = dasmig::address_script::latin;           // the Latin layout, where a country has one
```

- **Country.** Without one, a country is drawn by the population of its places; with `weighted = false`, each country is equally likely. An unknown country throws `std::invalid_argument`.
- **Place.** The anchor for an address whose city was drawn elsewhere (`persona-generator` passes its city). The name is matched without regard to case; `region` (a name or a code) tells places of one name apart, and without it the most populous wins. A place the data lacks needs `country` and is used as given: its street is composed and its postcode comes from the country's pattern.
- **Weighted.** Places by population, streets by the number of addresses on them, postcodes by their weight. Unweighted draws are uniform.
- **Unit chance.** Countries without a unit style (Japan, Korea, China, and any territory the table does not give one) never have a unit.
- **Script.** `address_script::local` (the default) uses the country's own layout; `latin` uses the Latin-script layout for the 14 territories that have one (AE, AM, CN, EG, HK, JP, KP, KR, MO, RU, TH, TW, UA, VN) and changes nothing elsewhere. It changes the layout only: street names stay as OpenStreetMap has them (Japanese streets are in Japanese) and place names are the Latin names GeoNames gives, in either script.

### Japan

A Japanese address is written large to small, with the postcode mark first, the prefecture and municipality on one line and the street last. The house number is a block number, `chome-ban-go` (`8-16-11`); `address::number` is its first part. The data has no ward or municipality names below the place, so the municipality line is the place itself; when the prefecture has the place's own name (Tokyo), it is written once.

## Streets and Postcodes on Their Own

```cpp
dasmig::street_request sr;
sr.country = "FR";
auto street = gen.get_street(sr, 1);   // a street of a French place
street.name; street.city; street.real_street; street.to_json();

dasmig::postcode_request pr;
pr.country = "GB";
std::string code = gen.get_postcode(pr, 1);   // "PE9 4DU"; empty where the country has no postcodes
```

Both take the same `country`, `place` and `weighted` fields as an address and draw from the same streams, so for the same fields and seed `get_street` returns that address's street and `get_postcode` its postcode. Each has the `_batch` and `for_each_` forms of the standard (`get_street_batch`, `for_each_postcode`, ...); a postcode batch is a `std::vector<std::string>`.

## How an Address Is Drawn

Each part draws from a stream split from the call seed, so a part added later leaves the others unchanged: `"country"`, `"place"`, `"street"`, `"number"`, `"unit"` and `"postcode"`.

1. **Country and place.** By population, unless the request names them.
2. **Street.** A street of that place, by address count. A place with no street data gets a *composed* street: a type and a name from the country's own tables (or the generic ones), such as `Rue de la Paix` or `Hauptstraße`. `real_street` tells which.
3. **Number.** Skewed towards low numbers, within the range observed on the street when there is one.
4. **Unit.** With the country's chance, written in the country's style (`Apt 4`, `Flat 3`, `3º B`).
5. **Postcode.** One of the place's own postcodes; where the data holds only the first part (Canada, the UK, the Netherlands, Ireland), the rest is drawn from the country's pattern; with no postcodes for the place, a postcode from the pattern. Countries without postcodes get none.
6. **Layout.** The country's own line order (`{A}` street and number, `{C}` city, `{S}` region, `{Z}` postcode), the number before or after the street as the country writes it, empty parts dropped.

## Seeding and Deterministic Generation

- `get_address(request, seed)` gives the same result for the same request, seed and data, on every platform, without advancing the instance engine.
- `get_address(request)` draws a fresh seed from the instance engine, initially seeded from system entropy. There are no generator-level `seed()` or `unseed()` controls; for deterministic sequences, keep the seeds.
- `v.seed()` is the seed a result was generated from: `get_address(dasmig::address_request{}, v.seed())` replays it with the same request and data.

## Extending Data

The shipped data is a base layer. Files with the same columns as the shipped tables can be loaded on top of it to add places, streets or postcodes, change them or remove them, without editing the shipped files. Each table has its own key (see [Data and Method](data.md#tables)).

```cpp
using dasmig::address_table;

// A directory may hold any of the six table files; a single file must have a table's name.
gen.extend("mods/lisbon");

// One table, from text, for example supplied by a WebAssembly host.
gen.extend_from_string(address_table::streets,
    "geonameid\tcountry_code\tname\tweight\n2267057\tPT\tRua do Exemplo\t500\n", "scenario");

// Remove rows: a layer needs only the key columns and a disabled column.
gen.extend_from_string(address_table::streets,
    "geonameid\tname\tdisabled\n2267057\tRua Augusta\t1\n", "cleanup");

// Add mode refuses to touch keys that are already loaded.
gen.extend("mods/new_streets.tsv", dasmig::core::layer_mode::add);
```

- Every value in a layer is checked before anything changes. A bad layer throws `dasmig::core::tsv_error` with the file, line and column, and the data stays as it was; a directory is applied whole or not at all.
- `layers(table)` lists what is loaded for a table: source, mode, rows added, replaced and removed, and warnings such as unknown columns.
- `load(...)` replaces the base and drops every layer.
- A street with weight 0 stays loaded but is never drawn, so a layer can switch a street off by weight as well as by `disabled`.

## Replay and JSON

Every result records the seed and the data it came from:

```cpp
auto v = gen.get_address(dasmig::address_request{});
v.signature().seed; // the call seed
v.signature().data; // gen.data_hash() when it was generated
```

`to_json()` gives the result as JSON, the form the WebAssembly layer and the game consume:

```json
{"country_code":"PT","city":"Guimarães","region":"Braga","street":"Largo do Toural","number":23,"unit":"","postcode":"4800-019","real_street":true,"geonameid":2740636,"lines":["Largo do Toural, 23","4800-019 Guimarães"],"formatted":"Largo do Toural, 23, 4800-019 Guimarães","signature":{"seed":"...","data":"..."}}
```

Seeds are strings because JavaScript numbers cannot hold every 64-bit value. `to_json(writer)` writes into an existing `core::json_writer`.

## Compiled-in Data

Define `DASMIG_ADDRESS_EMBED` before including the header to compile the `lite` tier into the program. `load(dataset::lite)` and `instance()` then use it and need no files. The compiled-in data has the same content and hash as the files.

`dasmig/addressgen_data.hpp` is generated from `resources/lite` by `make embed`; CI checks it is up to date.

## Thread Safety

Each `adg` instance is independent. Calls to the seeded `get_address(request, seed)` and the batch functions are `const` and safe from several threads on the same instance, provided nothing loads or extends data meanwhile. Reading a `full` country's street file on first use is part of such a call and is guarded, as is `load_streets()`. Calls that draw from the instance engine (`get_address(request)`), `load` and `extend` require external synchronisation (e.g. a `std::mutex`).

## Batch Generation

Every seeded getter has batch forms: `get_address_batch` returns a `std::vector`, and `for_each_address` streams the items to a sink in index order without holding the batch.

```cpp
auto& gen = dasmig::adg::instance();
const auto request = dasmig::address_request{};

// 1,000 results from one batch seed, on four threads.
auto items = gen.get_address_batch(request, {.count = 1000, .threads = 4}, 42);

// Item 1 replays on its own: the batch seed and the index give its seed.
auto again = gen.get_address(request, dasmig::core::item_seed(42, 1));

// A million results to a file as JSON Lines, never all in memory.
std::ofstream out{"items.jsonl"};
dasmig::core::jsonl_writer lines{out};
gen.for_each_address(request, {.count = 1'000'000, .threads = 0}, 42, lines);
```

- **Item seeds.** Item `i` is exactly `get_address(request, core::item_seed(batch_seed, first + i))`. A range can be generated directly (`first = 1000` starts at item 1000), two batches that follow each other equal one, and the output does not depend on the number of threads.
- **Options.** `core::batch_options` has `count`, `first`, `threads` and `unique`. The request describes one item; the options describe the batch.
- **Uniqueness.** `.unique = core::unique_rule::by<address>(key)` regenerates an item whose key already appeared in the batch, with a deterministic retry seed, and throws `std::runtime_error` naming the item when the attempts run out. A key such as `a.formatted()` makes a batch of distinct addresses.
- **Sinks.** `core::jsonl_writer` writes one `to_json()` per line, `core::csv_writer` flattens each result's JSON into dotted columns. Any callable `(std::uint64_t index, address&)` works.
- Batch calls use the same const getters from several threads, which the thread-safety rules above allow; the CI runs the batch tests under ThreadSanitizer.

## Error Reference

| Exception | When |
|-----------|------|
| `std::runtime_error` | Any generation call before data is loaded; a `full` country's street file that is missing or does not match `streets.manifest.tsv`; a composed street is needed and no `street_types.tsv` and `street_names.tsv` rows exist for the country or for `*` |
| `std::invalid_argument` | A request names a country with no places, or a place the data lacks without a country; `extend(path)` of a file with an unknown name |
| `dasmig::core::tsv_error` | A malformed table file or layer (message gives file, line and column); the data is unchanged |

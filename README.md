# Address Generator for C++

> **Requires C++23** (e.g., `-std=c++23` for GCC/Clang, `/std:c++latest` for MSVC).

[![Address Generator for C++](https://raw.githubusercontent.com/dasmig/address-generator/main/doc/address-generator.png)](https://github.com/dasmig/address-generator/releases)

[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/dasmig/address-generator/main/LICENSE.MIT)
[![CI](https://github.com/dasmig/address-generator/actions/workflows/ci.yml/badge.svg)](https://github.com/dasmig/address-generator/actions/workflows/ci.yml)
[![GitHub Releases](https://img.shields.io/github/release/dasmig/address-generator.svg)](https://github.com/dasmig/address-generator/releases)
[![C++23](https://img.shields.io/badge/standard-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Header-only](https://img.shields.io/badge/type-header--only-green.svg)](https://github.com/dasmig/address-generator#integration)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue.svg)](https://dasmig.github.io/address-generator/)

**[API Reference](https://dasmig.github.io/address-generator/)** · **[Usage Guide](doc/usage.md)** · **[Data and Method](doc/data.md)** · **[Releases](https://github.com/dasmig/address-generator/releases)**

Postal addresses with real streets by place, laid out in each country's format.

## Features

- **Real Streets by Place**. A place's own streets, drawn by how many addresses they have (OpenStreetMap), with a house number in the street's observed range; places without street data get a composed street (`Rue de la Paix`, `Hauptstraße`) instead.

- **Every Country's Layout**. 250 territories laid out in their own order (`12 Rua Augusta` / `1100-053 Lisbon`, `Kirchstraße 84` / `10886 Berlin`, `84 Elm Street` / `Laramie, WY 82071`), with the number before or after the street as the country writes it, postcodes in the country's format and units in the country's style.

- **Streets and Postcodes on Their Own**. `get_street` and `get_postcode` draw from the same streams as `get_address`, so they return the street and postcode of the address with the same seed.

- **Latin Layout and Japan**. `address_request::script` selects the Latin-script layout where a country has one; Japanese addresses are written large to small with block numbers.

- **Anchored to a City**. Pass a city drawn elsewhere (for example by `city-generator`) and the address is in it; the places are the same GeoNames places `city-generator` uses.

- **Deterministic Seeding**. Per-call `get_address(request, seed)` for reproducible results, and `address::seed()` for replaying a previous generation with the same request and data.

- **Batch Generation**. Generate thousands of results from one seed, on several threads with the same output for any thread count, optionally unique by a key, and stream them as JSON Lines or CSV (see the usage guide).

- **Identical Everywhere**. A seed gives the same result on every compiler, standard library and CPU (GCC, Clang, MSVC, Emscripten; x86-64 and ARM64), checked by known-answer tests.

- **Extensible Data**. Load your own TSV layers on top of the shipped data to add, change or remove places, streets or postcodes (`extend()`), checked before they are applied.

- **JSON and Replay**. `to_json()` on every result, and a `signature()` recording the seed and the data it came from.

- **Compiled-in Data**. Define `DASMIG_ADDRESS_EMBED` to build the `lite` tier into the program, for WebAssembly and other builds without files.

- **Multi-Instance Support**. Construct independent `adg` instances with their own data and random engine.

## Integration

[`addressgen.hpp`](https://github.com/dasmig/address-generator/blob/main/dasmig/addressgen.hpp) is the library [released here](https://github.com/dasmig/address-generator/releases). Keep [`core.hpp`](https://github.com/dasmig/address-generator/blob/main/dasmig/core.hpp) and the [`core/`](https://github.com/dasmig/address-generator/tree/main/dasmig/core) directory ([dasmig-core](https://github.com/dasmig/dasmig-core)) in the same directory. Add

```cpp
#include <dasmig/addressgen.hpp>
```

to the files you want to use it in, and supply the [`resources`](https://github.com/dasmig/address-generator/tree/main/resources) folder (or define `DASMIG_ADDRESS_EMBED`).

## Usage

```cpp
#include <dasmig/addressgen.hpp>
#include <iostream>

auto& gen = dasmig::adg::instance(); // loads the lite tier

auto v = gen.get_address(dasmig::address_request{});
std::cout << v << '
';           // 23 Largo do Toural, 4800-019 Guimarães
std::cout << v.to_json() << '
'; // {"country_code":"PT","city":"Guimarães",...,"signature":{...}}

// In a country, or in a city drawn elsewhere.
dasmig::address_request r;
r.country = "FR";
for (const auto& line : gen.get_address(r).lines()) std::cout << line << '
';
r.place = dasmig::place_ref{"Lisbon", ""};

// The same seed gives the same result everywhere.
auto again = gen.get_address(dasmig::address_request{}, v.seed());
```

See the [usage guide](doc/usage.md) for everything else.

## Building

```bash
make            # example
make test       # tests
make bench      # benchmarks, results in build/bench.tsv
make test-embed # tests with the lite tier compiled in
make coverage   # code coverage
make lint       # clang-tidy
make docs       # API docs
make embed      # regenerate dasmig/addressgen_data.hpp from resources/lite
```

## Compiler Support

Tested with:
- Clang 18+ (`-std=c++23`)
- GCC 14+ (`-std=c++23`)
- MSVC 19.38+ (`/std:c++latest`)
- Emscripten 3.1.74

## Dependencies

| Dependency | Version | Bundled | Purpose |
|-----------|---------|---------|---------|
| [dasmig-core](https://github.com/dasmig/dasmig-core) | 1.2 | Yes (`core.hpp`, `core/`) | Platform-independent random engine and distributions, data layers, JSON |
| [Catch2](https://github.com/catchorg/Catch2) | 3.x | Yes (amalgamated) | Unit testing |

## Related Libraries

| Library | Description |
|---------|-------------|
| [biodata-generator](https://github.com/dasmig/biodata-generator) | Procedural human physical characteristics |
| [birth-generator](https://github.com/dasmig/birth-generator) | Demographically plausible birthdays |
| [city-generator](https://github.com/dasmig/city-generator) | Weighted city selection by population |
| [country-generator](https://github.com/dasmig/country-generator) | Weighted country selection by population |
| [device-generator](https://github.com/dasmig/device-generator) | Devices, browsers, user agents and IP addresses that fit together |
| [email-generator](https://github.com/dasmig/email-generator) | Email addresses in each country's style |
| [entity-generator](https://github.com/dasmig/entity-generator) | ECS-based entity generation |
| [interest-generator](https://github.com/dasmig/interest-generator) | Religion, hobbies and favourite media by country, age and sex |
| [name-generator](https://github.com/dasmig/name-generator) | Culturally appropriate full names |
| [nickname-generator](https://github.com/dasmig/nickname-generator) | Gamer-style nicknames |
| [occupation-generator](https://github.com/dasmig/occupation-generator) | Employment status, education and occupation by country, age and sex |
| [persona-generator](https://github.com/dasmig/persona-generator) | Complete characters composed from the other generators |
| [phone-generator](https://github.com/dasmig/phone-generator) | Phone numbers valid in each country's numbering plan |
| [relationship-generator](https://github.com/dasmig/relationship-generator) | Families around a person: parents, siblings, partners and children |
| [trait-generator](https://github.com/dasmig/trait-generator) | Personality traits with conflicts, synergies and Big Five axes |
| [user-generator](https://github.com/dasmig/user-generator) | Complete user records: account, username, email, phone and address |

## License

This library is released under the [MIT License](https://opensource.org/licenses/MIT).

```
MIT License

Copyright (c) 2026 Diego Dasso Migotto
```

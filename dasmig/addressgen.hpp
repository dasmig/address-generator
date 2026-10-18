// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Diego Dasso Migotto

#ifndef DASMIG_ADDRESSGEN_HPP
#define DASMIG_ADDRESSGEN_HPP

#include "core.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef DASMIG_ADDRESS_EMBED
#include "addressgen_data.hpp"
#endif

/// @file addressgen.hpp
/// @brief Address Generator library — postal addresses with real streets by place, laid out in each country's format for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.

DASMIG_CORE_REQUIRE(1, 1);

namespace dasmig
{

/// @brief A place a request can anchor an address to, by plain values.
///
/// `persona-generator` and `user-generator` pass the city they drew.
struct place_ref
{
    std::string city;   ///< The place's name (any case); matched against the loaded places.
    std::string region; ///< Optional region name or code, to tell places of one name apart.
};

/// @brief The script an address is written in.
enum class address_script : std::uint8_t
{
    local, ///< The country's own layout (the default).
    latin  ///< The Latin-script layout, for the countries that have one (`lines_latin`); others keep their own.
};

/// @brief What an address call asks for.
///
/// Every generation call takes one; `address_request{}` asks for the
/// defaults. Choices are never stored on the generator, so one call cannot
/// change the next.
struct address_request
{
    /// @brief ISO-3166 two-letter country code, or a country drawn when absent.
    std::optional<std::string> country{std::nullopt};

    /// @brief Anchor the address to this place instead of drawing one.
    ///
    /// A place the data does not know is used as given (its street comes from
    /// the composed fallback and its postcode from the country's pattern),
    /// which needs `country`.
    std::optional<place_ref> place{std::nullopt};

    /// @brief Weighted selection (default): countries and places by
    ///        population, streets by address count. False selects uniformly.
    bool weighted{true};

    /// @brief Chance of a unit (apartment, flat) in the address; the
    ///        country's default when absent. Countries without a unit style
    ///        never have one.
    std::optional<double> unit_chance{std::nullopt};

    /// @brief The script of the layout. Street names are written as OpenStreetMap
    ///        has them and place names in Latin, whatever the script.
    address_script script{address_script::local};
};

/// @brief What a street call asks for: the street an address in that place
///        would have. Same fields as address_request, without the unit and script.
struct street_request
{
    std::optional<std::string> country{std::nullopt}; ///< ISO 3166-1 alpha-2, or drawn when absent.
    std::optional<place_ref> place{std::nullopt};     ///< Anchor to this place instead of drawing one.
    bool weighted{true};                              ///< Streets by address count; false is uniform.
};

/// @brief What a postcode call asks for.
struct postcode_request
{
    std::optional<std::string> country{std::nullopt}; ///< ISO 3166-1 alpha-2, or drawn when absent.
    std::optional<place_ref> place{std::nullopt};     ///< Anchor to this place instead of drawing one.
    bool weighted{true};                              ///< Postcodes by their weight; false is uniform.
};

/// @brief Return type for address generation.
///
/// Supports implicit conversion to std::string (the address on one line),
/// streaming via operator<< and JSON output via to_json().
class address
{
  public:
    std::string country_code; ///< ISO 3166-1 alpha-2.
    std::string city;         ///< The place's name.
    std::string region;       ///< The region as the country writes it (an abbreviation in the US).
    std::string street;       ///< The street's name, with its type.
    std::uint32_t number{0};  ///< House number; 0 when the street has none.
    std::string unit;         ///< Apartment or flat, empty when there is none.
    std::string postcode;     ///< In the country's format; empty where there are no postcodes.
    bool real_street{false};  ///< True when the street comes from the place's own data, false when composed.
    std::uint32_t geonameid{0}; ///< The place's GeoNames id (city-generator's); 0 for a place given by name only.

    /// @brief The address lines in the country's order.
    /// @return One string per line, without the recipient lines.
    [[nodiscard]] const std::vector<std::string>& lines() const { return _lines; }

    /// @brief The address on one line.
    /// @param separator Placed between the lines.
    /// @return The lines joined.
    [[nodiscard]] std::string formatted(std::string_view separator = ", ") const
    {
        std::string out;
        for (const auto& l : _lines)
        {
            if (!out.empty()) out += separator;
            out += l;
        }
        return out;
    }

    /// @brief Retrieve the random seed used to generate this address.
    /// @return The call seed; pass it to get_address(request, seed) with
    ///         the same request and data to replay this address.
    [[nodiscard]] std::uint64_t seed() const { return _signature.seed; }

    /// @brief The seed and the data this address was generated from.
    /// @return The replay signature; `data` is adg::data_hash() at the
    ///         time of generation.
    [[nodiscard]] const core::seed_signature& signature() const
    {
        return _signature;
    }

    /// @brief Human-readable summary string.
    /// @return The address on one line.
    [[nodiscard]] std::string to_string() const { return formatted(", "); }

    /// @brief Write this address as a JSON object.
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const
    {
        w.begin_object()
            .field("country_code", country_code)
            .field("city", city)
            .field("region", region)
            .field("street", street)
            .field("number", number)
            .field("unit", unit)
            .field("postcode", postcode)
            .field("real_street", real_street)
            .field("geonameid", geonameid)
            .key("lines")
            .begin_array();
        for (const auto& l : _lines) w.value(l);
        w.end_array().field("formatted", formatted()).key("signature");
        _signature.to_json(w);
        w.end_object();
    }

    /// @brief This address as a JSON document.
    /// @return Compact JSON.
    [[nodiscard]] std::string to_json() const
    {
        core::json_writer w;
        to_json(w);
        return w.str();
    }

    /// @brief Implicit conversion to std::string.
    /// @return Summary string.
    operator std::string() const // NOLINT(hicpp-explicit-conversions)
    {
        return to_string();
    }

    /// @brief Stream the summary to an output stream.
    /// @param os Output stream.
    /// @param v The address to stream.
    /// @return The stream.
    friend std::ostream& operator<<(std::ostream& os, const address& v)
    {
        os << v.to_string();
        return os;
    }

  private:
    std::vector<std::string> _lines;
    std::string _number_text; // when the number is not a plain integer (Japan's blocks)
    core::seed_signature _signature;
    friend class adg;
};

/// @brief Return type for street generation: a street of a place.
///
/// Converts to the street's name, streams as it and writes JSON.
class street
{
  public:
    std::string name;           ///< The street's name, with its type.
    std::string country_code;   ///< ISO 3166-1 alpha-2.
    std::string city;           ///< The place the street is in.
    bool real_street{false};    ///< True when it comes from the place's own data, false when composed.
    std::uint32_t geonameid{0}; ///< The place's GeoNames id; 0 for a place given by name only.

    /// @brief Retrieve the random seed used to generate this street.
    /// @return The call seed.
    [[nodiscard]] std::uint64_t seed() const { return _signature.seed; }

    /// @brief The seed and the data this street was generated from.
    /// @return The replay signature.
    [[nodiscard]] const core::seed_signature& signature() const { return _signature; }

    /// @brief Write this street as a JSON object.
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const
    {
        w.begin_object()
            .field("name", name)
            .field("country_code", country_code)
            .field("city", city)
            .field("real_street", real_street)
            .field("geonameid", geonameid)
            .key("signature");
        _signature.to_json(w);
        w.end_object();
    }

    /// @brief This street as a JSON document.
    /// @return Compact JSON.
    [[nodiscard]] std::string to_json() const
    {
        core::json_writer w;
        to_json(w);
        return w.str();
    }

    /// @brief Implicit conversion to std::string.
    /// @return The street's name.
    operator std::string() const { return name; } // NOLINT(hicpp-explicit-conversions)

    /// @brief Stream the name to an output stream.
    /// @param os Output stream.
    /// @param v The street to stream.
    /// @return The stream.
    friend std::ostream& operator<<(std::ostream& os, const street& v)
    {
        os << v.name;
        return os;
    }

  private:
    core::seed_signature _signature;
    friend class adg;
};

/// @brief The tables of the address data, for extending it.
enum class address_table : std::uint8_t
{
    places,       ///< `places.tsv`: the places addresses are drawn in.
    formats,      ///< `formats.tsv`: each country's layout and postcode format.
    streets,      ///< `streets.tsv`: real streets by place, weighted by address count.
    postcodes,    ///< `postcodes.tsv`: postcodes by place.
    street_types, ///< `street_types.tsv`: types for composed streets.
    street_names  ///< `street_names.tsv`: names for composed streets.
};

/// @brief Address Generator: postal addresses with real streets by place.
///
/// A call draws a country (by population), a place in it (by population),
/// a street of that place (by address count), a house number, a unit and a
/// postcode, each from a stream split from the call seed (`"country"`,
/// `"place"`, `"street"`, `"number"`, `"unit"`, `"postcode"`), and lays the
/// address out in the country's order. Places without street data get a
/// composed street. Output is identical on every compiler and platform
/// (dasmig-core).
///
/// Can be used as a singleton via instance() or constructed
/// independently.
///
/// @par Thread safety
/// Each instance is independent.  Concurrent calls to get_address(request, seed) on the
/// **same** instance are safe, including the first read of a `full` country's
/// street file; calls that use the instance engine, loading and extending
/// require external synchronisation.
class adg
{
  public:
    /// @brief Default constructor — creates an empty generator with no
    ///        data.
    adg() : _tables{make_tables_()} {}

    adg(const adg&) = delete;            ///< Not copyable.
    adg& operator=(const adg&) = delete; ///< Not copyable.
    adg(adg&&) noexcept = default;       ///< Movable.
    adg& operator=(adg&&) noexcept = default; ///< Movable.
    ~adg() = default;                       ///< Destructor.

    /// @brief Access the global singleton instance.
    ///
    /// Loads the `lite` tier (or `full` if `lite` is missing) on first
    /// access, from compiled-in data or an auto-probed `resources/`.
    /// @return The singleton.
    static adg& instance()
    {
        static adg inst{auto_probe_tag{}};
        return inst;
    }

    // -- Generation -------------------------------------------------------

    /// @brief Generate a random address.
    ///        Use address_request{} for the defaults.
    /// @param request What to generate.
    /// @return The address.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If the request names an unknown country
    ///         or place.
    [[nodiscard]] address get_address(const address_request& request)
    {
        return get_address(request, draw_seed_());
    }

    /// @brief Generate a deterministic address.
    /// @param request What to generate.
    /// @param call_seed Seed; the same request, seed and data give the same
    ///        result. Does not touch the instance engine.
    /// @return The address.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If the request names an unknown country
    ///         or place.
    [[nodiscard]] address get_address(const address_request& request, std::uint64_t call_seed) const
    {
        require_data_();
        const core::rng call{call_seed};
        address v;
        v._signature = {.seed = call_seed, .data = data_hash()};

        // Country and place.
        const resolved r = resolve_place_(request.country, request.place, request.weighted, call);
        const std::string& country = r.country;
        v.geonameid = r.id;
        const format_data& fmt = format_of_(country);

        v.country_code = country;
        v.city = r.city;
        v.region = region_text_(fmt, r.region_code, r.region);

        // Street and number.
        auto sr = call.split("street");
        const street_pick pick = pick_street_(r, request.weighted, sr);
        v.street = pick.name;
        v.real_street = pick.real;
        auto nr = call.split("number");
        if (fmt.number_blocks)
        {
            // Japan: chome-ban-go.
            v.number = core::uniform_int<std::uint32_t>(nr, 1, 9);
            // Drawn in statements: the order of a call's arguments is unspecified.
            const int ban = core::uniform_int<int>(nr, 1, 30);
            const int go = core::uniform_int<int>(nr, 1, 20);
            v._number_text = std::format("{}-{}-{}", v.number, ban, go);
        }
        else
        {
            v.number = house_number_(nr, pick.lo, pick.hi);
        }

        // Unit.
        auto ur = call.split("unit");
        const double chance = unit_chance_(request, fmt, r.population);
        if (core::bernoulli(ur, chance)) v.unit = unit_text_(fmt.unit_style, ur);

        // Postcode.
        auto zr = call.split("postcode");
        v.postcode = pick_postcode_(r, fmt, zr);

        v._lines = layout_(fmt, v, request.script);
        return v;
    }

    // -- Streets and postcodes -----------------------------------------------

    /// @brief Generate a random street. Use street_request{} for the defaults.
    /// @param request What to generate.
    /// @return The street.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If the request names an unknown country or place.
    [[nodiscard]] street get_street(const street_request& request)
    {
        return get_street(request, draw_seed_());
    }

    /// @brief Generate a deterministic street.
    ///
    /// Draws from the same streams as get_address(), so for the same request
    /// fields and seed it is the street of that address.
    ///
    /// @param request What to generate.
    /// @param call_seed Seed; does not touch the instance engine.
    /// @return The street.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If the request names an unknown country or place.
    [[nodiscard]] street get_street(const street_request& request, std::uint64_t call_seed) const
    {
        require_data_();
        const core::rng call{call_seed};
        street v;
        v._signature = {.seed = call_seed, .data = data_hash()};
        const resolved r = resolve_place_(request.country, request.place, request.weighted, call);
        auto sr = call.split("street");
        const street_pick pick = pick_street_(r, request.weighted, sr);
        v.name = pick.name;
        v.real_street = pick.real;
        v.country_code = r.country;
        v.city = r.city;
        v.geonameid = r.id;
        return v;
    }

    /// @brief Generate a batch of streets from a seed drawn like get_street() draws its own.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<street> get_street_batch(const street_request& request,
                                                       const core::batch_options& options)
    {
        return get_street_batch(request, options, draw_seed_());
    }

    /// @brief Generate a batch of streets, deterministically.
    ///
    /// Item `i` is `get_street(request, core::item_seed(batch_seed, options.first + i))`.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness (a rule made for `street`).
    /// @param batch_seed Seed of the whole batch.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<street> get_street_batch(const street_request& request,
                                                       const core::batch_options& options,
                                                       std::uint64_t batch_seed) const
    {
        return core::run_batch(options, batch_seed, [&](std::uint64_t item_seed)
                               { return get_street(request, item_seed); });
    }

    /// @brief Stream a batch of streets to a sink without holding the whole batch.
    /// @tparam Sink Callable taking `(std::uint64_t, street&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @param sink Receives the items, in index order, on the calling thread.
    template <typename Sink>
    void for_each_street(const street_request& request, const core::batch_options& options,
                         std::uint64_t batch_seed, Sink&& sink) const
    {
        core::for_each_batch(options, batch_seed,
                             [&](std::uint64_t item_seed) { return get_street(request, item_seed); },
                             std::forward<Sink>(sink));
    }

    /// @brief Stream a batch of streets with a batch seed drawn like get_street() draws its own.
    /// @tparam Sink Callable taking `(std::uint64_t, street&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_street(const street_request& request, const core::batch_options& options, Sink&& sink)
    {
        for_each_street(request, options, draw_seed_(), std::forward<Sink>(sink));
    }

    /// @brief Generate a random postcode in the country's format. Use
    ///        postcode_request{} for the defaults.
    /// @param request What to generate.
    /// @return The postcode; empty for a country without postcodes.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If the request names an unknown country or place.
    [[nodiscard]] std::string get_postcode(const postcode_request& request)
    {
        return get_postcode(request, draw_seed_());
    }

    /// @brief Generate a deterministic postcode.
    ///
    /// Draws from the same streams as get_address(), so for the same request
    /// fields and seed it is the postcode of that address.
    ///
    /// @param request What to generate.
    /// @param call_seed Seed; does not touch the instance engine.
    /// @return The postcode; empty for a country without postcodes.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If the request names an unknown country or place.
    [[nodiscard]] std::string get_postcode(const postcode_request& request, std::uint64_t call_seed) const
    {
        require_data_();
        const core::rng call{call_seed};
        const resolved r = resolve_place_(request.country, request.place, request.weighted, call);
        auto zr = call.split("postcode");
        return pick_postcode_(r, format_of_(r.country), zr);
    }

    /// @brief Generate a batch of postcodes from a seed drawn like get_postcode() draws its own.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<std::string> get_postcode_batch(const postcode_request& request,
                                                              const core::batch_options& options)
    {
        return get_postcode_batch(request, options, draw_seed_());
    }

    /// @brief Generate a batch of postcodes, deterministically.
    ///
    /// Item `i` is `get_postcode(request, core::item_seed(batch_seed, options.first + i))`.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness (a rule made for `std::string`).
    /// @param batch_seed Seed of the whole batch.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<std::string> get_postcode_batch(const postcode_request& request,
                                                              const core::batch_options& options,
                                                              std::uint64_t batch_seed) const
    {
        return core::run_batch(options, batch_seed, [&](std::uint64_t item_seed)
                               { return get_postcode(request, item_seed); });
    }

    /// @brief Stream a batch of postcodes to a sink without holding the whole batch.
    /// @tparam Sink Callable taking `(std::uint64_t, std::string&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @param sink Receives the items, in index order, on the calling thread.
    template <typename Sink>
    void for_each_postcode(const postcode_request& request, const core::batch_options& options,
                           std::uint64_t batch_seed, Sink&& sink) const
    {
        core::for_each_batch(options, batch_seed,
                             [&](std::uint64_t item_seed) { return get_postcode(request, item_seed); },
                             std::forward<Sink>(sink));
    }

    /// @brief Stream a batch of postcodes with a batch seed drawn like get_postcode() draws its own.
    /// @tparam Sink Callable taking `(std::uint64_t, std::string&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_postcode(const postcode_request& request, const core::batch_options& options, Sink&& sink)
    {
        for_each_postcode(request, options, draw_seed_(), std::forward<Sink>(sink));
    }

    // -- Batch generation -------------------------------------------------

    /// @brief Generate a batch of address results from a seed drawn like
    ///        get_address() draws its own.
    ///
    /// Item `i` is `get_address(request, core::item_seed(batch_seed, options.first + i))`
    /// for the drawn batch seed; see the seeded overload.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<address> get_address_batch(const address_request& request,
                                                  const core::batch_options& options)
    {
        return get_address_batch(request, options, draw_seed_());
    }

    /// @brief Generate a batch of address results, deterministically.
    ///
    /// Item `i` is exactly `get_address(request, core::item_seed(batch_seed,
    /// options.first + i))`, so an item replays on its own, a range can be
    /// generated directly, two batches that follow each other equal one, and
    /// the output does not depend on `options.threads`.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness (see
    ///        core::unique_rule; the rule must be made for `address`).
    /// @param batch_seed Seed of the whole batch.
    /// @return The results, in index order.
    /// @throws std::invalid_argument If the request or options are invalid.
    /// @throws std::runtime_error If no data has been loaded, or a unique
    ///         batch runs out of attempts.
    [[nodiscard]] std::vector<address> get_address_batch(const address_request& request,
                                                  const core::batch_options& options,
                                                  std::uint64_t batch_seed) const
    {
        return core::run_batch(options, batch_seed, [&](std::uint64_t item_seed)
                               { return get_address(request, item_seed); });
    }

    /// @brief Stream a batch of address results to a sink without holding the
    ///        whole batch.
    ///
    /// Generates the same items as get_address_batch() and calls
    /// `sink(index, item)` in index order on the calling thread; `index`
    /// includes `options.first`.
    ///
    /// @tparam Sink Callable taking `(std::uint64_t, address&)`; for example
    ///         core::jsonl_writer or core::csv_writer.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_address(const address_request& request, const core::batch_options& options,
                        std::uint64_t batch_seed, Sink&& sink) const
    {
        core::for_each_batch(options, batch_seed,
                             [&](std::uint64_t item_seed) { return get_address(request, item_seed); },
                             std::forward<Sink>(sink));
    }

    /// @brief Stream a batch of address results with a batch seed drawn like
    ///        get_address() draws its own.
    /// @tparam Sink Callable taking `(std::uint64_t, address&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_address(const address_request& request, const core::batch_options& options,
                        Sink&& sink)
    {
        for_each_address(request, options, draw_seed_(), std::forward<Sink>(sink));
    }

    // -- Data management --------------------------------------------------

    /// @brief Check whether any data has been loaded.
    /// @return True when places are loaded.
    [[nodiscard]] bool has_data() const { return !_model.places.empty(); }

    /// @brief Return the number of loaded places.
    /// @return The count.
    [[nodiscard]] std::size_t count() const { return _model.places.size(); }

    /// @brief The countries that have places, sorted.
    /// @return ISO codes.
    [[nodiscard]] const std::vector<std::string>& countries() const { return _model.codes; }

    /// @brief Whether a country has real streets for at least one place.
    /// @param code ISO 3166-1 alpha-2.
    /// @return True when street data is loaded for the country.
    [[nodiscard]] bool has_streets(std::string_view code) const
    {
        const auto it = _model.countries.find(code);
        return it != _model.countries.end() && it->second.has_streets;
    }

    /// @brief Load the street data of one country now, instead of on the first
    ///        call that needs it.
    ///
    /// Only the `full` tier keeps its streets in one file per country; for the
    /// other data this does nothing. Safe to call from several threads.
    ///
    /// @param code ISO 3166-1 alpha-2.
    /// @return True when the country has real streets.
    /// @throws std::runtime_error If the country's file is missing or does not
    ///         match `streets.manifest.tsv`.
    /// @throws core::tsv_error If the file is malformed.
    bool load_streets(std::string_view code) const // NOLINT(modernize-use-nodiscard)
    {
        if (!_shard_cfg.active) return has_streets(code);
        const shard* s = shard_of_(code);
        return s != nullptr && !s->streets.empty();
    }

    /// @brief Whether a country's street data is in memory.
    /// @param code ISO 3166-1 alpha-2.
    /// @return True for the `lite` data (always loaded) and for a `full`
    ///         country once a call or load_streets() has read its file.
    [[nodiscard]] bool streets_loaded(std::string_view code) const
    {
        if (!_shard_cfg.active) return has_streets(code);
        const auto it = _shards.find(code);
        return it != _shards.end() && it->second->loaded.load();
    }

    /// @brief Load data from a resource directory, replacing all data and
    ///        layers loaded before.
    ///
    /// The directory must contain `places.tsv`; the other tables are read
    /// when present. A path without `places.tsv` is ignored.
    ///
    /// @param dir Directory such as `resources/lite`.
    /// @throws core::tsv_error If a file is malformed; the data is then
    ///         unchanged.
    void load(const std::filesystem::path& dir)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec) ||
            !std::filesystem::is_regular_file(dir / file_name_(address_table::places), ec))
        {
            return;
        }
        auto docs = read_directory_(dir);
        std::optional<shard_config> shards;
        const auto manifest = dir / manifest_name_;
        if (!docs.at(index_(table::streets)) && std::filesystem::is_regular_file(manifest, ec))
        {
            shards = make_shard_config_(core::tsv_document::load(manifest), dir, "");
        }
        apply_(std::move(docs), core::layer_mode::replace, true, std::move(shards));
    }

    /// @brief Load a dataset tier, replacing all data and layers loaded
    ///        before.
    ///
    /// Uses the compiled-in copy when the library is built with
    /// `DASMIG_ADDRESS_EMBED`, otherwise searches `resources/`, `../resources`,
    /// `address-generator/resources` and `../address-generator/resources`.
    ///
    /// @param tier The tier.
    /// @return False when the tier was not found.
    /// @throws core::tsv_error If a file is malformed.
    [[nodiscard]] bool load(dataset tier)
    {
        const std::string tier_dir{core::tier_name(tier)};
        const auto marker = tier_dir + "/" + std::string{file_name_(address_table::places)};
        const auto root = core::find_resources(library_name_, marker);
        documents docs;
        for (const auto t : all_tables_)
        {
            docs.at(index_(t)) = core::load_resource(tier_dir + "/" + std::string{file_name_(t)},
                                                     embedded_files_(), root);
        }
        if (!docs.at(index_(address_table::places))) return false;
        std::optional<shard_config> shards;
        if (!docs.at(index_(address_table::streets)))
        {
            if (auto manifest = core::load_resource(tier_dir + "/" + std::string{manifest_name_},
                                                    embedded_files_(), root))
            {
                shards = make_shard_config_(*manifest, root, tier_dir + "/");
            }
        }
        apply_(std::move(docs), core::layer_mode::replace, true, std::move(shards));
        return true;
    }

    /// @brief Add data layers from a directory or a file.
    ///
    /// A directory may hold any of the table files (see address_table); all
    /// are applied together or, if any fails, none. A file must have one of
    /// those names.
    ///
    /// @param path Directory or file.
    /// @param mode How the layers combine with the data loaded before.
    /// @throws core::tsv_error If a file cannot be read or a row is
    ///         invalid; the data is then unchanged.
    /// @throws std::invalid_argument If @p path is a file with another name.
    void extend(const std::filesystem::path& path, core::layer_mode mode = core::layer_mode::override)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec))
        {
            apply_(read_directory_(path), mode, false);
            return;
        }
        for (const auto t : all_tables_)
        {
            if (path.filename() == file_name_(t))
            {
                (void)extend(t, core::tsv_document::load(path), mode);
                return;
            }
        }
        throw std::invalid_argument("extend: expected places.tsv, formats.tsv, streets.tsv, postcodes.tsv, "
                                    "street_types.tsv or street_names.tsv, got " + path.filename().string());
    }

    /// @brief Add a data layer to one table.
    ///
    /// Rows are keyed by the table's key columns (see doc/data.md). In the
    /// default override mode a row replaces the loaded row with the same key
    /// and new keys are added; a row with `disabled` set to 1 removes its key.
    ///
    /// @param which The table.
    /// @param doc TSV with that table's columns.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did (rows added, replaced, removed, warnings).
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend(address_table which, core::tsv_document doc,
                                   core::layer_mode mode = core::layer_mode::override)
    {
        documents docs;
        docs.at(index_(which)).emplace(std::move(doc));
        apply_(std::move(docs), mode, false);
        return _tables.at(index_(which)).layers().back();
    }

    /// @brief Add a data layer to one table from TSV text (for example
    ///        from a WebAssembly host).
    /// @param which The table.
    /// @param text TSV with that table's columns.
    /// @param source Name used in errors.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend_from_string(address_table which, std::string_view text, std::string source,
                                               core::layer_mode mode = core::layer_mode::override)
    {
        return extend(which, core::tsv_document::parse(text, std::move(source)), mode);
    }

    /// @brief The layers in effect for one table, in order.
    /// @param which The table.
    /// @return The layer records.
    [[nodiscard]] const std::vector<core::layer_info>& layers(address_table which) const
    {
        return _tables.at(index_(which)).layers();
    }

    /// @brief Hash of the loaded data (all tables), recorded in each
    ///        result's signature().
    /// @return The hash; 0 when nothing is loaded.
    [[nodiscard]] std::uint64_t data_hash() const
    {
        std::uint64_t h = 0;
        bool any = false;
        for (const auto& t : _tables)
        {
            any = any || !t.layers().empty();
            h = core::hash_combine(h, t.data_hash());
        }
        // The per-country street files are read on demand, so the manifest
        // stands for them: a seed replays whether or not a country is loaded.
        if (_shard_cfg.active) h = core::hash_combine(h, _shard_cfg.hash);
        return any ? h : 0;
    }

  private:
    using table = address_table;

    // -- Tables -------------------------------------------------------------

    static constexpr std::string_view library_name_ = "address-generator";
    static constexpr std::string_view manifest_name_ = "streets.manifest.tsv";
    static constexpr std::size_t table_count_ = 6;
    static constexpr std::array<table, table_count_> all_tables_{table::places,       table::formats,
                                                                 table::streets,      table::postcodes,
                                                                 table::street_types, table::street_names};

    using documents = std::array<std::optional<core::tsv_document>, table_count_>;
    using tables = std::array<core::layered_table, table_count_>;

    static constexpr std::size_t index_(table t) { return static_cast<std::size_t>(t); }

    static constexpr std::string_view file_name_(table t)
    {
        switch (t)
        {
        case table::places: return "places.tsv";
        case table::formats: return "formats.tsv";
        case table::streets: return "streets.tsv";
        case table::postcodes: return "postcodes.tsv";
        case table::street_types: return "street_types.tsv";
        case table::street_names: return "street_names.tsv";
        }
        return "";
    }

    static core::table_schema schema_(table t)
    {
        switch (t)
        {
        case table::places:
            return {.key = {"geonameid"},
                    .required = {"country_code", "name", "population"},
                    .optional = {"region_code", "region"}};
        case table::formats:
            return {.key = {"country_code"},
                    .required = {"lines", "number_after"},
                    .optional = {"lines_latin", "require", "region_style", "zip_pattern", "zip_completion",
                                 "zip_examples", "number_sep", "unit_style", "number_style"}};
        case table::streets:
            return {.key = {"geonameid", "name"},
                    .required = {"country_code", "weight"},
                    .optional = {"class", "min_number", "max_number"}};
        case table::postcodes:
            return {.key = {"geonameid", "postcode"}, .required = {"country_code", "weight"}, .optional = {"partial"}};
        case table::street_types:
            return {.key = {"country_code", "type"}, .required = {"position", "weight"}, .optional = {}};
        case table::street_names:
            return {.key = {"country_code", "name"}, .required = {"weight"}, .optional = {}};
        }
        return {};
    }

    static tables make_tables_()
    {
        return {core::layered_table{schema_(table::places)},       core::layered_table{schema_(table::formats)},
                core::layered_table{schema_(table::streets)},      core::layered_table{schema_(table::postcodes)},
                core::layered_table{schema_(table::street_types)}, core::layered_table{schema_(table::street_names)}};
    }

    // -- Parsed rows --------------------------------------------------------

    static constexpr std::size_t none_ = std::numeric_limits<std::size_t>::max();

    struct place_data
    {
        std::uint32_t id{0};
        std::string country;
        std::string name;
        std::string region_code;
        std::string region;
        std::uint64_t population{0};
    };

    enum class position : std::uint8_t { before, after, suffix };

    struct format_data
    {
        std::vector<std::string> lines;       // templates with {A} {C} {S} {Z} {D} {X}
        std::vector<std::string> lines_latin; // the same in Latin script, empty when there is none
        bool number_blocks{false};            // Japan: 3-12-5
        bool region_as_code{false};
        std::string zip_pattern;
        std::string zip_completion;
        std::vector<std::string> zip_examples;
        bool number_after{false};
        bool number_comma{false};
        std::string unit_style;
    };

    struct street_data
    {
        std::string name;
        std::uint32_t min_number{0};
        std::uint32_t max_number{0};
    };

    struct place_streets
    {
        std::vector<street_data> streets;
        core::weighted_table<std::size_t> weights; // street index, by address count
    };

    // `full` keeps streets in one file per country, `streets/<CC>.tsv`, read on
    // the first call that needs the country. Layers added with extend() are
    // replayed over a country's file when it is read.
    struct shard_entry
    {
        std::size_t rows{0};
        std::uint64_t hash{0}; // tsv_document::content_hash() of the country's file
    };

    struct layer_doc
    {
        core::tsv_document doc;
        core::layer_mode mode;
    };

    struct shard_config
    {
        bool active{false};
        std::optional<std::filesystem::path> root; // resources directory; empty when compiled in
        std::string prefix;                        // path below root, e.g. "full/"
        std::map<std::string, shard_entry, std::less<>> entries;
        std::uint64_t hash{0}; // of streets.manifest.tsv
        std::vector<layer_doc> layers;
    };

    struct shard
    {
        std::once_flag once;
        std::atomic<bool> loaded{false};
        std::unordered_map<std::uint32_t, place_streets> streets;
    };

    using shard_map = std::map<std::string, std::unique_ptr<shard>, std::less<>>;

    struct postcode_data
    {
        std::string code;
        bool partial{false};
    };

    struct place_postcodes
    {
        std::vector<postcode_data> codes;
        core::weighted_table<std::size_t> weights;
    };

    struct type_data
    {
        std::string type;
        position pos{position::before};
    };

    struct country_data
    {
        std::vector<std::size_t> places;                 // indices into model.places
        core::weighted_table<std::size_t> place_weights; // place index, by population
        double population{0.0};
        format_data format;
        bool has_format{false};
        bool has_streets{false};
        std::vector<type_data> types;
        core::weighted_table<std::size_t> type_weights; // index into types
        std::vector<std::string> names;
        core::weighted_table<std::size_t> name_weights; // index into names
    };

    struct model
    {
        std::vector<place_data> places; // in table row order
        std::unordered_map<std::uint32_t, std::size_t> place_of_id;
        std::unordered_multimap<std::string, std::size_t> places_by_name; // lower-cased ASCII
        std::map<std::string, country_data, std::less<>> countries;
        std::vector<std::string> codes;
        core::weighted_table<std::string> country_weights; // by population
        format_data generic_format;
        std::unordered_map<std::uint32_t, place_streets> streets;
        std::unordered_map<std::uint32_t, place_postcodes> postcodes;
    };

    static bool is_code_(std::string_view s)
    {
        return s.size() == 2 && s[0] >= 'A' && s[0] <= 'Z' && s[1] >= 'A' && s[1] <= 'Z';
    }

    static void check_code_(const core::tsv_row& row)
    {
        if (!is_code_(row["country_code"]))
        {
            throw row.error("country_code", "expected two capital letters (ISO 3166-1 alpha-2)");
        }
    }

    static std::uint32_t parse_id_(const core::tsv_row& row)
    {
        const auto id = row.get<std::int64_t>("geonameid");
        if (id <= 0 || std::cmp_greater(id, std::numeric_limits<std::uint32_t>::max()))
        {
            throw row.error("geonameid", "must be a positive 32-bit id");
        }
        return static_cast<std::uint32_t>(id);
    }

    static double parse_weight_(const core::tsv_row& row)
    {
        const auto w = row.get<double>("weight");
        if (!(w >= 0.0)) throw row.error("weight", "must not be negative");
        return w;
    }

    static place_data parse_place_(const core::tsv_row& row)
    {
        check_code_(row);
        place_data p;
        p.id = parse_id_(row);
        p.country = row.get<std::string>("country_code");
        p.name = row.get<std::string>("name");
        if (p.name.empty()) throw row.error("name", "must not be empty");
        p.region_code = row.get_or<std::string>("region_code", "");
        p.region = row.get_or<std::string>("region", "");
        const auto pop = row.get<std::int64_t>("population");
        if (pop < 0) throw row.error("population", "must not be negative");
        p.population = static_cast<std::uint64_t>(pop);
        return p;
    }

    static std::vector<std::string> split_(std::string_view text, char sep)
    {
        std::vector<std::string> out;
        std::size_t start = 0;
        while (start <= text.size())
        {
            const auto end = text.find(sep, start);
            const auto stop = end == std::string_view::npos ? text.size() : end;
            if (stop > start) out.emplace_back(text.substr(start, stop - start));
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
        return out;
    }

    static std::pair<std::string, format_data> parse_format_(const core::tsv_row& row)
    {
        check_code_(row);
        format_data f;
        f.lines = split_(row.get<std::string>("lines"), '|');
        if (f.lines.empty()) throw row.error("lines", "must have at least one line");
        f.lines_latin = split_(row.get_or<std::string>("lines_latin", ""), '|');
        const auto number_style = row.get_or<std::string>("number_style", "");
        if (!number_style.empty() && number_style != "plain" && number_style != "blocks")
        {
            throw row.error("number_style", "expected plain or blocks");
        }
        f.number_blocks = number_style == "blocks";
        f.region_as_code = row.get_or<std::string>("region_style", "name") == "code";
        f.zip_pattern = row.get_or<std::string>("zip_pattern", "");
        f.zip_completion = row.get_or<std::string>("zip_completion", "");
        f.zip_examples = split_(row.get_or<std::string>("zip_examples", ""), '~');
        const auto after = row.get<int>("number_after");
        if (after != 0 && after != 1) throw row.error("number_after", "expected 0 or 1");
        f.number_after = after == 1;
        const auto sep = row.get_or<std::string>("number_sep", "space");
        if (sep != "space" && sep != "comma") throw row.error("number_sep", "expected space or comma");
        f.number_comma = sep == "comma";
        f.unit_style = row.get_or<std::string>("unit_style", "none");
        return {row.get<std::string>("country_code"), std::move(f)};
    }

    struct street_row
    {
        std::uint32_t id{0};
        std::string country;
        street_data street;
        double weight{0.0};
    };

    static shard_config make_shard_config_(const core::tsv_document& manifest,
                                           std::optional<std::filesystem::path> root, std::string prefix)
    {
        shard_config c;
        c.active = true;
        c.root = std::move(root);
        c.prefix = std::move(prefix);
        c.hash = manifest.content_hash();
        for (const auto& row : manifest.rows())
        {
            check_code_(row);
            const auto rows = row.get<std::int64_t>("rows");
            if (rows < 0) throw row.error("rows", "must not be negative");
            const auto text = row["hash"];
            std::uint64_t h = 0;
            bool valid = !text.empty() && text.size() <= 16;
            for (const char d : text)
            {
                const auto v = std::string_view{"0123456789abcdef"}.find(d);
                valid = valid && v != std::string_view::npos;
                if (valid) h = (h << 4U) | v;
            }
            if (!valid) throw row.error("hash", "expected the file's content hash in hexadecimal (up to 16 digits)");
            c.entries[row.get<std::string>("country_code")] = {.rows = static_cast<std::size_t>(rows), .hash = h};
        }
        return c;
    }

    static street_row parse_street_(const core::tsv_row& row)
    {
        check_code_(row);
        street_row s;
        s.id = parse_id_(row);
        s.country = row.get<std::string>("country_code");
        s.street.name = row.get<std::string>("name");
        if (s.street.name.empty()) throw row.error("name", "must not be empty");
        s.weight = parse_weight_(row);
        const auto lo = row.get_or<int>("min_number", 0);
        const auto hi = row.get_or<int>("max_number", 0);
        if (lo < 0 || hi < lo) throw row.error("max_number", "must not be below min_number, and numbers not negative");
        s.street.min_number = static_cast<std::uint32_t>(lo);
        s.street.max_number = static_cast<std::uint32_t>(hi);
        return s;
    }

    struct postcode_row
    {
        std::uint32_t id{0};
        postcode_data code;
        double weight{0.0};
    };

    static postcode_row parse_postcode_(const core::tsv_row& row)
    {
        check_code_(row);
        postcode_row p;
        p.id = parse_id_(row);
        p.code.code = row.get<std::string>("postcode");
        if (p.code.code.empty()) throw row.error("postcode", "must not be empty");
        p.code.partial = row.get_or<int>("partial", 0) == 1;
        p.weight = parse_weight_(row);
        return p;
    }

    static std::pair<type_data, double> parse_type_(const core::tsv_row& row, std::string& country)
    {
        if (row["country_code"] != "*") check_code_(row);
        country = row.get<std::string>("country_code");
        type_data t;
        t.type = row.get<std::string>("type");
        if (t.type.empty()) throw row.error("type", "must not be empty");
        const auto pos = row["position"];
        if (pos == "before") t.pos = position::before;
        else if (pos == "after") t.pos = position::after;
        else if (pos == "suffix") t.pos = position::suffix;
        else throw row.error("position", std::format("expected before, after or suffix, got '{}'", pos));
        return {std::move(t), parse_weight_(row)};
    }

    static void validate_(table t, const core::tsv_row& row)
    {
        std::string scratch;
        switch (t)
        {
        case table::places: (void)parse_place_(row); break;
        case table::formats: (void)parse_format_(row); break;
        case table::streets: (void)parse_street_(row); break;
        case table::postcodes: (void)parse_postcode_(row); break;
        case table::street_types: (void)parse_type_(row, scratch); break;
        case table::street_names:
            if (row["country_code"] != "*") check_code_(row);
            if (row["name"].empty()) throw row.error("name", "must not be empty");
            (void)parse_weight_(row);
            break;
        }
    }

    // -- State ----------------------------------------------------------------

    tables _tables;
    model _model;
    shard_config _shard_cfg;
    shard_map _shards;
    core::rng _engine{core::rng::from_entropy()};

    struct auto_probe_tag {};

    explicit adg(auto_probe_tag /*tag*/) : _tables{make_tables_()}
    {
        if (!load(dataset::lite)) (void)load(dataset::full);
    }

    // -- Helpers ----------------------------------------------------------

    static std::span<const core::embedded_file> embedded_files_()
    {
#ifdef DASMIG_ADDRESS_EMBED
        return embedded::addressgen::files;
#else
        return {};
#endif
    }

    std::uint64_t draw_seed_() { return _engine(); }

    static documents read_directory_(const std::filesystem::path& dir)
    {
        documents docs;
        for (const auto t : all_tables_)
        {
            const auto file = dir / file_name_(t);
            std::error_code ec;
            if (std::filesystem::is_regular_file(file, ec))
            {
                docs.at(index_(t)) = core::tsv_document::load(file);
            }
        }
        return docs;
    }

    // Apply layers to copies of the tables, then commit: all or nothing.
    // With @p reset (load), tables without a document are emptied.
    void apply_(documents docs, core::layer_mode mode, bool reset, std::optional<shard_config> shards = {})
    {
        tables next = _tables;
        shard_config cfg = reset ? shard_config{} : _shard_cfg;
        if (reset && shards) cfg = std::move(*shards);
        // Street layers added while the streets are per-country files are kept
        // as documents, to be replayed over each file when it is read.
        if (!reset && cfg.active)
        {
            if (const auto& d = docs.at(index_(table::streets))) cfg.layers.push_back({.doc = *d, .mode = mode});
        }
        for (const auto t : all_tables_)
        {
            auto& doc = docs.at(index_(t));
            auto& tbl = next.at(index_(t));
            if (!doc)
            {
                if (reset) tbl.clear();
                continue;
            }
            (void)tbl.extend(std::move(*doc), mode, [t](const core::tsv_row& row) { validate_(t, row); });
        }
        auto built = build_model_(next, cfg);
        auto built_shards = make_shards_(next, cfg);
        _tables = std::move(next);
        _model = std::move(built);
        _shard_cfg = std::move(cfg);
        _shards = std::move(built_shards);
    }

    static std::string lower_(std::string_view s)
    {
        std::string out{s};
        for (auto& c : out)
        {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return out;
    }

    static shard_map make_shards_(const tables& tbls, const shard_config& cfg)
    {
        shard_map out;
        if (!cfg.active) return out;
        for (const auto& [code, entry] : cfg.entries) out.emplace(code, std::make_unique<shard>());
        for (const auto& row : tbls.at(index_(table::streets)).rows())
        {
            out.emplace(std::string{row["country_code"]}, std::make_unique<shard>());
        }
        return out;
    }

    static model build_model_(const tables& tbls, const shard_config& cfg)
    {
        model m;
        for (const auto& row : tbls.at(index_(table::places)).rows())
        {
            m.places.push_back(parse_place_(row));
        }
        std::map<std::string, std::vector<std::size_t>, std::less<>> by_country;
        for (std::size_t i = 0; i < m.places.size(); ++i)
        {
            const auto& p = m.places[i];
            m.place_of_id[p.id] = i;
            m.places_by_name.emplace(lower_(p.name), i);
            by_country[p.country].push_back(i);
        }
        for (auto& [code, idx] : by_country)
        {
            auto& c = m.countries[code];
            c.places = std::move(idx);
            for (const auto i : c.places)
            {
                const double w = static_cast<double>(std::max<std::uint64_t>(m.places[i].population, 1));
                c.place_weights.add(i, w);
                c.population += w;
            }
            m.codes.push_back(code);
            m.country_weights.add(code, c.population);
        }
        std::ranges::sort(m.codes);

        for (const auto& row : tbls.at(index_(table::formats)).rows())
        {
            auto [code, f] = parse_format_(row);
            auto& c = m.countries[code];
            c.format = std::move(f);
            c.has_format = true;
        }
        m.generic_format.lines = {"{A}", "{Z} {C}"};

        for (const auto& row : tbls.at(index_(table::streets)).rows())
        {
            auto s = parse_street_(row);
            m.countries[s.country].has_streets = true;
            if (!cfg.active) add_street_(m.streets, std::move(s));
        }
        if (cfg.active && !std::ranges::any_of(cfg.layers, [](const layer_doc& l)
                                               { return l.mode == core::layer_mode::replace; }))
        {
            for (const auto& [code, entry] : cfg.entries)
            {
                if (entry.rows > 0) m.countries[code].has_streets = true;
            }
        }
        for (const auto& row : tbls.at(index_(table::postcodes)).rows())
        {
            auto p = parse_postcode_(row);
            auto& pp = m.postcodes[p.id];
            pp.weights.add(pp.codes.size(), p.weight);
            pp.codes.push_back(std::move(p.code));
        }

        // Composed streets: a country's own types and names, else the generic ones.
        for (auto& [code, c] : m.countries) fill_composed_(tbls, code, c);
        if (!m.countries.contains("*")) fill_composed_(tbls, "*", m.countries["*"]);
        return m;
    }

    static void add_street_(std::unordered_map<std::uint32_t, place_streets>& into, street_row s)
    {
        auto& ps = into[s.id];
        ps.weights.add(ps.streets.size(), s.weight);
        ps.streets.push_back(std::move(s.street));
    }

    // Read one country's street file (once, under the shard's flag).
    void fill_shard_(const std::string& country, shard& s) const
    {
        std::optional<core::tsv_document> base;
        if (const auto e = _shard_cfg.entries.find(country); e != _shard_cfg.entries.end() && e->second.rows > 0)
        {
            const auto path = std::format("{}streets/{}.tsv", _shard_cfg.prefix, country);
            base = core::load_resource(path, embedded_files_(), _shard_cfg.root);
            if (!base)
            {
                throw std::runtime_error("Street data for " + country + " is listed in " +
                                         std::string{manifest_name_} + " but " + path + " was not found");
            }
            if (base->content_hash() != e->second.hash || base->size() != e->second.rows)
            {
                throw std::runtime_error(std::format(
                    "{} does not match {} (hash {:016x}, {} rows; expected {:016x}, {})", path, manifest_name_,
                    base->content_hash(), base->size(), e->second.hash, e->second.rows));
            }
        }
        if (_shard_cfg.layers.empty())
        {
            if (base)
            {
                for (const auto& row : base->rows()) add_street_(s.streets, parse_street_(row));
            }
        }
        else
        {
            core::layered_table t{schema_(table::streets)};
            if (base) (void)t.extend(std::move(*base), core::layer_mode::replace);
            for (const auto& l : _shard_cfg.layers)
            {
                (void)t.extend(l.doc, l.mode, [](const core::tsv_row& row) { validate_(table::streets, row); });
            }
            for (const auto& row : t.rows())
            {
                if (row["country_code"] == country) add_street_(s.streets, parse_street_(row));
            }
        }
        s.loaded.store(true);
    }

    // The country's street shard, read if it was not yet; null when the
    // country has no street data.
    [[nodiscard]] const shard* shard_of_(std::string_view country) const
    {
        const auto it = _shards.find(country);
        if (it == _shards.end()) return nullptr;
        shard& s = *it->second;
        std::call_once(s.once, [&] { fill_shard_(std::string{country}, s); });
        return &s;
    }

    static void fill_composed_(const tables& tbls, const std::string& code, country_data& c)
    {
        for (const auto& row : tbls.at(index_(table::street_types)).rows())
        {
            std::string country;
            auto [t, w] = parse_type_(row, country);
            if (country != code) continue;
            c.type_weights.add(c.types.size(), w);
            c.types.push_back(std::move(t));
        }
        for (const auto& row : tbls.at(index_(table::street_names)).rows())
        {
            if (row["country_code"] != code) continue;
            c.name_weights.add(c.names.size(), parse_weight_(row));
            c.names.push_back(row.get<std::string>("name"));
        }
    }

    void require_data_() const
    {
        if (_model.places.empty())
        {
            throw std::runtime_error("No address data loaded. Call load() first.");
        }
    }

    struct resolved
    {
        std::size_t index{none_}; // into model.places, none_ for a place given by name only
        std::uint32_t id{0};
        std::string country;
        std::string city;
        std::string region_code;
        std::string region;
        std::uint64_t population{0};
    };

    [[nodiscard]] resolved from_place_(std::size_t index) const
    {
        const auto& p = _model.places[index];
        return {.index = index, .id = p.id, .country = p.country, .city = p.name,
                .region_code = p.region_code, .region = p.region, .population = p.population};
    }

    // The place an address is in: the request's, else one drawn from its
    // country (itself drawn unless the request gives it).
    [[nodiscard]] resolved resolve_place_(const std::optional<std::string>& want_country,
                                          const std::optional<place_ref>& want_place, bool weighted,
                                          const core::rng& call) const
    {
        if (want_place)
        {
            if (const auto index = find_place_(want_country, *want_place); index != none_) return from_place_(index);
            if (!want_country)
            {
                throw std::invalid_argument("place '" + want_place->city +
                                            "' is not in the data; give a country to use it as given");
            }
            return {.index = none_, .id = 0, .country = *want_country, .city = want_place->city,
                    .region_code = want_place->region, .region = want_place->region, .population = 0};
        }
        auto cr = call.split("country");
        const std::string country = want_country ? *want_country : pick_country_(cr, weighted);
        const auto it = _model.countries.find(country);
        if (it == _model.countries.end() || it->second.places.empty())
        {
            throw std::invalid_argument("no places for country '" + country + "'");
        }
        auto pr = call.split("place");
        const auto& c = it->second;
        return from_place_(weighted ? c.place_weights.pick(pr)
                                    : c.places[core::uniform_int<std::size_t>(pr, 0, c.places.size() - 1)]);
    }

    [[nodiscard]] const format_data& format_of_(const std::string& country) const
    {
        const auto it = _model.countries.find(country);
        return it != _model.countries.end() && it->second.has_format ? it->second.format : _model.generic_format;
    }

    struct street_pick
    {
        std::string name;
        std::uint32_t lo{0};
        std::uint32_t hi{0};
        bool real{false};
    };

    // A street of the place, from its own data, else composed.
    [[nodiscard]] street_pick pick_street_(const resolved& r, bool weighted, core::rng& sr) const
    {
        const place_streets* ps = r.index != none_ ? streets_of_(r.country, r.id) : nullptr;
        if (ps != nullptr && weighted && !(ps->weights.total_weight() > 0.0)) ps = nullptr; // all switched off
        if (ps == nullptr) return {.name = composed_street_(r.country, sr), .lo = 0, .hi = 0, .real = false};
        const auto& s = ps->streets[weighted ? ps->weights.pick_index(sr)
                                             : core::uniform_int<std::size_t>(sr, 0, ps->streets.size() - 1)];
        return {.name = s.name, .lo = s.min_number, .hi = s.max_number, .real = true};
    }

    [[nodiscard]] std::string pick_postcode_(const resolved& r, const format_data& fmt, core::rng& zr) const
    {
        const place_postcodes* pc = r.index != none_ ? postcodes_of_(r.id) : nullptr;
        if (pc != nullptr && !(pc->weights.total_weight() > 0.0)) pc = nullptr;
        return postcode_(fmt, pc, zr);
    }

    [[nodiscard]] std::string pick_country_(core::rng& r, bool weighted) const
    {
        if (weighted) return _model.country_weights.pick(r);
        return _model.codes[core::uniform_int<std::size_t>(r, 0, _model.codes.size() - 1)];
    }

    [[nodiscard]] std::size_t find_place_(const std::optional<std::string>& want_country, const place_ref& ref) const
    {
        const auto range = _model.places_by_name.equal_range(lower_(ref.city));
        std::size_t best = none_;
        bool best_region = false;
        for (auto it = range.first; it != range.second; ++it)
        {
            const auto& p = _model.places[it->second];
            if (want_country && p.country != *want_country) continue;
            const bool region_ok = !ref.region.empty() && (lower_(p.region) == lower_(ref.region) ||
                                                           lower_(p.region_code) == lower_(ref.region));
            if (!ref.region.empty() && !region_ok && best != none_ && best_region) continue;
            const bool better = best == none_ || (region_ok && !best_region) ||
                                (region_ok == best_region && p.population > _model.places[best].population) ||
                                (region_ok == best_region && p.population == _model.places[best].population &&
                                 p.id < _model.places[best].id);
            if (better)
            {
                best = it->second;
                best_region = region_ok;
            }
        }
        return best;
    }

    [[nodiscard]] const place_streets* streets_of_(std::string_view country, std::uint32_t id) const
    {
        if (_shard_cfg.active)
        {
            const shard* s = shard_of_(country);
            if (s == nullptr) return nullptr;
            const auto it = s->streets.find(id);
            return it == s->streets.end() ? nullptr : &it->second;
        }
        const auto it = _model.streets.find(id);
        return it == _model.streets.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const place_postcodes* postcodes_of_(std::uint32_t id) const
    {
        const auto it = _model.postcodes.find(id);
        return it == _model.postcodes.end() ? nullptr : &it->second;
    }

    static std::string region_text_(const format_data& f, const std::string& code, const std::string& name)
    {
        const bool alpha = !code.empty() && code.size() <= 3 &&
                           std::ranges::all_of(code, [](char c) { return c >= 'A' && c <= 'Z'; });
        return f.region_as_code && alpha ? code : name;
    }

    [[nodiscard]] std::string composed_street_(const std::string& country, core::rng& r) const
    {
        const auto it = _model.countries.find(country);
        const country_data* c = it != _model.countries.end() ? &it->second : nullptr;
        if (c == nullptr || c->types.empty() || c->names.empty()) c = &_model.countries.at("*");
        if (c->types.empty() || c->names.empty())
        {
            throw std::runtime_error("No street data and no composed-street tables loaded for country " + country);
        }
        const auto& t = c->types[c->type_weights.pick_index(r)];
        const auto& n = c->names[c->name_weights.pick_index(r)];
        switch (t.pos)
        {
        case position::before: return t.type + " " + n;
        case position::after: return n + " " + t.type;
        case position::suffix: return n + t.type;
        }
        return n;
    }

    // Skewed towards low numbers, like real streets; products feed no addition.
    static std::uint32_t house_number_(core::rng& r, std::uint32_t lo, std::uint32_t hi)
    {
        const double u = core::uniform01(r);
        if (hi == 0)
        {
            const double x = u * u;
            return 1U + static_cast<std::uint32_t>(120.0 * x);
        }
        const double x = u * std::sqrt(u);
        const auto span = static_cast<double>(hi - lo + 1U);
        return std::min(hi, lo + static_cast<std::uint32_t>(span * x));
    }

    static double unit_chance_(const address_request& request, const format_data& f, std::uint64_t population)
    {
        if (f.unit_style.empty() || f.unit_style == "none") return 0.0;
        if (request.unit_chance) return std::clamp(*request.unit_chance, 0.0, 1.0);
        return population >= 250'000 ? 0.30 : 0.12;
    }

    static std::string unit_text_(const std::string& style, core::rng& r)
    {
        const auto n = core::uniform_int<int>(r, 1, 40);
        if (style == "flat") return "Flat " + std::to_string(n);
        if (style == "app_fr") return "Appt " + std::to_string(n);
        if (style == "whg") return "Whg. " + std::to_string(n);
        if (style == "ap_br") return "apto " + std::to_string(n);
        if (style == "piso")
        {
            const int floor = core::uniform_int<int>(r, 1, 9);
            const char letter = static_cast<char>('A' + core::uniform_int<int>(r, 0, 3));
            return std::to_string(floor) + "º " + std::string(1, letter);
        }
        return "Apt " + std::to_string(n);
    }

    // '#' a digit, 'N' a digit 1-9, 'A' a capital letter, 'L' a letter that is
    // accepted wherever a letter is restricted, 'U' a UK second-half letter,
    // '_' a space (a table cell does not keep a leading space).
    static std::string expand_(std::string_view pattern, core::rng& r)
    {
        static constexpr std::string_view safe = "ABCEGHJKLMNPRTVXY";
        static constexpr std::string_view uk = "ABDEFGHJLNPQRSTUWXYZ";
        std::string out;
        for (const char c : pattern)
        {
            switch (c)
            {
            case '#': out += static_cast<char>('0' + core::uniform_int<int>(r, 0, 9)); break;
            case 'N': out += static_cast<char>('1' + core::uniform_int<int>(r, 0, 8)); break;
            case 'A': out += static_cast<char>('A' + core::uniform_int<int>(r, 0, 25)); break;
            case 'L': out += safe[core::uniform_int<std::size_t>(r, 0, safe.size() - 1)]; break;
            case 'U': out += uk[core::uniform_int<std::size_t>(r, 0, uk.size() - 1)]; break;
            case '_': out += ' '; break;
            default: out += c; break;
            }
        }
        return out;
    }

    static std::string postcode_(const format_data& f, const place_postcodes* pc, core::rng& r)
    {
        if (pc != nullptr && !pc->codes.empty())
        {
            const auto& c = pc->codes[pc->weights.pick_index(r)];
            return c.partial ? c.code + expand_(f.zip_completion, r) : c.code;
        }
        if (!f.zip_pattern.empty()) return expand_(f.zip_pattern, r);
        if (!f.zip_examples.empty())
        {
            return f.zip_examples[core::uniform_int<std::size_t>(r, 0, f.zip_examples.size() - 1)];
        }
        return "";
    }

    static void replace_all_(std::string& s, std::string_view from, std::string_view to)
    {
        for (std::size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size()))
        {
            s.replace(pos, from.size(), to);
        }
    }

    static std::string tidy_(std::string s)
    {
        // Collapse the gaps left by empty parts.
        using fix = std::pair<std::string_view, std::string_view>;
        for (const auto& [bad, good] : {fix{"  ", " "}, fix{" ,", ","}, fix{", ,", ","}, fix{",,", ","}})
        {
            while (s.contains(bad)) replace_all_(s, bad, good);
        }
        const auto is_edge = [](char c) { return c == ' ' || c == ',' || c == '-'; };
        std::size_t b = 0;
        std::size_t e = s.size();
        while (b < e && is_edge(s[b])) ++b;
        while (e > b && is_edge(s[e - 1])) --e;
        return s.substr(b, e - b);
    }

    static std::vector<std::string> layout_(const format_data& f, const address& v, address_script script)
    {
        std::string street = v.street;
        if (v.number != 0)
        {
            const auto n = v._number_text.empty() ? std::to_string(v.number) : v._number_text;
            street = f.number_after ? street + (f.number_comma ? ", " : " ") + n : n + " " + street;
        }
        if (!v.unit.empty()) street += ", " + v.unit;

        std::vector<std::string> out;
        const auto& templates = script == address_script::latin && !f.lines_latin.empty() ? f.lines_latin : f.lines;
        for (const auto& tmpl : templates)
        {
            std::string line = tmpl;
            bool any = false;
            const auto put = [&](std::string_view token, const std::string& value) {
                if (!line.contains(token)) return;
                if (!value.empty()) any = true;
                replace_all_(line, token, value);
            };
            put("{A}", street);
            put("{C}", v.city);
            // A region with the city's own name (Tokyo, Singapore) is not repeated on its line.
            put("{S}", v.region == v.city && tmpl.contains("{C}") ? std::string{} : v.region);
            put("{Z}", v.postcode);
            put("{D}", "");
            put("{X}", "");
            if (!any) continue;
            line = tidy_(std::move(line));
            if (!line.empty()) out.push_back(std::move(line));
        }
        return out;
    }
};

} // namespace dasmig

#endif // DASMIG_ADDRESSGEN_HPP

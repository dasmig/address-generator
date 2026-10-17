#include "catch_amalgamated.hpp"
#include "../dasmig/addressgen.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

using dasmig::address_table;
using dasmig::core::layer_mode;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static dasmig::adg make_loaded_generator()
{
    dasmig::adg gen;
    if (!gen.load(dasmig::dataset::lite))
    {
        for (const auto* candidate : {"resources/lite", "../resources/lite"})
        {
            if (std::filesystem::is_directory(candidate))
            {
                gen.load(std::filesystem::path{candidate});
                break;
            }
        }
    }
    REQUIRE(gen.has_data());
    return gen;
}

// One US place and one Portuguese place, each with data we control.
static dasmig::adg make_small_generator()
{
    dasmig::adg gen;
    gen.extend_from_string(address_table::places,
                           "geonameid\tcountry_code\tname\tregion_code\tregion\tpopulation\n"
                           "1\tUS\tSpringfield\tIL\tIllinois\t100000\n"
                           "2\tPT\tLisboa\t\tLisboa\t500000\n"
                           "3\tCA\tMontreal\tQC\tQuebec\t400000\n",
                           "places.tsv", layer_mode::replace);
    gen.extend_from_string(address_table::formats,
                           "country_code\tlines\tnumber_after\tnumber_sep\tregion_style\tzip_pattern\tzip_completion\tunit_style\n"
                           "US\t{A}|{C}, {S} {Z}\t0\tspace\tcode\t#####\t\tapt\n"
                           "PT\t{A}|{Z} {C}\t1\tcomma\tname\t####-###\t\tpiso\n"
                           "CA\t{A}|{C} {S} {Z}\t0\tspace\tcode\tL#L #L#\t_#L#\tnone\n",
                           "formats.tsv", layer_mode::replace);
    gen.extend_from_string(address_table::streets,
                           "geonameid\tcountry_code\tname\tweight\tmin_number\tmax_number\n"
                           "1\tUS\tMain Street\t9\t10\t20\n"
                           "1\tUS\tElm Street\t1\t100\t120\n"
                           "2\tPT\tRua Augusta\t1\t1\t1\n",
                           "streets.tsv", layer_mode::replace);
    gen.extend_from_string(address_table::postcodes,
                           "geonameid\tcountry_code\tpostcode\tweight\tpartial\n"
                           "1\tUS\t62701\t1\t0\n"
                           "2\tPT\t1100-053\t1\t0\n"
                           "3\tCA\tH3Z\t1\t1\n",
                           "postcodes.tsv", layer_mode::replace);
    gen.extend_from_string(address_table::street_types,
                           "country_code\ttype\tposition\tweight\n"
                           "*\tStreet\tafter\t1\n",
                           "street_types.tsv", layer_mode::replace);
    gen.extend_from_string(address_table::street_names, "country_code\tname\tweight\n*\tOak\t1\n",
                           "street_names.tsv", layer_mode::replace);
    return gen;
}

static dasmig::address_request in(const char* country)
{
    dasmig::address_request r;
    r.country = country;
    return r;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

TEST_CASE("load(dataset::lite) loads every table", "[loading]")
{
    const auto gen = make_loaded_generator();
    REQUIRE(gen.count() > 30000);
    REQUIRE(gen.countries().size() > 200);
    REQUIRE(gen.has_streets("PT"));
    REQUIRE_FALSE(gen.has_streets("ZA"));
    for (const auto t : {address_table::places, address_table::formats, address_table::streets,
                         address_table::postcodes, address_table::street_types, address_table::street_names})
    {
        REQUIRE(gen.layers(t).size() == 1);
    }
}

TEST_CASE("load - non-existent path is a no-op", "[loading]")
{
    dasmig::adg gen;
    gen.load(std::filesystem::path{"no/such/directory"});
    REQUIRE_FALSE(gen.has_data());
    REQUIRE(gen.data_hash() == 0);
}

#ifndef DASMIG_ADDRESS_EMBED // compiled-in data is never missing
TEST_CASE("load(dataset) returns false for a missing tier", "[loading]")
{
    const auto old_cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path() / "adg_empty_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);
    dasmig::adg gen;
    const bool loaded = gen.load(dasmig::dataset::full);
    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);
    REQUIRE_FALSE(loaded);
    REQUIRE_FALSE(gen.has_data());
}
#endif

TEST_CASE("the singleton loads data on first use", "[loading]")
{
    REQUIRE(dasmig::adg::instance().has_data());
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

TEST_CASE("get_address throws when no data is loaded", "[generation]")
{
    dasmig::adg gen;
    REQUIRE_THROWS_AS(gen.get_address(dasmig::address_request{}), std::runtime_error);
    REQUIRE_THROWS_AS(gen.get_address(dasmig::address_request{}, std::uint64_t{1}), std::runtime_error);
}

TEST_CASE("get_address with a seed is deterministic", "[generation][seed]")
{
    const auto gen = make_loaded_generator();
    for (std::uint64_t s = 0; s < 100; ++s)
    {
        REQUIRE(gen.get_address(dasmig::address_request{}, s).to_json() == gen.get_address(dasmig::address_request{}, s).to_json());
    }
}

TEST_CASE("a result replays from its seed", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    const auto v = gen.get_address(dasmig::address_request{});
    REQUIRE(gen.get_address(dasmig::address_request{}, v.seed()).to_json() == v.to_json());
}

TEST_CASE("explicit per-call seeds replay across generator instances", "[seed]")
{
    const auto first = make_loaded_generator();
    const auto second = make_loaded_generator();
    REQUIRE(first.get_address(dasmig::address_request{}, 42).to_json() == second.get_address(dasmig::address_request{}, 42).to_json());
}

TEST_CASE("a country request stays in the country", "[generation][request]")
{
    const auto gen = make_loaded_generator();
    for (std::uint64_t s = 0; s < 200; ++s)
    {
        const auto v = gen.get_address(in("PT"), s);
        REQUIRE(v.country_code == "PT");
        REQUIRE_FALSE(v.city.empty());
        REQUIRE(v.real_street);
        REQUIRE(v.lines().size() == 2);
    }
    REQUIRE_THROWS_AS(gen.get_address(in("ZZ"), std::uint64_t{1}), std::invalid_argument);
}

TEST_CASE("a place request anchors the address", "[generation][request]")
{
    const auto gen = make_loaded_generator();
    dasmig::address_request r;
    r.place = dasmig::place_ref{"lisbon", ""};
    for (std::uint64_t s = 0; s < 50; ++s)
    {
        const auto v = gen.get_address(r, s);
        REQUIRE(v.city == "Lisbon");
        REQUIRE(v.country_code == "PT");
        REQUIRE(v.geonameid == 2267057);
        REQUIRE(v.real_street);
    }

    // A place the data lacks needs a country and is used as given.
    r.place = dasmig::place_ref{"Nowhereville", "Nowhere"};
    REQUIRE_THROWS_AS(gen.get_address(r, std::uint64_t{1}), std::invalid_argument);
    r.country = "PT";
    const auto v = gen.get_address(r, std::uint64_t{1});
    REQUIRE(v.city == "Nowhereville");
    REQUIRE_FALSE(v.real_street);
    REQUIRE(v.geonameid == 0);
    REQUIRE(v.postcode.size() == 8); // ####-### from the pattern
}

TEST_CASE("places of one name are told apart by region", "[generation][request]")
{
    const auto gen = make_loaded_generator();
    dasmig::address_request r;
    r.country = "US";
    r.place = dasmig::place_ref{"Springfield", "IL"};
    const auto illinois = gen.get_address(r, std::uint64_t{1});
    r.place = dasmig::place_ref{"Springfield", "Massachusetts"};
    const auto massachusetts = gen.get_address(r, std::uint64_t{1});
    REQUIRE(illinois.city == "Springfield");
    REQUIRE(massachusetts.city == "Springfield");
    REQUIRE(illinois.geonameid != massachusetts.geonameid);
    REQUIRE(illinois.region == "IL");
    REQUIRE(massachusetts.region == "MA");
}

TEST_CASE("streets follow their address counts", "[generation][stats]")
{
    const auto gen = make_small_generator();
    std::map<std::string, int> counts;
    constexpr int draws = 20000;
    for (std::uint64_t s = 0; s < draws; ++s) ++counts[gen.get_address(in("US"), s).street];
    REQUIRE(counts["Main Street"] == Catch::Approx(draws * 0.9).margin(draws * 0.02));
    REQUIRE(counts["Elm Street"] == Catch::Approx(draws * 0.1).margin(draws * 0.02));

    // Unweighted, the two are equally likely.
    auto uniform = in("US");
    uniform.weighted = false;
    counts.clear();
    for (std::uint64_t s = 0; s < draws; ++s) ++counts[gen.get_address(uniform, s).street];
    REQUIRE(counts["Main Street"] == Catch::Approx(draws * 0.5).margin(draws * 0.02));
}

TEST_CASE("house numbers stay in the street's range", "[generation][number]")
{
    const auto gen = make_small_generator();
    std::set<std::uint32_t> seen;
    for (std::uint64_t s = 0; s < 2000; ++s)
    {
        const auto v = gen.get_address(in("US"), s);
        if (v.street == "Main Street") REQUIRE((v.number >= 10 && v.number <= 20));
        else REQUIRE((v.number >= 100 && v.number <= 120));
        seen.insert(v.number);
    }
    REQUIRE(seen.size() > 10);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

TEST_CASE("the number goes where the country puts it", "[layout]")
{
    const auto gen = make_small_generator();
    auto us = in("US");
    us.unit_chance = 0.0;
    const auto a = gen.get_address(us, std::uint64_t{3});
    REQUIRE(a.lines().size() == 2);
    REQUIRE(a.lines()[0] == std::to_string(a.number) + " " + a.street);
    REQUIRE(a.lines()[1] == "Springfield, IL 62701");
    REQUIRE(a.formatted() == a.lines()[0] + ", " + a.lines()[1]);
    REQUIRE(a.formatted(" / ") == a.lines()[0] + " / " + a.lines()[1]);

    auto pt = in("PT");
    pt.unit_chance = 0.0;
    const auto b = gen.get_address(pt, std::uint64_t{3});
    REQUIRE(b.lines().size() == 2);
    REQUIRE(b.lines()[0] == "Rua Augusta, 1");
    REQUIRE(b.lines()[1] == "1100-053 Lisboa");
}

TEST_CASE("a unit follows the country's style", "[layout][unit]")
{
    const auto gen = make_small_generator();
    auto us = in("US");
    us.unit_chance = 1.0;
    REQUIRE(gen.get_address(us, std::uint64_t{1}).unit.starts_with("Apt "));
    auto pt = in("PT");
    pt.unit_chance = 1.0;
    const auto b = gen.get_address(pt, std::uint64_t{1});
    REQUIRE(b.lines()[0].starts_with("Rua Augusta, 1, "));
    REQUIRE(b.unit.find("º") != std::string::npos);

    // Canada has no unit style: never a unit, whatever the request says.
    auto ca = in("CA");
    ca.unit_chance = 1.0;
    for (std::uint64_t s = 0; s < 20; ++s) REQUIRE(gen.get_address(ca, s).unit.empty());

    us.unit_chance = 0.0;
    for (std::uint64_t s = 0; s < 200; ++s) REQUIRE(gen.get_address(us, s).unit.empty());
}

TEST_CASE("a partial postcode is completed from the pattern", "[layout][postcode]")
{
    const auto gen = make_small_generator();
    for (std::uint64_t s = 0; s < 50; ++s)
    {
        const auto v = gen.get_address(in("CA"), s);
        REQUIRE(v.postcode.starts_with("H3Z "));
        REQUIRE(v.postcode.size() == 7);
        REQUIRE(std::isdigit(static_cast<unsigned char>(v.postcode[4])) != 0);
        REQUIRE(std::isdigit(static_cast<unsigned char>(v.postcode[6])) != 0);
    }
}

TEST_CASE("a place without street data gets a composed street", "[layout][fallback]")
{
    const auto gen = make_small_generator();
    const auto v = gen.get_address(in("CA"), std::uint64_t{1});
    REQUIRE_FALSE(v.real_street);
    REQUIRE(v.street == "Oak Street");

    // Without composed tables there is nothing to fall back on.
    dasmig::adg bare;
    bare.extend_from_string(address_table::places, "geonameid\tcountry_code\tname\tpopulation\n9\tUS\tX\t1\n",
                            "p.tsv", layer_mode::replace);
    REQUIRE_THROWS_AS(bare.get_address(in("US"), std::uint64_t{1}), std::runtime_error);
}

TEST_CASE("every country lays out an address", "[layout]")
{
    const auto gen = make_loaded_generator();
    for (const auto& code : gen.countries())
    {
        for (std::uint64_t s = 0; s < 3; ++s)
        {
            const auto v = gen.get_address(in(code.c_str()), s);
            REQUIRE(v.country_code == code);
            REQUIRE_FALSE(v.lines().empty());
            REQUIRE_FALSE(v.street.empty());
            REQUIRE_FALSE(v.city.empty());
            for (const auto& line : v.lines()) REQUIRE_FALSE(line.empty());
        }
    }
}

TEST_CASE("every country lays out an address in the Latin script", "[layout][script]")
{
    const auto gen = make_loaded_generator();
    std::size_t differing = 0;
    for (const auto& code : gen.countries())
    {
        auto request = in(code.c_str());
        request.script = dasmig::address_script::latin;
        for (std::uint64_t s = 0; s < 3; ++s)
        {
            const auto v = gen.get_address(request, s);
            REQUIRE_FALSE(v.lines().empty());
            for (const auto& line : v.lines()) REQUIRE_FALSE(line.empty());
            if (v.lines() != gen.get_address(in(code.c_str()), s).lines()) ++differing;
        }
    }
    REQUIRE(differing > 0); // the countries with a Latin layout write it differently
}

TEST_CASE("the Latin layout changes only the layout", "[layout][script]")
{
    const auto gen = make_loaded_generator();
    auto latin = in("JP");
    latin.script = dasmig::address_script::latin;
    const auto local = gen.get_address(in("JP"), std::uint64_t{4});
    const auto roman = gen.get_address(latin, std::uint64_t{4});
    REQUIRE(local.street == roman.street);
    REQUIRE(local.postcode == roman.postcode);
    REQUIRE(local.lines() != roman.lines());
    REQUIRE(local.lines().front().starts_with("\u3012")); // 〒 first, then large to small
    REQUIRE(roman.lines().front().find(local.street) == 0); // street first in the Latin layout

    // A country without a Latin layout is the same either way.
    auto us = in("US");
    us.script = dasmig::address_script::latin;
    REQUIRE(gen.get_address(us, std::uint64_t{4}).lines() == gen.get_address(in("US"), std::uint64_t{4}).lines());
}

TEST_CASE("Japanese house numbers are blocks and the municipality has a line", "[layout][jp]")
{
    const auto gen = make_loaded_generator();
    const std::regex blocks{R"( [1-9]-[0-9]{1,2}-[0-9]{1,2}$)"};
    for (std::uint64_t s = 0; s < 50; ++s)
    {
        const auto v = gen.get_address(in("JP"), s);
        REQUIRE(v.lines().size() == 3);
        REQUIRE(std::regex_search(v.lines()[2], blocks));
        REQUIRE(v.lines()[1].find(v.city) != std::string::npos);
        REQUIRE(v.number >= 1);
        REQUIRE(v.number <= 9);
    }
    // Other countries keep plain numbers.
    REQUIRE_FALSE(std::regex_search(gen.get_address(in("DE"), std::uint64_t{1}).lines()[0], blocks));
}

TEST_CASE("a bad number_style is rejected", "[extend][errors]")
{
    auto gen = make_small_generator();
    REQUIRE_THROWS_AS(gen.extend_from_string(address_table::formats,
                                             "country_code\tlines\tnumber_after\tnumber_style\n"
                                             "US\t{A}|{C}\t0\tcircles\n",
                                             "bad"),
                      dasmig::core::tsv_error);
}

TEST_CASE("get_street is the street of the address with the same seed", "[street]")
{
    const auto gen = make_loaded_generator();
    for (const auto* code : {"PT", "US", "JP"})
    {
        for (const bool weighted : {true, false})
        {
            for (std::uint64_t s = 0; s < 40; ++s)
            {
                auto ar = in(code);
                ar.weighted = weighted;
                dasmig::street_request sr;
                sr.country = code;
                sr.weighted = weighted;
                const auto a = gen.get_address(ar, s);
                const auto st = gen.get_street(sr, s);
                REQUIRE(st.name == a.street);
                REQUIRE(st.real_street == a.real_street);
                REQUIRE(st.geonameid == a.geonameid);
                REQUIRE(st.city == a.city);
                REQUIRE(st.country_code == a.country_code);
                REQUIRE(st.seed() == s);
                REQUIRE(st.signature().data == gen.data_hash());
            }
        }
    }
    // A drawn country matches too.
    REQUIRE(gen.get_street(dasmig::street_request{}, std::uint64_t{8}).name ==
            gen.get_address(dasmig::address_request{}, std::uint64_t{8}).street);
}

TEST_CASE("get_postcode is the postcode of the address with the same seed", "[postcode]")
{
    const auto gen = make_loaded_generator();
    for (const auto* code : {"PT", "US", "GB", "BR", "JP", "NL"})
    {
        for (std::uint64_t s = 0; s < 40; ++s)
        {
            dasmig::postcode_request pr;
            pr.country = code;
            REQUIRE(gen.get_postcode(pr, s) == gen.get_address(in(code), s).postcode);
        }
    }
    dasmig::postcode_request none;
    none.country = "HK"; // no postcodes
    REQUIRE(gen.get_postcode(none, std::uint64_t{1}).empty());
}

TEST_CASE("street and postcode getters honour the place anchor", "[street][postcode]")
{
    const auto gen = make_loaded_generator();
    const auto base = gen.get_address(in("PT"), std::uint64_t{3});
    dasmig::street_request sr;
    sr.country = "PT";
    sr.place = dasmig::place_ref{.city = base.city, .region = base.region};
    dasmig::postcode_request pr;
    pr.country = "PT";
    pr.place = sr.place;
    for (std::uint64_t s = 0; s < 20; ++s)
    {
        const auto st = gen.get_street(sr, s);
        REQUIRE(st.city == base.city);
        REQUIRE(st.geonameid == base.geonameid);
        REQUIRE(gen.get_postcode(pr, s) == gen.get_postcode(pr, s));
    }
    sr.place = dasmig::place_ref{.city = "Nowhereville", .region = ""};
    REQUIRE_FALSE(gen.get_street(sr, std::uint64_t{1}).real_street); // a place the data lacks: composed
    sr.country.reset();
    REQUIRE_THROWS_AS(gen.get_street(sr, std::uint64_t{1}), std::invalid_argument);
}

TEST_CASE("street and postcode errors", "[street][postcode][errors]")
{
    dasmig::adg bare;
    REQUIRE_THROWS_AS(bare.get_street(dasmig::street_request{}, std::uint64_t{1}), std::runtime_error);
    REQUIRE_THROWS_AS(bare.get_postcode(dasmig::postcode_request{}, std::uint64_t{1}), std::runtime_error);
    const auto gen = make_loaded_generator();
    dasmig::street_request sr;
    sr.country = "ZZ";
    REQUIRE_THROWS_AS(gen.get_street(sr, std::uint64_t{1}), std::invalid_argument);
    dasmig::postcode_request pr;
    pr.country = "ZZ";
    REQUIRE_THROWS_AS(gen.get_postcode(pr, std::uint64_t{1}), std::invalid_argument);
}

TEST_CASE("street and postcode batches: items equal the single calls for any thread count", "[street][postcode][batch]")
{
    const auto gen = make_loaded_generator();
    dasmig::street_request sr;
    sr.country = "DE";
    const auto streets = gen.get_street_batch(sr, {.count = 120, .threads = 1}, 21);
    const auto more = gen.get_street_batch(sr, {.count = 120, .threads = 4}, 21);
    dasmig::postcode_request pr;
    pr.country = "DE";
    const auto codes = gen.get_postcode_batch(pr, {.count = 120, .threads = 1}, 21);
    const auto codes_more = gen.get_postcode_batch(pr, {.count = 120, .threads = 0}, 21);
    REQUIRE(streets.size() == 120);
    REQUIRE(codes == codes_more);
    for (std::size_t i = 0; i < streets.size(); ++i)
    {
        REQUIRE(streets[i].to_json() == more[i].to_json());
        REQUIRE(streets[i].to_json() == gen.get_street(sr, dasmig::core::item_seed(21, i)).to_json());
        REQUIRE(codes[i] == gen.get_postcode(pr, dasmig::core::item_seed(21, i)));
    }

    std::size_t seen = 0;
    gen.for_each_street(sr, {.count = 50, .first = 5, .threads = 3}, 21,
                        [&](std::uint64_t index, const dasmig::street& v)
                        {
                            REQUIRE(index == 5 + seen);
                            REQUIRE(v.name == streets[5 + seen].name);
                            ++seen;
                        });
    REQUIRE(seen == 50);
    seen = 0;
    gen.for_each_postcode(pr, {.count = 50, .first = 5, .threads = 3}, 21,
                          [&](std::uint64_t index, const std::string& v)
                          {
                              REQUIRE(index == 5 + seen);
                              REQUIRE(v == codes[5 + seen]);
                              ++seen;
                          });
    REQUIRE(seen == 50);
}

TEST_CASE("street to_json and conversions", "[street][json]")
{
    const auto gen = make_loaded_generator();
    dasmig::street_request sr;
    sr.country = "PT";
    const auto st = gen.get_street(sr, std::uint64_t{12});
    const std::string as_string = st;
    REQUIRE(as_string == st.name);
    std::ostringstream os;
    os << st;
    REQUIRE(os.str() == st.name);
    const auto json = st.to_json();
    REQUIRE(json.starts_with(R"({"name":")"));
    REQUIRE(json.find(R"("signature":{"seed":"000000000000000c")") != std::string::npos);
}

TEST_CASE("generated postcodes match the format source's expression", "[layout][postcode]")
{
    const auto gen = make_loaded_generator();
    std::filesystem::path file;
    for (const auto* candidate : {"tests/data/postcode_regex.tsv", "data/postcode_regex.tsv",
                                  "../tests/data/postcode_regex.tsv"})
    {
        if (std::filesystem::is_regular_file(candidate))
        {
            file = candidate;
            break;
        }
    }
    REQUIRE_FALSE(file.empty());
    const auto doc = dasmig::core::tsv_document::load(file);
    std::size_t checked = 0;
    for (const auto& row : doc.rows())
    {
        const std::string code = row.get<std::string>("country_code");
        if (std::find(gen.countries().begin(), gen.countries().end(), code) == gen.countries().end()) continue;
        std::regex rx;
        try
        {
            rx = std::regex{row.get<std::string>("regex"), std::regex::ECMAScript};
        }
        catch (const std::regex_error&)
        {
            continue; // a construct std::regex lacks
        }
        for (std::uint64_t s = 0; s < 60; ++s)
        {
            const auto v = gen.get_address(in(code.c_str()), s);
            INFO(code << ": " << v.postcode);
            REQUIRE(std::regex_match(v.postcode, rx));
        }
        ++checked;
    }
    REQUIRE(checked > 100);
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

TEST_CASE("string conversion and streaming give the address on one line", "[conversion]")
{
    const auto gen = make_loaded_generator();
    const auto v = gen.get_address(dasmig::address_request{}, std::uint64_t{5});
    const std::string s = v;
    std::ostringstream os;
    os << v;
    REQUIRE(s == v.formatted());
    REQUIRE(os.str() == v.formatted());
}

// ---------------------------------------------------------------------------
// Known answers (rule 2): the same seed and data give these exact results on
// every compiler and platform. A change here is a change of output and needs
// a major version.
// ---------------------------------------------------------------------------

TEST_CASE("known answers for fixed seeds", "[kat]")
{
    dasmig::adg gen;
    gen.load(std::filesystem::path{"resources/lite"});
    if (!gen.has_data()) gen.load(std::filesystem::path{"../resources/lite"});
    REQUIRE(gen.has_data());
    REQUIRE(gen.data_hash() == 0xe864d42e79fb6ab3ULL);

    REQUIRE(gen.get_address(dasmig::address_request{}, std::uint64_t{1}).to_json() == R"({"country_code":"DE","city":"Sindelfingen","region":"Baden-Wurttemberg","street":"Wilhelm-Haspel-Straße","number":76,"unit":"","postcode":"71069","real_street":true,"geonameid":2831948,"lines":["Wilhelm-Haspel-Straße 76","71069 Sindelfingen"],"formatted":"Wilhelm-Haspel-Straße 76, 71069 Sindelfingen","signature":{"seed":"0000000000000001","data":"e864d42e79fb6ab3"}})");
    REQUIRE(gen.get_address(in("PT"), std::uint64_t{1}).to_json() == R"({"country_code":"PT","city":"Portalegre","region":"Portalegre","street":"Rua 31 de Janeiro","number":109,"unit":"","postcode":"7300-022","real_street":true,"geonameid":2264508,"lines":["Rua 31 de Janeiro, 109","7300-022 Portalegre"],"formatted":"Rua 31 de Janeiro, 109, 7300-022 Portalegre","signature":{"seed":"0000000000000001","data":"e864d42e79fb6ab3"}})");
    REQUIRE(gen.get_address(in("US"), std::uint64_t{1}).to_json() == R"({"country_code":"US","city":"Kenner","region":"LA","street":"Williams Boulevard","number":3370,"unit":"","postcode":"70097","real_street":true,"geonameid":4329753,"lines":["3370 Williams Boulevard","Kenner, LA 70097"],"formatted":"3370 Williams Boulevard, Kenner, LA 70097","signature":{"seed":"0000000000000001","data":"e864d42e79fb6ab3"}})");
    REQUIRE(gen.get_address(in("JP"), std::uint64_t{1}).to_json() == R"({"country_code":"JP","city":"Tokyo","region":"Tokyo","street":"PROMINENCE ROKA","number":8,"unit":"","postcode":"100-6013","real_street":true,"geonameid":1850147,"lines":["〒100-6013","Tokyo","PROMINENCE ROKA 8-16-11"],"formatted":"〒100-6013, Tokyo, PROMINENCE ROKA 8-16-11","signature":{"seed":"0000000000000001","data":"e864d42e79fb6ab3"}})");
    dasmig::street_request fr;
    fr.country = "FR";
    REQUIRE(gen.get_street(fr, std::uint64_t{1}).to_json() == R"({"name":"Rue du Mail","country_code":"FR","city":"Savigny-sur-Orge","real_street":true,"geonameid":2975525,"signature":{"seed":"0000000000000001","data":"e864d42e79fb6ab3"}})");
    dasmig::postcode_request gb;
    gb.country = "GB";
    REQUIRE(gen.get_postcode(gb, std::uint64_t{1}) == "PE9 4DU");

    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::uint64_t s = 0; s < 10000; ++s)
    {
        for (const char c : gen.get_address(dasmig::address_request{}, s).to_json())
        {
            h ^= static_cast<unsigned char>(c);
            h *= 0x100000001b3ULL;
        }
    }
    if (h != 0xb9e652d45e4f2c41ULL) // TEMPORARY DEBUG
    {
        for (std::uint64_t s = 0; s < 10000; ++s)
        {
            std::printf("DBG %llu %016llx
", static_cast<unsigned long long>(s),
                        static_cast<unsigned long long>(dasmig::core::fnv1a64(gen.get_address(dasmig::address_request{}, s).to_json())));
        }
    }
    REQUIRE(h == 0xb9e652d45e4f2c41ULL);
}

// ---------------------------------------------------------------------------
// JSON and signatures
// ---------------------------------------------------------------------------

TEST_CASE("known answers: 10,000 streets and postcodes", "[kat][street][postcode]")
{
    const auto gen = make_loaded_generator();
    const auto fold = [](std::uint64_t h, const std::string& s)
    {
        for (const char c : s)
        {
            h ^= static_cast<unsigned char>(c);
            h *= 0x100000001b3ULL;
        }
        return h;
    };
    std::uint64_t streets = 0xcbf29ce484222325ULL;
    std::uint64_t codes = 0xcbf29ce484222325ULL;
    for (std::uint64_t s = 0; s < 10000; ++s)
    {
        streets = fold(streets, gen.get_street(dasmig::street_request{}, s).to_json());
        codes = fold(codes, gen.get_postcode(dasmig::postcode_request{}, s));
    }
    REQUIRE(streets == 0xc152ef3a2ec2cbf8ULL);
    REQUIRE(codes == 0xa9f87680b354eb21ULL);
}

TEST_CASE("to_json and signature", "[json]")
{
    const auto gen = make_loaded_generator();
    const auto v = gen.get_address(dasmig::address_request{}, std::uint64_t{99});
    REQUIRE(v.signature().seed == 99);
    REQUIRE(v.seed() == 99);
    REQUIRE(v.signature().data == gen.data_hash());
    const auto json = v.to_json();
    REQUIRE(json.starts_with(R"({"country_code":")"));
    REQUIRE(json.find(R"("signature":{"seed":"0000000000000063")") != std::string::npos);
    REQUIRE(json.find(R"("lines":[)") != std::string::npos);

    dasmig::core::json_writer w;
    w.begin_array();
    v.to_json(w);
    w.end_array();
    REQUIRE(w.str() == "[" + json + "]");
}

// ---------------------------------------------------------------------------
// Extending data (rule 8)
// ---------------------------------------------------------------------------

TEST_CASE("extend overrides, adds and removes rows", "[extend]")
{
    auto gen = make_small_generator();
    const auto base_hash = gen.data_hash();
    auto us = in("US");
    us.unit_chance = 0.0;
    const auto before = gen.get_address(us, std::uint64_t{7});

    // A new street with all the weight: every draw picks it.
    const auto& info = gen.extend_from_string(address_table::streets,
                                              "geonameid\tcountry_code\tname\tweight\tmin_number\tmax_number\n"
                                              "1\tUS\tMain Street\t0\t10\t20\n"
                                              "1\tUS\tElm Street\t0\t100\t120\n"
                                              "1\tUS\tZulu Way\t1\t5\t5\n",
                                              "mod.tsv");
    REQUIRE(info.replaced == 2);
    REQUIRE(info.added == 1);
    REQUIRE(gen.data_hash() != base_hash);
    REQUIRE(gen.layers(address_table::streets).size() == 2);
    const auto after = gen.get_address(us, std::uint64_t{7});
    REQUIRE(after.street == "Zulu Way");
    REQUIRE(after.number == 5);
    REQUIRE(after.signature().data == gen.data_hash());

    // Removing a place's streets sends it to the composed fallback.
    gen.extend_from_string(address_table::streets,
                           "geonameid\tname\tdisabled\n1\tMain Street\t1\n1\tElm Street\t1\n1\tZulu Way\t1\n",
                           "remove.tsv");
    REQUIRE_FALSE(gen.get_address(us, std::uint64_t{7}).real_street);

    // Reloading drops every layer.
    auto fresh = make_small_generator();
    REQUIRE(fresh.data_hash() == base_hash);
    REQUIRE(fresh.get_address(us, std::uint64_t{7}).to_json() == before.to_json());
}

TEST_CASE("a bad layer is rejected and leaves the data unchanged", "[extend][errors]")
{
    auto gen = make_small_generator();
    const auto hash = gen.data_hash();
    const auto streets = gen.layers(address_table::streets).size();

    REQUIRE_THROWS_WITH(gen.extend_from_string(address_table::streets,
                                               "geonameid\tcountry_code\tname\tweight\n1\tUS\tX\theavy\n", "bad.tsv"),
                        "bad.tsv:2: column 'weight': expected a number, got 'heavy'");
    REQUIRE_THROWS_AS(gen.extend_from_string(address_table::streets,
                                             "geonameid\tcountry_code\tname\tweight\n1\tUS\tX\t-1\n", "negative.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(address_table::streets,
                                             "geonameid\tcountry_code\tname\tweight\n1\tusa\tX\t1\n", "code.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(address_table::streets, "geonameid\tname\n1\tX\n", "partial.tsv"),
                      dasmig::core::tsv_error); // required column missing
    REQUIRE_THROWS_AS(gen.extend_from_string(address_table::streets,
                                             "geonameid\tcountry_code\tname\tweight\n1\tUS\tMain Street\t1\n", "clash.tsv",
                                             layer_mode::add),
                      dasmig::core::tsv_error); // already loaded
    REQUIRE_THROWS_AS(gen.extend_from_string(address_table::street_types,
                                             "country_code\ttype\tposition\tweight\nUS\tSt\tbeside\t1\n", "pos.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(address_table::formats,
                                             "country_code\tlines\tnumber_after\nUS\t{A}\t2\n", "after.tsv"),
                      dasmig::core::tsv_error);

    REQUIRE(gen.data_hash() == hash);
    REQUIRE(gen.layers(address_table::streets).size() == streets);
}

#ifdef DASMIG_ADDRESS_EMBED
TEST_CASE("compiled-in data needs no files and matches them", "[embed]")
{
    const auto old_cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path() / "adg_embed_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);
    dasmig::adg gen;
    const bool loaded = gen.load(dasmig::dataset::lite);
    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);

    REQUIRE(loaded);
    REQUIRE(gen.layers(address_table::places).front().source == "embedded:lite/places.tsv");
    REQUIRE(gen.data_hash() == 0xe864d42e79fb6ab3ULL); // same as the files
}
#endif

// ---------------------------------------------------------------------------
// Batch generation
// ---------------------------------------------------------------------------

TEST_CASE("address batch: items equal the single calls for any thread count", "[batch]")
{
    auto gen = make_loaded_generator();
    const auto request = dasmig::address_request{};
    const auto batch = gen.get_address_batch(request, {.count = 200, .threads = 1}, 77);
    REQUIRE(batch.size() == 200);
    for (std::uint64_t i = 0; i < batch.size(); ++i)
    {
        REQUIRE(batch[i].to_json() == gen.get_address(request, dasmig::core::item_seed(77, i)).to_json());
    }

    // A range is computed directly, on any number of threads.
    const auto tail = gen.get_address_batch(request, {.count = 100, .first = 100, .threads = 4}, 77);
    REQUIRE(tail.size() == 100);
    for (std::size_t i = 0; i < tail.size(); ++i)
    {
        REQUIRE(tail[i].to_json() == batch[100 + i].to_json());
    }
    for (const std::size_t threads : {2U, 7U, 0U})
    {
        const auto other = gen.get_address_batch(request, {.count = 200, .threads = threads}, 77);
        REQUIRE(other.size() == batch.size());
        for (std::size_t i = 0; i < other.size(); ++i)
        {
            REQUIRE(other[i].to_json() == batch[i].to_json());
        }
    }

    // Without a seed the batch still has the requested size.
    REQUIRE(gen.get_address_batch(request, {.count = 5}).size() == 5);
}

TEST_CASE("address batch: streaming gives the same items in order", "[batch]")
{
    auto gen = make_loaded_generator();
    const auto request = dasmig::address_request{};
    const auto batch = gen.get_address_batch(request, {.count = 300, .first = 10, .threads = 3}, 5);
    std::uint64_t next = 10;
    std::size_t seen = 0;
    gen.for_each_address(request, {.count = 300, .first = 10, .threads = 3}, 5,
                        [&](std::uint64_t index, const dasmig::address& item)
                        {
                            REQUIRE(index == next);
                            REQUIRE(item.to_json() == batch[seen].to_json());
                            ++next;
                            ++seen;
                        });
    REQUIRE(seen == batch.size());
}

TEST_CASE("address batch: known answer of 10,000 items", "[batch][kat]")
{
    auto gen = make_loaded_generator();
    const auto request = dasmig::address_request{};
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    gen.for_each_address(request, {.count = 10000, .threads = 4}, 1,
                        [&](std::uint64_t, const dasmig::address& item)
                        { hash = dasmig::core::hash_combine(hash, dasmig::core::fnv1a64(item.to_json())); });
    REQUIRE(hash == 0xc57500d9ad79f366ULL);
}

// ---------------------------------------------------------------------------
// The full tier: one street file per country, read on demand
// ---------------------------------------------------------------------------

static std::filesystem::path resource_dir(const char* tier)
{
    for (const auto* base : {"resources", "../resources"})
    {
        const auto dir = std::filesystem::path{base} / tier;
        if (std::filesystem::is_directory(dir)) return dir;
    }
    return {};
}

// A small data directory: the lite tables with the full tier's street files
// of some countries and a manifest for them. Empty when the resources are
// not there.
static std::filesystem::path make_shard_dir(const char* name, std::initializer_list<std::string> countries)
{
    const auto lite = resource_dir("lite");
    const auto full = resource_dir("full");
    if (lite.empty() || full.empty() || !std::filesystem::is_regular_file(full / "streets.manifest.tsv"))
    {
        return {};
    }
    const auto tmp = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "streets");
    for (const auto* f : {"places.tsv", "formats.tsv", "postcodes.tsv", "street_types.tsv", "street_names.tsv"})
    {
        std::filesystem::copy_file(lite / f, tmp / f);
    }
    std::ifstream in_manifest{full / "streets.manifest.tsv"};
    std::ofstream out_manifest{tmp / "streets.manifest.tsv"};
    std::string line;
    std::getline(in_manifest, line);
    out_manifest << line << '\n';
    while (std::getline(in_manifest, line))
    {
        const auto code = line.substr(0, line.find('\t'));
        if (std::ranges::find(countries, code) == countries.end()) continue;
        out_manifest << line << '\n';
        std::filesystem::copy_file(full / "streets" / (code + ".tsv"), tmp / "streets" / (code + ".tsv"));
    }
    return tmp;
}

TEST_CASE("full tier reads a country's street file on first use", "[shards]")
{
    const auto dir = make_shard_dir("adg_shards_first_use", {"PT", "NL"});
    if (dir.empty()) SKIP("resources not found");
    dasmig::adg gen;
    gen.load(dir);
    REQUIRE(gen.has_data());
    const auto hash = gen.data_hash();
    REQUIRE(hash != 0);

    REQUIRE(gen.has_streets("PT"));
    REQUIRE_FALSE(gen.has_streets("DE"));
    REQUIRE_FALSE(gen.streets_loaded("PT"));

    const auto v = gen.get_address(in("PT"), std::uint64_t{5});
    REQUIRE(v.real_street);
    REQUIRE(gen.streets_loaded("PT"));
    REQUIRE_FALSE(gen.streets_loaded("NL"));
    REQUIRE(gen.data_hash() == hash); // reading a country does not change the signature
    REQUIRE(v.signature().data == hash);

    REQUIRE(gen.load_streets("NL"));
    REQUIRE(gen.streets_loaded("NL"));
    REQUIRE_FALSE(gen.load_streets("DE")); // no file: composed streets
    REQUIRE_FALSE(gen.get_address(in("DE"), std::uint64_t{5}).real_street);
    std::filesystem::remove_all(dir);
}

TEST_CASE("full tier gives the same addresses whatever was read before, on any thread count", "[shards][batch]")
{
    const auto dir = make_shard_dir("adg_shards_order", {"PT", "NL"});
    if (dir.empty()) SKIP("resources not found");
    dasmig::adg cold;
    cold.load(dir);
    dasmig::adg warm;
    warm.load(dir);
    REQUIRE(warm.load_streets("NL"));
    REQUIRE(warm.load_streets("PT"));

    for (std::uint64_t s = 0; s < 200; ++s)
    {
        const auto request = in(s % 2 == 0 ? "PT" : "NL");
        REQUIRE(cold.get_address(request, s).to_json() == warm.get_address(request, s).to_json());
    }

    // Threads that all need an unread country at once.
    dasmig::adg parallel;
    parallel.load(dir);
    const auto a = parallel.get_address_batch(in("NL"), {.count = 400, .threads = 8}, 3);
    const auto b = warm.get_address_batch(in("NL"), {.count = 400, .threads = 1}, 3);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) REQUIRE(a[i].to_json() == b[i].to_json());
    REQUIRE(a.front().real_street);
    std::filesystem::remove_all(dir);
}

TEST_CASE("street layers apply to a full-tier country when it is read", "[shards][extend]")
{
    const auto dir = make_shard_dir("adg_shards_layers", {"PT"});
    if (dir.empty()) SKIP("resources not found");
    dasmig::adg gen;
    gen.load(dir);
    const auto base = gen.get_address(in("PT"), std::uint64_t{9});
    REQUIRE(base.real_street);
    REQUIRE(gen.streets_loaded("PT"));

    const auto hash = gen.data_hash();
    const auto layer = "geonameid\tcountry_code\tname\tweight\n" + std::to_string(base.geonameid) +
                       "\tPT\tRua do Teste\t1000000000000\n";
    gen.extend_from_string(address_table::streets, layer, "layer");
    REQUIRE(gen.data_hash() != hash);
    REQUIRE_FALSE(gen.streets_loaded("PT")); // read again, with the layer on top

    dasmig::address_request r = in("PT");
    r.place = dasmig::place_ref{.city = base.city, .region = base.region};
    for (std::uint64_t s = 0; s < 20; ++s) REQUIRE(gen.get_address(r, s).street == "Rua do Teste");

    // The same layer added before the country was read gives the same answers.
    dasmig::adg other;
    other.load(dir);
    other.extend_from_string(address_table::streets, layer, "layer");
    REQUIRE(other.data_hash() == gen.data_hash());
    for (std::uint64_t s = 0; s < 20; ++s) REQUIRE(other.get_address(r, s).to_json() == gen.get_address(r, s).to_json());

    // A layer can also remove the street again.
    gen.extend_from_string(address_table::streets,
                           "geonameid\tname\tdisabled\n" + std::to_string(base.geonameid) + "\tRua do Teste\t1\n",
                           "layer 2");
    for (std::uint64_t s = 0; s < 20; ++s) REQUIRE(gen.get_address(r, s).street != "Rua do Teste");
    std::filesystem::remove_all(dir);
}

TEST_CASE("a street file that does not match the manifest is an error", "[shards][errors]")
{
    const auto dir = make_shard_dir("adg_shards_bad", {"PT"});
    if (dir.empty()) SKIP("resources not found");

    {
        std::ofstream m{dir / "streets.manifest.tsv"};
        m << "country_code\trows\thash\nPT\t24216\t0123456789abcdef\n";
    }
    dasmig::adg gen;
    gen.load(dir);
    REQUIRE(gen.has_data());
    REQUIRE_THROWS_AS(gen.get_address(in("PT"), std::uint64_t{1}), std::runtime_error);

    {
        std::ofstream m{dir / "streets.manifest.tsv"};
        m << "country_code\trows\thash\nFR\t5\t0123456789abcdef\n"; // listed, no file
    }
    dasmig::adg missing;
    missing.load(dir);
    REQUIRE_THROWS_AS(missing.get_address(in("FR"), std::uint64_t{1}), std::runtime_error);

    {
        std::ofstream m{dir / "streets.manifest.tsv"};
        m << "country_code\trows\thash\nPT\t1\tnot-hex\n";
    }
    dasmig::adg bad;
    REQUIRE_THROWS_AS(bad.load(dir), dasmig::core::tsv_error);
    std::filesystem::remove_all(dir);
}

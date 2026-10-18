#include "../dasmig/addressgen.hpp"
#include <cstdint>
#include <iostream>

int main()
{
    // The singleton loads the lite tier (compiled in, or from resources/).
    auto& gen = dasmig::adg::instance();
    if (!gen.has_data())
    {
        std::cerr << "Could not load the address data.\n";
        return 1;
    }
    std::cout << "Loaded " << gen.place_count() << " places in " << gen.country_count() << " countries.\n\n";

    std::cout << "Random:\n";
    for (int i = 0; i < 5; ++i)
    {
        std::cout << "  " << gen.get_address(dasmig::address_request{}) << '\n';
    }

    std::cout << "\nIn Portugal, laid out line by line:\n";
    dasmig::address_request portugal;
    portugal.country = "PT";
    for (const auto& line : gen.get_address(portugal).lines())
    {
        std::cout << "  " << line << '\n';
    }

    std::cout << "\nIn a city drawn elsewhere (for example by city-generator):\n";
    dasmig::address_request lisbon;
    lisbon.place = dasmig::place_ref{.city = "Lisbon", .region = ""};
    std::cout << "  " << gen.get_address(lisbon) << '\n';

    std::cout << "\nSeed 42, twice:\n";
    const auto a = gen.get_address(dasmig::address_request{}, std::uint64_t{42});
    const auto b = gen.get_address(dasmig::address_request{}, std::uint64_t{42});
    std::cout << "  " << a.to_json() << '\n'
              << "  " << b.to_json() << '\n';

    std::cout << "\nWith a layer that adds a street:\n";
    gen.extend_from_string(dasmig::address_table::streets,
                           "geonameid\tcountry_code\tname\tweight\n2267057\tPT\tRua do Exemplo\t100000\n", "example.tsv");
    for (int i = 0; i < 3; ++i)
    {
        std::cout << "  " << gen.get_address(lisbon) << '\n';
    }
    return 0;
}

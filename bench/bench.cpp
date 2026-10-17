// Benchmarks for address-generator; see bench.hpp for the options.

#include "../dasmig/addressgen.hpp"
#include "bench.hpp"

int main(int argc, char** argv)
{
    dasmig::bench::runner run{"address-generator", argc, argv};

    run.load("load", [&] {
        dasmig::adg gen;
        return gen.load(run.tier());
    });

    dasmig::adg gen;
    if (!gen.load(run.tier())) return run.missing_tier();

    run.calls("get_address", [&](std::uint64_t seed) { return gen.get_address({}, seed); });
    run.calls("get_address to_json", [&](std::uint64_t seed) {
        return gen.get_address({}, seed).to_json();
    });

    run.batches("get_address_batch", [&](std::size_t count, std::size_t threads, std::uint64_t seed) {
        return gen.get_address_batch({}, {.count = count, .threads = threads}, seed);
    });

    return run.finish();
}

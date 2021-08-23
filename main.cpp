#include <iostream>
#include <cstdlib>
#include "cfg_string_generator.hpp"

std::unordered_map<char, std::vector<std::string>> rules;
size_t depth = 17;

template <typename Container>
void print_strings(Container& strings)
{
    for (auto& s: strings) {
            std::cout << s;
            std::cout << std::endl;
    }
    std::cout << std::endl;
}

template <bool low_mem, typename Container>
void print_derivations(Container& derivations)
{
    for (auto& s: derivations) {
        std::cout << s.first << " -> " << std::endl;
        for (auto& derivation: s.second) {
            for (auto& der: derivation) {
                if constexpr(low_mem)
                    std::cout << "(" << *der << "), ";
                else
                    std::cout << "(" << der.first << ", " << *der.second << "), ";
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

constexpr bool get_flag(unsigned flags, std::size_t pos) { return (flags >> pos) & 1; }

int main(int argc, char** argv) {
    rules['S'] = {"0A", "1B"};
    rules['A'] = {"0AA", "1S", "1"};
    rules['B'] = {"1BB", "0S", "0"};

    //derivation, repetition, low_memory, fast, derivation_fq, single_thread
    //const char* OUTPUT_ENABLE_STR = std::getenv("OUTPUT_ENABLE");
    //if (OUTPUT_ENABLE_STR == NULL) {
    if (argc == 1) {
        std::cout << "Mode:" << std::endl;
        std::cout << (get_flag(FLAGS, 0) ? "Derivation\n" : "");
        std::cout << (get_flag(FLAGS, 1) ? "repetition\n" : "");
        std::cout << (get_flag(FLAGS, 2) ? "fast\n" : "");
        std::cout << (get_flag(FLAGS, 3) ? "single_thread\n" : "");
        std::cout << (get_flag(FLAGS, 4) ? "derivation_fq\n" : "");
        std::cout << FLAGS;
        std::cout << std::endl;
        return 1;
    }
    //const bool OUTPUT_ENABLE = OUTPUT_ENABLE_STR[0] - '0';
    const bool OUTPUT_ENABLE = argv[1][0] == '1' ;

    if constexpr (DERIVATION_ENABLE) {
        auto derivations = cfg_string_gen::cfg_string_generator<true,
                                                                get_flag(FLAGS, 0),
                                                                get_flag(FLAGS, 3),
                                                                get_flag(FLAGS, 1),
                                                                get_flag(FLAGS, 4),
                                                                get_flag(FLAGS, 2)>(rules, depth);
        if (OUTPUT_ENABLE)
            print_derivations<get_flag(FLAGS, 3)>(derivations);
    }
    else {
        auto derivations = cfg_string_gen::cfg_string_generator<false,
                                                                get_flag(FLAGS, 0),
                                                                false,
                                                                get_flag(FLAGS, 1),
                                                                false,
                                                                get_flag(FLAGS, 2)>(rules, depth);
        if (OUTPUT_ENABLE)
            print_strings(derivations);
    }
    
}

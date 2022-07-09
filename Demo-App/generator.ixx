module;
#include "generator.hpp" // Reference implementation of std::generator proposal P2502R2

export module generator;

// export everything from this namespace segment
export namespace std {
using generator = std::generator;
namespace ranges {
using elements_of = std::ranges::elements_of;
}
} // namespace std

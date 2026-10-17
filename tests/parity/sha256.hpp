#pragma once
#include <string>
#include <string_view>
namespace sts2::parity {
// Test/file provenance utility; never linked into the simulation kernel.
std::string sha256(std::string_view bytes);
}

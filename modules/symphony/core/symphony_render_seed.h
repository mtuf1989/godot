#pragma once

#include <cstdint>

// Process-wide seed used when an operator would otherwise hash its address.
// Offline renders set this before compile and clear it afterward.
namespace SymphonyRenderSeed {
void set(uint32_t p_seed);
uint32_t get();
}

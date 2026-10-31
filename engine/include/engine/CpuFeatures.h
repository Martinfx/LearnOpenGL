#pragma once
#include <string>

namespace engine {

// The build may use x86 instruction sets beyond the baseline (SSE4.2 for the
// bundled Jolt; a system Jolt package can bring AVX2, FMA, AVX-512...). Running
// such a build on a CPU without them dies with SIGILL somewhere random. Call this
// first thing in main(): it returns false and names the missing features instead.
bool checkCpuFeatures(std::string* missing = nullptr);

} // namespace engine

// Tiny Annex-B H.264 helpers (no decoding, just NAL scanning).
#pragma once
#include <cstdint>
#include <vector>
#include <cstddef>

namespace clipback::h264 {

// Returns true if the byte stream contains an IDR slice (NAL type 5).
bool ContainsIdr(const uint8_t* data, size_t size);

// Extracts SPS (7) and PPS (8) NAL units (with 4-byte start codes) into `out`. Returns false if either is missing.
bool ExtractParameterSets(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

} // namespace clipback::h264

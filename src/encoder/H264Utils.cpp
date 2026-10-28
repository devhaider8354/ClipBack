#include "encoder/H264Utils.h"

namespace clipback::h264 {

namespace {

struct Nal {
    size_t offset;   // first byte after the start code
    size_t length;
    uint8_t type;
};

// Finds all NAL units delimited by 00 00 01 / 00 00 00 01 start codes.
template <typename Fn>
void ForEachNal(const uint8_t* data, size_t size, Fn fn)
{
    size_t i = 0;
    size_t start = static_cast<size_t>(-1);
    while (i + 2 < size) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            if (start != static_cast<size_t>(-1)) {
                size_t end = i;
                if (end > start && data[end - 1] == 0) --end;   // 4-byte start code of the next NAL
                if (end > start) fn(Nal{ start, end - start, static_cast<uint8_t>(data[start] & 0x1F) });
            }
            start = i + 3;
            i += 3;
        } else {
            ++i;
        }
    }
    if (start != static_cast<size_t>(-1) && start < size) {
        fn(Nal{ start, size - start, static_cast<uint8_t>(data[start] & 0x1F) });
    }
}

} // namespace

bool ContainsIdr(const uint8_t* data, size_t size)
{
    bool found = false;
    ForEachNal(data, size, [&](const Nal& n) { if (n.type == 5) found = true; });
    return found;
}

bool ExtractParameterSets(const uint8_t* data, size_t size, std::vector<uint8_t>& out)
{
    std::vector<uint8_t> sps, pps;
    ForEachNal(data, size, [&](const Nal& n) {
        if (n.type == 7 && sps.empty()) sps.assign(data + n.offset, data + n.offset + n.length);
        else if (n.type == 8 && pps.empty()) pps.assign(data + n.offset, data + n.offset + n.length);
    });
    if (sps.empty() || pps.empty()) return false;
    out.clear();
    static const uint8_t startCode[4] = { 0, 0, 0, 1 };
    out.insert(out.end(), startCode, startCode + 4);
    out.insert(out.end(), sps.begin(), sps.end());
    out.insert(out.end(), startCode, startCode + 4);
    out.insert(out.end(), pps.begin(), pps.end());
    return true;
}

} // namespace clipback::h264

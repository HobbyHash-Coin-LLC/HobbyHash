// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#include <consensus/hobc_marker.h>

#include <cstring>

namespace {
constexpr unsigned char MAGIC[4] = {'H', 'O', 'B', 'C'};

// Attempt to parse a marker whose magic begins exactly at offset `pos`.
// Canonical-order parse: required 0x01,0x02,0x03 then optional 0x04,0x05,0x06.
bool TryParseAt(std::span<const unsigned char> b, size_t pos, HobcTelemetryMarker& out)
{
    const size_t n = b.size();
    if (pos + 5 > n) return false;
    if (std::memcmp(b.data() + pos, MAGIC, 4) != 0) return false;
    size_t i = pos + 4;
    if (b[i] != HobcTelemetryMarker::VERSION) return false;
    ++i;

    HobcTelemetryMarker m;

    // Read one TLV of an expected type. Returns 1=parsed, 0=type absent (end/mismatch), -1=malformed.
    auto read_tlv = [&](uint8_t expected, size_t maxlen, size_t fixedlen,
                        std::string* strout, uint8_t* byteout, float* floatout) -> int {
        if (i >= n) return 0;
        if (b[i] != expected) return 0;
        if (i + 2 > n) return -1;
        const uint8_t len = b[i + 1];
        if (fixedlen != 0 && len != fixedlen) return -1;
        if (len > maxlen) return -1;
        if (i + 2 + static_cast<size_t>(len) > n) return -1;
        const unsigned char* v = b.data() + i + 2;
        if (byteout) *byteout = (len >= 1) ? v[0] : 0;
        if (floatout) std::memcpy(floatout, v, 4);
        if (strout) strout->assign(reinterpret_cast<const char*>(v), len);
        i += 2 + static_cast<size_t>(len);
        return 1;
    };

    // Required fields (canonical order 0x01, 0x02, 0x03).
    if (read_tlv(HobcTelemetryMarker::T_POOL_ID, 1, 1, nullptr, &m.pool_id, nullptr) != 1) return false;
    if (read_tlv(HobcTelemetryMarker::T_ALGO, 1, 1, nullptr, &m.algo, nullptr) != 1) return false;
    if (m.algo > 2) return false;
    if (read_tlv(HobcTelemetryMarker::T_WDIFF, 4, 4, nullptr, nullptr, &m.winning_share_diff) != 1) return false;

    // Optional fields (canonical order 0x04, 0x05, 0x06). Absent is fine; malformed is fatal.
    if (read_tlv(HobcTelemetryMarker::T_SITE, HobcTelemetryMarker::MAX_POOL_SITE, 0, &m.pool_site, nullptr, nullptr) < 0) return false;
    if (read_tlv(HobcTelemetryMarker::T_RIG, HobcTelemetryMarker::MAX_RIG_MODEL, 0, &m.rig_model, nullptr, nullptr) < 0) return false;
    if (read_tlv(HobcTelemetryMarker::T_WORKER, HobcTelemetryMarker::MAX_WORKER, 0, &m.worker_name, nullptr, nullptr) < 0) return false;

    out = m;
    return true;
}
} // namespace

bool ParseHobcTelemetryMarker(std::span<const unsigned char> b, HobcTelemetryMarker& out)
{
    const size_t n = b.size();
    if (n < 5) return false;
    for (size_t pos = 0; pos + 5 <= n; ++pos) {
        if (b[pos] == MAGIC[0] && b[pos + 1] == MAGIC[1] &&
            b[pos + 2] == MAGIC[2] && b[pos + 3] == MAGIC[3]) {
            if (TryParseAt(b, pos, out)) return true;
        }
    }
    return false;
}

std::vector<unsigned char> BuildHobcTelemetryMarker(const HobcTelemetryMarker& m)
{
    std::vector<unsigned char> o;
    o.insert(o.end(), MAGIC, MAGIC + 4);
    o.push_back(HobcTelemetryMarker::VERSION);

    o.push_back(HobcTelemetryMarker::T_POOL_ID);
    o.push_back(1);
    o.push_back(m.pool_id);

    o.push_back(HobcTelemetryMarker::T_ALGO);
    o.push_back(1);
    o.push_back(m.algo);

    o.push_back(HobcTelemetryMarker::T_WDIFF);
    o.push_back(4);
    {
        unsigned char tmp[4];
        std::memcpy(tmp, &m.winning_share_diff, 4);
        o.insert(o.end(), tmp, tmp + 4);
    }

    auto emit_str = [&](uint8_t type, const std::string& s, size_t cap) {
        if (s.empty()) return;
        const size_t len = s.size() > cap ? cap : s.size();
        o.push_back(type);
        o.push_back(static_cast<unsigned char>(len));
        o.insert(o.end(), s.begin(), s.begin() + len);
    };
    emit_str(HobcTelemetryMarker::T_SITE, m.pool_site, HobcTelemetryMarker::MAX_POOL_SITE);
    emit_str(HobcTelemetryMarker::T_RIG, m.rig_model, HobcTelemetryMarker::MAX_RIG_MODEL);
    emit_str(HobcTelemetryMarker::T_WORKER, m.worker_name, HobcTelemetryMarker::MAX_WORKER);

    return o;
}

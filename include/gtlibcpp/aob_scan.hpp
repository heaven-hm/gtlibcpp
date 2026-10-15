#pragma once
#include "gtlibcpp/memory_session.hpp"
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <vector>
#include <limits>

namespace gtlibcpp {

// Read-only byte-pattern matching in a caller-approved address range.
// Pattern bytes are expressed as pairs of hex digits separated by spaces;
// '?' and '??' are wildcards, e.g. "48 8B ?? 05".
// Limits scanning to caller-selected ranges; does not enumerate or mutate processes.
inline bool parse_aob(std::string_view text, std::vector<int>& pattern) {
    pattern.clear();
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        if (i == text.size()) break;
        if (text[i] == '?') {
            ++i;
            if (i < text.size() && text[i] == '?') ++i;
            if (i < text.size() && text[i] != ' ') return false;
            pattern.push_back(-1);
        } else {
            if (i + 1 >= text.size()) return false;
            int a = nibble(text[i]), b = nibble(text[i + 1]);
            if (a < 0 || b < 0) return false;
            i += 2;
            if (i < text.size() && text[i] != ' ') return false;
            pattern.push_back(a * 16 + b);
        }
        if (pattern.size() > 4096) return false;
    }
    return !pattern.empty();
}

inline Result<std::vector<Address>> scan_aob(
    MemorySession& session, Address start, std::size_t length,
    std::string_view signature, std::size_t max_matches = 64) {
    std::vector<int> pattern;
    if (!parse_aob(signature, pattern))
        return Result<std::vector<Address>>::failure(make_error(
            ErrorCode::invalid_type, "invalid AoB pattern", "scan_aob", start));
    if (length == 0 || length > (16u << 20) || max_matches == 0 ||
        start > (std::numeric_limits<Address>::max)() - length)
        return Result<std::vector<Address>>::failure(make_error(
            ErrorCode::invalid_size, "invalid or oversized scan range", "scan_aob", start, length));
    std::vector<Address> matches;
    constexpr std::size_t chunk_size = 64u << 10;
    const auto width = pattern.size();
    if (width > length) return Result<std::vector<Address>>::success(std::move(matches));
    std::size_t offset = 0;
    while (offset < length) {
        // Overlap consecutive chunks to match signatures straddling boundaries.
        const std::size_t size = (std::min)(length - offset, chunk_size + width - 1);
        const auto bytes = session.read_bytes(start + offset, size);
        if (!bytes)
            return Result<std::vector<Address>>::failure(bytes.error());
        for (std::size_t i = 0; i + width <= bytes.value().size(); ++i) {
            bool match = true;
            for (std::size_t k = 0; k < width; ++k) {
                if (pattern[k] >= 0 && bytes.value()[i + k] != static_cast<std::uint8_t>(pattern[k])) {
                    match = false;
                    break;
                }
            }
            if (match) {
                matches.push_back(start + offset + i);
                if (matches.size() >= max_matches)
                    return Result<std::vector<Address>>::success(std::move(matches));
            }
        }
        if (size == length - offset) break;
        offset += chunk_size;
    }
    return Result<std::vector<Address>>::success(std::move(matches));
}

} // namespace gtlibcpp

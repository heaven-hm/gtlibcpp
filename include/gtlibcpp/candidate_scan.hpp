#pragma once

#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/log.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace gtlibcpp {

enum class CandidateComparison {
    equal,
    changed,
    unchanged,
    increased,
    decreased,
    greater_than,
    less_than,
    between
};

enum class ScanType {
    exact,
    range,
    unknown_initial
};

template <typename T>
struct CandidateSnapshot {
    std::vector<Address> addresses;
    std::vector<T> values;

    [[nodiscard]] std::size_t size() const noexcept {
        return addresses.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return addresses.empty();
    }
};

template <typename T>
[[nodiscard]] inline bool evaluate_comparison(
    CandidateComparison comp,
    T now,
    T old,
    T target,
    T target_upper) {
    static_assert(std::is_arithmetic_v<T>, "Evaluation requires arithmetic type");
    switch (comp) {
        case CandidateComparison::equal:
            if constexpr (std::is_floating_point_v<T>) {
                return std::abs(now - target) <= static_cast<T>(0.0001);
            } else {
                return now == target;
            }
        case CandidateComparison::changed:
            if constexpr (std::is_floating_point_v<T>) {
                return std::abs(now - old) > static_cast<T>(0.0001);
            } else {
                return now != old;
            }
        case CandidateComparison::unchanged:
            if constexpr (std::is_floating_point_v<T>) {
                return std::abs(now - old) <= static_cast<T>(0.0001);
            } else {
                return now == old;
            }
        case CandidateComparison::increased:
            return now > old;
        case CandidateComparison::decreased:
            return now < old;
        case CandidateComparison::greater_than:
            return now > target;
        case CandidateComparison::less_than:
            return now < target;
        case CandidateComparison::between:
            return now >= target && now <= target_upper;
    }
    return false;
}

// First scan over a bounded address range [start_address, start_address + range_size).
// Bounded, safe against overflow, chunks reads in 64 KiB blocks.
template <typename T>
Result<CandidateSnapshot<T>> first_scan(
    MemorySession& session,
    Address start_address,
    std::size_t range_size,
    ScanType scan_type,
    T target = {},
    T target_upper = {},
    std::size_t alignment = sizeof(T),
    std::size_t max_matches = 65536) {
    static_assert(std::is_arithmetic_v<T>, "Candidate scan requires arithmetic type");

    if (start_address == 0 || range_size == 0 || max_matches == 0) {
        return Result<CandidateSnapshot<T>>::failure(make_error(
            ErrorCode::invalid_size, "invalid scan parameters",
            "first_scan", start_address, range_size));
    }
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return Result<CandidateSnapshot<T>>::failure(make_error(
            ErrorCode::invalid_size, "alignment must be a power of two",
            "first_scan", start_address, alignment));
    }
    if (start_address > (std::numeric_limits<Address>::max)() - range_size) {
        return Result<CandidateSnapshot<T>>::failure(make_error(
            ErrorCode::invalid_address, "scan range wraps the address space",
            "first_scan", start_address, range_size));
    }

    CandidateSnapshot<T> snapshot;
    const std::size_t chunk_size = 64 * 1024;
    std::size_t offset = 0;

    while (offset < range_size && snapshot.addresses.size() < max_matches) {
        const std::size_t this_chunk = (std::min)(chunk_size, range_size - offset);
        const Address chunk_addr = start_address + offset;

        if (this_chunk < sizeof(T)) {
            break;
        }

        const auto read_result = session.read_bytes(chunk_addr, this_chunk);
        if (read_result) {
            const auto& bytes = read_result.value();
            const std::size_t limit = bytes.size() >= sizeof(T) ? bytes.size() - sizeof(T) : 0;

            for (std::size_t i = 0; i <= limit && snapshot.addresses.size() < max_matches; ++i) {
                const Address item_addr = chunk_addr + i;
                if ((item_addr % alignment) != 0) {
                    continue;
                }
                T value{};
                std::memcpy(&value, bytes.data() + i, sizeof(T));

                bool matched = false;
                switch (scan_type) {
                    case ScanType::exact:
                        if constexpr (std::is_floating_point_v<T>) {
                            matched = std::abs(value - target) <= static_cast<T>(0.0001);
                        } else {
                            matched = value == target;
                        }
                        break;
                    case ScanType::range:
                        matched = value >= target && value <= target_upper;
                        break;
                    case ScanType::unknown_initial:
                        matched = true;
                        break;
                }

                if (matched) {
                    snapshot.addresses.push_back(item_addr);
                    snapshot.values.push_back(value);
                }
            }
        }

        offset += this_chunk;
    }

    Logger::instance().debug("first_scan", "scanned bytes=" + std::to_string(offset) +
                             " matches=" + std::to_string(snapshot.addresses.size()));

    return Result<CandidateSnapshot<T>>::success(std::move(snapshot));
}

// Next scan over existing CandidateSnapshot
template <typename T>
Result<CandidateSnapshot<T>> next_scan(
    MemorySession& session,
    const CandidateSnapshot<T>& previous,
    CandidateComparison comparison,
    T target = {},
    T target_upper = {},
    std::size_t max_candidates = 65536) {
    static_assert(std::is_arithmetic_v<T>, "Candidate scan requires arithmetic type");

    if (previous.addresses.size() != previous.values.size() ||
        previous.addresses.size() > max_candidates || max_candidates == 0) {
        return Result<CandidateSnapshot<T>>::failure(make_error(
            ErrorCode::invalid_size, "invalid candidate list size",
            "next_scan", 0, previous.addresses.size()));
    }

    CandidateSnapshot<T> refined;
    refined.addresses.reserve(previous.addresses.size());
    refined.values.reserve(previous.addresses.size());

    for (std::size_t index = 0; index < previous.addresses.size(); ++index) {
        const Address address = previous.addresses[index];
        if (address == 0 || address > (std::numeric_limits<Address>::max)() - sizeof(T)) {
            return Result<CandidateSnapshot<T>>::failure(make_error(
                ErrorCode::invalid_size, "invalid candidate address",
                "next_scan", address, sizeof(T)));
        }

        const auto read = session.read<T>(address);
        if (!read) {
            // Partial read recovery: if memory is unmapped or fails, skip this candidate
            continue;
        }

        const auto now = read.value();
        const auto old = previous.values[index];

        if (evaluate_comparison(comparison, now, old, target, target_upper)) {
            refined.addresses.push_back(address);
            refined.values.push_back(now);
        }
    }

    Logger::instance().debug("next_scan", "initial candidates=" + std::to_string(previous.addresses.size()) +
                             " refined candidates=" + std::to_string(refined.addresses.size()));

    return Result<CandidateSnapshot<T>>::success(std::move(refined));
}

// Existing backwards-compatible refine_candidates API
template <typename T>
Result<std::vector<Address>> refine_candidates(
    MemorySession& session,
    const std::vector<Address>& candidates,
    const std::vector<T>& previous_values,
    CandidateComparison comparison,
    T target = {},
    std::size_t max_candidates = 65536) {
    static_assert(std::is_arithmetic_v<T>, "Candidate scan requires arithmetic type");

    CandidateSnapshot<T> snapshot;
    snapshot.addresses = candidates;
    snapshot.values = previous_values;

    auto result = next_scan<T>(session, snapshot, comparison, target, target, max_candidates);
    if (!result) return Result<std::vector<Address>>::failure(result.error());

    return Result<std::vector<Address>>::success(std::move(result.value().addresses));
}

} // namespace gtlibcpp

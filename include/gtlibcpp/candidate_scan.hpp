#pragma once

#include "gtlibcpp/memory_session.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace gtlibcpp {

// Read-only refinement over explicitly supplied candidate addresses.
// Does not discover new process regions or retain previous scan snapshots.
enum class CandidateComparison { equal, changed, unchanged, increased, decreased };

template <typename T>
Result<std::vector<Address>> refine_candidates(
    MemorySession& session,
    const std::vector<Address>& candidates,
    const std::vector<T>& previous_values,
    CandidateComparison comparison,
    T target = {},
    std::size_t max_candidates = 65536) {
    static_assert(std::is_arithmetic_v<T>, "Candidate scan requires arithmetic type");
    if (candidates.size() != previous_values.size() ||
        candidates.size() > max_candidates || max_candidates == 0) {
        return Result<std::vector<Address>>::failure(make_error(
            ErrorCode::invalid_size, "invalid candidate list size",
            "refine_candidates", 0, candidates.size()));
    }
    std::vector<Address> matched;
    matched.reserve(candidates.size());
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const Address address = candidates[index];
        if (address == 0 || address > (std::numeric_limits<Address>::max)() - sizeof(T)) {
            return Result<std::vector<Address>>::failure(make_error(
                ErrorCode::invalid_size, "invalid candidate address",
                "refine_candidates", address, sizeof(T)));
        }
        const auto read = session.read<T>(address);
        if (!read) return Result<std::vector<Address>>::failure(read.error());
        const auto now = read.value();
        const auto old = previous_values[index];
        bool keep = false;
        switch (comparison) {
            case CandidateComparison::equal: keep = now == target; break;
            case CandidateComparison::changed: keep = now != old; break;
            case CandidateComparison::unchanged: keep = now == old; break;
            case CandidateComparison::increased: keep = now > old; break;
            case CandidateComparison::decreased: keep = now < old; break;
        }
        if (keep) matched.push_back(address);
    }
    return Result<std::vector<Address>>::success(std::move(matched));
}

} // namespace gtlibcpp

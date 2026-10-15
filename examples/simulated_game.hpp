#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "gtlibcpp/backend.hpp"
#include "gtlibcpp/types.hpp"

namespace gtlibcpp::demo {

/// Simulated in-memory game process backend for cross-platform E2E testing.
class SimulatedGameProcess final : public IMemoryBackend {
public:
    static constexpr Address kBaseAddress   = 0x140000000ULL; // Module Base
    static constexpr Address kPlayerPtrAddr = 0x140100000ULL; // Static Player Pointer
    static constexpr Address kPlayerObjAddr = 0x200000000ULL; // Allocated Player Struct
    static constexpr Address kStatsPtrAddr  = 0x200000050ULL; // Offset +0x50 to Stats Pointer
    static constexpr Address kStatsObjAddr  = 0x300000000ULL; // Allocated Stats Struct

    // Offsets within Stats struct:
    static constexpr Address kHealthOffset = 0x10; // uint32_t health
    static constexpr Address kArmorOffset  = 0x14; // float armor
    static constexpr Address kAmmoOffset   = 0x18; // uint32_t ammo
    static constexpr Address kScoreOffset  = 0x20; // uint64_t score
    static constexpr Address kNameOffset   = 0x30; // char name[32]

    SimulatedGameProcess() {
        identity_.pid = 4242;
        identity_.image_path = "C:/Games/CyberAssault/game.exe";
        identity_.architecture = Architecture::x64;
        identity_.image_sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
        identity_.start_time = 1700000000000LL;

        // Initialize multi-level pointer chain:
        // kPlayerPtrAddr -> points to kPlayerObjAddr
        write_u64_internal(kPlayerPtrAddr, kPlayerObjAddr);

        // kPlayerObjAddr + 0x50 (kStatsPtrAddr) -> points to kStatsObjAddr
        write_u64_internal(kStatsPtrAddr, kStatsObjAddr);

        // Populate initial player stats:
        write_u32_internal(kStatsObjAddr + kHealthOffset, 100);
        write_f32_internal(kStatsObjAddr + kArmorOffset, 75.5f);
        write_u32_internal(kStatsObjAddr + kAmmoOffset, 30);
        write_u64_internal(kStatsObjAddr + kScoreOffset, 9999ULL);

        const std::string initial_name = "Commander_Shepard";
        for (std::size_t i = 0; i <= initial_name.size(); ++i) {
            memory_[kStatsObjAddr + kNameOffset + i] = static_cast<std::uint8_t>(initial_name[i]);
        }
    }

    Result<std::vector<std::uint8_t>> read(Address address, std::size_t size) override {
        if (!alive_.load()) {
            return Result<std::vector<std::uint8_t>>::failure(make_error(
                ErrorCode::target_dead, "simulated game target process terminated",
                "SimulatedGameProcess::read", address, size));
        }
        std::vector<std::uint8_t> buffer(size);
        {
            std::lock_guard<std::mutex> lock(memory_mutex_);
            for (std::size_t i = 0; i < size; ++i) {
                auto it = memory_.find(address + i);
                if (it == memory_.end()) {
                    buffer[i] = 0; // Uninitialized memory page returns zero
                } else {
                    buffer[i] = it->second;
                }
            }
        }
        read_count_++;
        return Result<std::vector<std::uint8_t>>::success(std::move(buffer));
    }

    Result<std::size_t> write(Address address, const std::vector<std::uint8_t>& bytes) override {
        if (!alive_.load()) {
            return Result<std::size_t>::failure(make_error(
                ErrorCode::target_dead, "simulated game target process terminated",
                "SimulatedGameProcess::write", address, bytes.size()));
        }
        {
            std::lock_guard<std::mutex> lock(memory_mutex_);
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                memory_[address + i] = bytes[i];
            }
        }
        write_count_++;
        return Result<std::size_t>::success(bytes.size());
    }

    bool is_alive() noexcept override { return alive_.load(); }
    std::string target_id() const override { return "CyberAssault.exe"; }
    TargetIdentity identity() const override { return identity_; }

    void terminate_process() noexcept { alive_.store(false); }
    void revive_process() noexcept { alive_.store(true); }

    std::uint64_t total_reads() const noexcept { return read_count_.load(); }
    std::uint64_t total_writes() const noexcept { return write_count_.load(); }

private:
    void write_u32_internal(Address addr, std::uint32_t val) {
        std::lock_guard<std::mutex> lock(memory_mutex_);
        for (std::size_t i = 0; i < 4; ++i) {
            memory_[addr + i] = static_cast<std::uint8_t>((val >> (i * 8)) & 0xFF);
        }
    }

    void write_f32_internal(Address addr, float val) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &val, sizeof(val));
        write_u32_internal(addr, bits);
    }

    void write_u64_internal(Address addr, std::uint64_t val) {
        std::lock_guard<std::mutex> lock(memory_mutex_);
        for (std::size_t i = 0; i < 8; ++i) {
            memory_[addr + i] = static_cast<std::uint8_t>((val >> (i * 8)) & 0xFF);
        }
    }

    mutable std::mutex memory_mutex_{};
    std::map<Address, std::uint8_t> memory_{};
    TargetIdentity identity_{};
    std::atomic<bool> alive_{true};
    std::atomic<std::uint64_t> read_count_{0};
    std::atomic<std::uint64_t> write_count_{0};
};

} // namespace gtlibcpp::demo

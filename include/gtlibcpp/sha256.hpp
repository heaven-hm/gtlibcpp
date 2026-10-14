#pragma once

// Dependency-free SHA-256 for bounded mutation preconditions.
// Hashing is not an authentication mechanism: callers must still authorize
// mutations independently through Policy and an out-of-band approval channel.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gtlibcpp {
namespace detail {
inline std::uint32_t sha256_rotr(std::uint32_t x, unsigned n) noexcept {
    return (x >> n) | (x << (32u - n));
}
} // namespace detail

inline std::string sha256_hex(const std::vector<std::uint8_t>& input) {
    static constexpr std::array<std::uint32_t, 64> k = {{
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,
        0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
        0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,
        0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,
        0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
        0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,
        0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,
        0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
        0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    }};
    std::array<std::uint32_t,8> h = {{
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    }};
    std::vector<std::uint8_t> padded = input;
    padded.push_back(0x80);
    while (padded.size() % 64u != 56u) padded.push_back(0);
    const std::uint64_t bit_count = static_cast<std::uint64_t>(input.size()) * 8u;
    for (int shift = 56; shift >= 0; shift -= 8)
        padded.push_back(static_cast<std::uint8_t>((bit_count >> shift) & 0xffu));
    for (std::size_t offset = 0; offset < padded.size(); offset += 64u) {
        std::uint32_t w[64]{};
        for (std::size_t i = 0; i < 16; ++i) {
            const auto j = offset + i * 4u;
            w[i] = (static_cast<std::uint32_t>(padded[j]) << 24) |
                   (static_cast<std::uint32_t>(padded[j+1]) << 16) |
                   (static_cast<std::uint32_t>(padded[j+2]) << 8) |
                    static_cast<std::uint32_t>(padded[j+3]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const auto x = w[i-15], y = w[i-2];
            const auto s0 = detail::sha256_rotr(x,7) ^ detail::sha256_rotr(x,18) ^ (x >> 3);
            const auto s1 = detail::sha256_rotr(y,17) ^ detail::sha256_rotr(y,19) ^ (y >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        auto a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], z=h[7];
        for (std::size_t i=0; i<64; ++i) {
            const auto s1 = detail::sha256_rotr(e,6) ^ detail::sha256_rotr(e,11)
                          ^ detail::sha256_rotr(e,25);
            const auto ch = (e & f) ^ (~e & g);
            const auto t1 = z + s1 + ch + k[i] + w[i];
            const auto s0 = detail::sha256_rotr(a,2) ^ detail::sha256_rotr(a,13)
                          ^ detail::sha256_rotr(a,22);
            const auto maj = (a & b) ^ (a & c) ^ (b & c);
            const auto t2 = s0 + maj;
            z=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=z;
    }
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(64, '0');
    for (std::size_t i=0; i<8; ++i) {
        for (std::size_t j=0; j<8; ++j)
            out[i*8+j]=digits[(h[i] >> (28-4*j)) & 15u];
    }
    return out;
}
} // namespace gtlibcpp

// hy3_algotrace — SHA-256 implementation.
//
// Self-contained FIPS 180-4 SHA-256. No third-party dependencies.

#include "hy3_algotrace/sha256.hpp"

#include <cstring>

namespace hy3 {

namespace {

// SHA-256 initial hash values H0..H7 (first 32 bits of fractional parts of
// square roots of first 8 primes 2..19).
constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

inline uint32_t shr(uint32_t x, uint32_t n) { return x >> n; }

inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t bigSigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline uint32_t bigSigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline uint32_t smallSigma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ shr(x, 3);
}

inline uint32_t smallSigma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ shr(x, 10);
}

} // namespace

std::string sha256_hex(const std::vector<uint8_t>& data) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    // Pre-processing: padding.
    uint64_t bitLen = static_cast<uint64_t>(data.size()) * 8;
    std::vector<uint8_t> msg = data;
    msg.push_back(0x80);
    // Append zeros until length in bits ≡ 448 (mod 512) => bytes ≡ 56 (mod 64).
    while (msg.size() % 64 != 56) {
        msg.push_back(0x00);
    }
    // Append original length in bits as 64-bit big-endian.
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFF));
    }

    // Process each 512-bit (64-byte) chunk.
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[off + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[off + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[off + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[off + i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            w[i] = smallSigma1(w[i - 2]) + w[i - 7] + smallSigma0(w[i - 15]) +
                   w[i - 16];
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + bigSigma1(e) + ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = bigSigma0(a) + maj(a, b, c);
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    // Produce 64-char lowercase hex.
    static const char* hexd = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 8; ++i) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            uint8_t byte = static_cast<uint8_t>((h[i] >> shift) & 0xFF);
            out.push_back(hexd[byte >> 4]);
            out.push_back(hexd[byte & 0x0F]);
        }
    }
    return out;
}

void stripBomIfPresent(std::vector<uint8_t>& data, bool& removed) {
    removed = false;
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        data.erase(data.begin(), data.begin() + 3);
        removed = true;
    }
}

bool normalizeUtf8(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out,
                   std::string& error) {
    out.clear();
    error.clear();

    std::vector<uint8_t> work = raw;
    bool bomRemoved = false;
    stripBomIfPresent(work, bomRemoved);
    (void)bomRemoved;

    // Reject embedded NUL.
    for (uint8_t b : work) {
        if (b == 0x00) {
            error = sha_errc::E_EMBEDDED_NUL;
            return false;
        }
    }

    // Validate UTF-8 and normalize line endings in one pass.
    size_t i = 0;
    const size_t n = work.size();
    while (i < n) {
        uint8_t b = work[i];

        // Line ending normalization: CRLF -> LF, lone CR -> LF.
        if (b == '\r') {
            if (i + 1 < n && work[i + 1] == '\n') {
                out.push_back('\n');
                i += 2;
                continue;
            }
            out.push_back('\n');
            i += 1;
            continue;
        }

        if (b < 0x80) {
            out.push_back(b);
            i += 1;
            continue;
        }

        // Multi-byte UTF-8 sequence.
        int extra = 0;
        uint32_t cp = 0;
        if ((b & 0xE0) == 0xC0) {        // 110xxxxx -> 2 bytes
            extra = 1;
            cp = b & 0x1F;
        } else if ((b & 0xF0) == 0xE0) { // 1110xxxx -> 3 bytes
            extra = 2;
            cp = b & 0x0F;
        } else if ((b & 0xF8) == 0xF0) { // 11110xxx -> 4 bytes
            extra = 3;
            cp = b & 0x07;
        } else {
            error = sha_errc::E_UTF8_INVALID; // continuation byte or overlong lead
            return false;
        }

        if (i + static_cast<size_t>(extra) >= n) {
            error = sha_errc::E_UTF8_INVALID; // truncated sequence
            return false;
        }

        for (int k = 1; k <= extra; ++k) {
            uint8_t cb = work[i + k];
            if ((cb & 0xC0) != 0x80) {
                error = sha_errc::E_UTF8_INVALID; // bad continuation
                return false;
            }
            cp = (cp << 6) | (cb & 0x3F);
        }

        // Reject overlong encodings and surrogates / out-of-range.
        if (extra == 1 && cp < 0x80) { error = sha_errc::E_UTF8_INVALID; return false; }
        if (extra == 2 && cp < 0x800) { error = sha_errc::E_UTF8_INVALID; return false; }
        if (extra == 3 && cp < 0x10000) { error = sha_errc::E_UTF8_INVALID; return false; }
        if (cp > 0x10FFFF) { error = sha_errc::E_UTF8_INVALID; return false; }
        if (cp >= 0xD800 && cp <= 0xDFFF) { error = sha_errc::E_UTF8_INVALID; return false; }

        // Encode back to canonical minimal UTF-8 and emit.
        if (cp < 0x80) {
            out.push_back(static_cast<uint8_t>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<uint8_t>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<uint8_t>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<uint8_t>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
        }

        i += static_cast<size_t>(extra) + 1;
    }

    return true;
}

} // namespace hy3

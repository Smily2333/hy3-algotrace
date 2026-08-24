// hy3_algotrace — SHA-256
//
// A self-contained, dependency-free implementation of FIPS 180-4 SHA-256.
// Used to fingerprint the prompt template and each rendered prompt so that
// runs are auditable and reproducible (see docs/phase-02-protocol.md §9).
//
// Deliberately NOT using std::hash (not stable across platforms/versions).
// The implementation is validated against the NIST standard test vectors:
//   - ""            -> e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
//   - "abc"         -> ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
//   - 56-byte input -> 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
//
// Text normalization (UTF-8, no BOM, CRLF/CR -> LF, reject invalid UTF-8 / NUL)
// is provided here too so the hashing boundary is a single, well-defined place.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hy3 {

// Compute SHA-256 over raw bytes. Returns 64-char lowercase hex string.
std::string sha256_hex(const std::vector<uint8_t>& data);

// Convenience: hash a UTF-8 string viewed as bytes (no normalization).
inline std::string sha256_hex(const std::string& s) {
    return sha256_hex(std::vector<uint8_t>(s.begin(), s.end()));
}

// --- Text normalization (Phase 2A §9 / Phase 2B-1 spec) --------------------
//
// `normalizeUtf8` takes raw file bytes and produces canonical UTF-8 text:
//   * Rejects a UTF-8 BOM is NOT rejected at this layer; callers that want to
//     strip BOM should do so before. (We DO strip a leading BOM here.)
//   * CRLF (\r\n) and lone CR (\r) are normalized to LF (\n).
//   * Invalid UTF-8 sequences are rejected (returns false).
//   * Embedded NUL bytes are rejected (returns false).
//   * Trailing content is preserved verbatim except the above transforms; the
//     function does NOT trim leading/trailing whitespace of the *content*.
//
// On success `out` holds the normalized bytes and the function returns true.
// On failure `out` is cleared and the function returns false; `error` is set
// to a stable code: E_UTF8_INVALID or E_EMBEDDED_NUL.
bool normalizeUtf8(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out,
                   std::string& error);

// Strip a single leading UTF-8 BOM (EF BB BF) if present. Returns true if a
// BOM was removed (or absent), false never (helper always succeeds; presence
// is reported via the bool& param).
void stripBomIfPresent(std::vector<uint8_t>& data, bool& removed);

namespace sha_errc {
    inline constexpr const char* E_UTF8_INVALID = "E_UTF8_INVALID";
    inline constexpr const char* E_EMBEDDED_NUL  = "E_EMBEDDED_NUL";
} // namespace sha_errc

} // namespace hy3

// SHA-256 and HMAC-SHA256 (FIPS 180-4, RFC 2104). Small and portable, so the
// node, the main station and the host tests all sign radio frames identically.
#pragma once

#include <cstddef>
#include <cstdint>

namespace plantlink {

class Sha256 {
 public:
  static constexpr size_t kDigestLen = 32;
  static constexpr size_t kBlockLen = 64;

  Sha256() { reset(); }
  void reset();
  void update(const uint8_t* data, size_t len);
  void finish(uint8_t out[kDigestLen]);

 private:
  void block(const uint8_t* p);
  uint32_t h_[8];
  uint8_t buf_[kBlockLen];
  size_t bufLen_ = 0;
  uint64_t total_ = 0;
};

// HMAC-SHA256 over the concatenation of up to three parts (unused parts: len 0).
void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* a, size_t aLen, const uint8_t* b, size_t bLen,
                const uint8_t* c, size_t cLen, uint8_t out[Sha256::kDigestLen]);

// Constant-time comparison.
bool equalBytes(const uint8_t* a, const uint8_t* b, size_t n);

}  // namespace plantlink

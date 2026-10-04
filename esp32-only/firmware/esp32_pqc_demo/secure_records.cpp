#include "secure_records.h"
#include <mbedtls/md.h>
#include <string.h>

namespace secure_records {
namespace {
bool enabled = false;
uint8_t session[16], txKey[32], rxKey[32];
uint64_t txSequence = 0, rxSequence = 0;
void wipe(void *data, size_t n) {
  volatile uint8_t *p = static_cast<volatile uint8_t *>(data);
  while (n--) *p++ = 0;
}
bool mac(const uint8_t *key, const uint8_t *data, size_t n, uint8_t *out) {
  return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, 32, data, n, out) == 0;
}
bool equal(const uint8_t *a, const uint8_t *b, size_t n) {
  uint8_t difference = 0;
  for (size_t i = 0; i < n; ++i) difference |= a[i] ^ b[i];
  return difference == 0;
}
bool derive(const uint8_t *secret, const char *label, const uint8_t *hash, uint32_t epoch, uint8_t *out) {
  const char domain[] = "esp32-only/records/v1";
  uint8_t input[80];
  size_t n = sizeof(domain), len = strlen(label);
  memcpy(input, domain, n); memcpy(input+n, label, len); n += len;
  memcpy(input+n, hash, 32); n += 32;
  for (int i = 3; i >= 0; --i) input[n++] = uint8_t(epoch >> (i*8));
  const bool ok = mac(secret, input, n, out);
  wipe(input, sizeof(input));
  return ok;
}
}
void reset() {
  enabled = false; txSequence = rxSequence = 0;
  wipe(session, sizeof(session)); wipe(txKey, sizeof(txKey)); wipe(rxKey, sizeof(rxKey));
}
bool active() { return enabled; }
bool start(const uint8_t secret[32], const uint8_t hash[32], uint32_t epoch) {
  reset();
  uint8_t id[32];
  bool ok = derive(secret, "session", hash, epoch, id) &&
      derive(secret, "host", hash, epoch, rxKey) && derive(secret, "device", hash, epoch, txKey);
  if (ok) memcpy(session, id, 16);
  wipe(id, sizeof(id));
  if (!ok) reset();
  enabled = ok;
  return ok;
}
bool encode(uint8_t *packet, const uint8_t *data, size_t length) {
  if (!enabled || !length || length > MAX_PAYLOAD || txSequence == UINT64_MAX) return false;
  memcpy(packet, "PQR7", 4); memcpy(packet+4, session, 16);
  for (int i = 0; i < 8; ++i) packet[20+i] = uint8_t(txSequence >> (56-i*8));
  for (int i = 0; i < 4; ++i) packet[28+i] = uint8_t(length >> (24-i*8));
  memcpy(packet+HEADER, data, length);
  if (!mac(txKey, packet, HEADER+length, packet+HEADER+length)) return false;
  ++txSequence;
  return true;
}
bool inspect(const uint8_t *header, size_t &length) {
  if (!enabled || memcmp(header, "PQR7", 4) || !equal(header+4, session, 16)) return false;
  uint64_t sequence = 0;
  for (int i = 0; i < 8; ++i) sequence = (sequence << 8) | header[20+i];
  length = 0;
  for (int i = 0; i < 4; ++i) length = (length << 8) | header[28+i];
  return sequence == rxSequence && sequence != UINT64_MAX && length > 0 && length <= MAX_PAYLOAD;
}
bool verify(const uint8_t *packet, size_t length) {
  size_t announced = 0;
  if (!inspect(packet, announced) || announced != length) return false;
  uint8_t tag[32];
  const bool ok = mac(rxKey, packet, HEADER+length, tag) && equal(tag, packet+HEADER+length, TAG);
  wipe(tag, sizeof(tag));
  if (ok) ++rxSequence;
  return ok;
}
}

#pragma once
#include <stddef.h>
#include <stdint.h>

// Only the protocol task calls these functions. The worker never touches TCP,
// active session buffers, camera buffers, or the main task's DSA scratch space.
namespace pending_rekey {
enum State { EMPTY, WORKING, OFFER, CONFIRMED, FAILED };
bool prepare();  // reserve worker memory before mDNS fragments internal heap
bool start(const uint8_t context[72], const uint8_t *identitySecret);
bool install(const uint8_t ciphertext[1088]);
State state();
size_t reply(uint8_t *output);  // body: type + offer/proof + device timings
bool promote(const uint8_t token[32], uint8_t *pk, uint8_t *sk,
             uint8_t *ct, uint8_t *secret);
void discard();  // joins an outstanding job before wiping, never force-deletes it
}

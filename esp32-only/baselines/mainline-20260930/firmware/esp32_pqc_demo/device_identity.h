#pragma once
#include <Arduino.h>

// One atomic NVS blob holds both halves of the identity. Never regenerate a
// malformed existing identity silently: the host has pinned its public key.
bool loadOrCreateDeviceIdentity(uint8_t *publicKey, uint8_t *secretKey);
void printDeviceFingerprint(const uint8_t *publicKey);

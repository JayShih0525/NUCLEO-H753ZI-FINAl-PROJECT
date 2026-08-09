#ifndef ML_KEM_LIB_H
#define ML_KEM_LIB_H

#include <stdint.h>
#include "ml_kem_api.h"

#define MLKEM_OK                0x00
#define MLKEM_ERR_KEYPAIR       0xA1
#define MLKEM_ERR_ENCAPSULATE   0xA2
#define MLKEM_ERR_DECAPSULATE   0xA3

#define MLKEM_PUBLIC_KEY_SIZE   pqcrystals_kyber768_ref_PUBLICKEYBYTES
#define MLKEM_SECRET_KEY_SIZE   pqcrystals_kyber768_ref_SECRETKEYBYTES
#define MLKEM_CIPHERTEXT_SIZE   pqcrystals_kyber768_ref_CIPHERTEXTBYTES
#define MLKEM_SHARED_SIZE       pqcrystals_kyber768_ref_BYTES

uint8_t MLKEM_Keypair(uint8_t *public_key, uint8_t *secret_key);

uint8_t MLKEM_Encapsulate(
	uint8_t *ciphertext,
	uint8_t *shared_secret,
	const uint8_t *public_key
);

uint8_t MLKEM_Decapsulate(
	uint8_t *shared_secret,
	const uint8_t *ciphertext,
	const uint8_t *secret_key
);

#endif

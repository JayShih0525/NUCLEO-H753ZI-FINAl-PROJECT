#include "ml_kem_lib.h"

uint8_t MLKEM_Keypair(uint8_t *public_key, uint8_t *secret_key)
{
	if (pqcrystals_kyber768_ref_keypair(public_key, secret_key) != 0){
		return MLKEM_ERR_KEYPAIR;
	}

	return MLKEM_OK;
}

uint8_t MLKEM_Encapsulate(uint8_t *ciphertext, uint8_t *shared_secret, const uint8_t *public_key)
{
	if (pqcrystals_kyber768_ref_enc(ciphertext, shared_secret, public_key) != 0){
		return MLKEM_ERR_ENCAPSULATE;
	}

	return MLKEM_OK;
}

uint8_t MLKEM_Decapsulate(uint8_t *shared_secret, const uint8_t *ciphertext, const uint8_t *secret_key)
{
	if (pqcrystals_kyber768_ref_dec(shared_secret, ciphertext, secret_key) != 0){
		return MLKEM_ERR_DECAPSULATE;
	}

	return MLKEM_OK;
}

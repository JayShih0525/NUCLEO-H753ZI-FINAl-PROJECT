#ifndef ML_KEM_CBD_H
#define ML_KEM_CBD_H

#include <stdint.h>
#include "ml_kem_params.h"
#include "ml_kem_poly.h"

#define poly_cbd_eta1 KYBER_NAMESPACE(poly_cbd_eta1)
void poly_cbd_eta1(poly *r, const uint8_t buf[KYBER_ETA1*KYBER_N/4]);

#define poly_cbd_eta2 KYBER_NAMESPACE(poly_cbd_eta2)
void poly_cbd_eta2(poly *r, const uint8_t buf[KYBER_ETA2*KYBER_N/4]);

#endif

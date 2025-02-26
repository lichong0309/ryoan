#ifndef crypto_scalarmult_curve25519_H
#define crypto_scalarmult_curve25519_H

#include <stddef.h>

#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define crypto_scalarmult_curve25519_BYTES 32U
SODIUM_EXPORT
size_t crypto_scalarmult_curve25519_bytes(void);

#define crypto_scalarmult_curve25519_SCALARBYTES 32U
SODIUM_EXPORT
size_t crypto_scalarmult_curve25519_scalarbytes(void);

SODIUM_EXPORT
int crypto_scalarmult_curve25519(unsigned char *q, const unsigned char *n,
                                 const unsigned char *p)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_scalarmult_curve25519_base(unsigned char *q, const unsigned char *n);

/* ------------------------------------------------------------------------- */

int _crypto_scalarmult_curve25519_pick_best_implementation(void);

#ifdef __cplusplus
}
#endif

#endif

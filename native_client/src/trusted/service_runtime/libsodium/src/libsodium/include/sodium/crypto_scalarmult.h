#ifndef crypto_scalarmult_H
#define crypto_scalarmult_H

#include <stddef.h>

#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/crypto_scalarmult_curve25519.h"
#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define crypto_scalarmult_BYTES crypto_scalarmult_curve25519_BYTES
SODIUM_EXPORT
size_t  crypto_scalarmult_bytes(void);

#define crypto_scalarmult_SCALARBYTES crypto_scalarmult_curve25519_SCALARBYTES
SODIUM_EXPORT
size_t  crypto_scalarmult_scalarbytes(void);

#define crypto_scalarmult_PRIMITIVE "curve25519"
SODIUM_EXPORT
const char *crypto_scalarmult_primitive(void);

SODIUM_EXPORT
int crypto_scalarmult_base(unsigned char *q, const unsigned char *n);

SODIUM_EXPORT
int crypto_scalarmult(unsigned char *q, const unsigned char *n,
                      const unsigned char *p)
            __attribute__ ((warn_unused_result));

#ifdef __cplusplus
}
#endif

#endif

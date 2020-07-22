#ifndef crypto_verify_32_H
#define crypto_verify_32_H

#include <stddef.h>
#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define crypto_verify_32_BYTES 32U
SODIUM_EXPORT
size_t crypto_verify_32_bytes(void);

SODIUM_EXPORT
int crypto_verify_32(const unsigned char *x, const unsigned char *y)
            __attribute__ ((warn_unused_result));

#ifdef __cplusplus
}
#endif

#endif

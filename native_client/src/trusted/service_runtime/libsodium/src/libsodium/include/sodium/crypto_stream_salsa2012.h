#ifndef crypto_stream_salsa2012_H
#define crypto_stream_salsa2012_H

/*
 *  WARNING: This is just a stream cipher. It is NOT authenticated encryption.
 *  While it provides some protection against eavesdropping, it does NOT
 *  provide any security against active attacks.
 *  Unless you know what you're doing, what you are looking for is probably
 *  the crypto_box functions.
 */

#include <stddef.h>
#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/export.h"

#ifdef __cplusplus
# if __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_stream_salsa2012_KEYBYTES 32U
SODIUM_EXPORT
size_t crypto_stream_salsa2012_keybytes(void);

#define crypto_stream_salsa2012_NONCEBYTES 8U
SODIUM_EXPORT
size_t crypto_stream_salsa2012_noncebytes(void);

SODIUM_EXPORT
int crypto_stream_salsa2012(unsigned char *c, unsigned long long clen,
                            const unsigned char *n, const unsigned char *k);

SODIUM_EXPORT
int crypto_stream_salsa2012_xor(unsigned char *c, const unsigned char *m,
                                unsigned long long mlen, const unsigned char *n,
                                const unsigned char *k);

#ifdef __cplusplus
}
#endif

#endif

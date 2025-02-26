#ifndef crypto_secretbox_H
#define crypto_secretbox_H

#include <stddef.h>

#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/crypto_secretbox_xsalsa20poly1305.h"
#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/export.h"

#ifdef __cplusplus
# if __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_secretbox_KEYBYTES crypto_secretbox_xsalsa20poly1305_KEYBYTES
SODIUM_EXPORT
size_t  crypto_secretbox_keybytes(void);

#define crypto_secretbox_NONCEBYTES crypto_secretbox_xsalsa20poly1305_NONCEBYTES
SODIUM_EXPORT
size_t  crypto_secretbox_noncebytes(void);

#define crypto_secretbox_MACBYTES crypto_secretbox_xsalsa20poly1305_MACBYTES
SODIUM_EXPORT
size_t  crypto_secretbox_macbytes(void);

#define crypto_secretbox_PRIMITIVE "xsalsa20poly1305"
SODIUM_EXPORT
const char *crypto_secretbox_primitive(void);

SODIUM_EXPORT
int crypto_secretbox_easy(unsigned char *c, const unsigned char *m,
                          unsigned long long mlen, const unsigned char *n,
                          const unsigned char *k);

SODIUM_EXPORT
int crypto_secretbox_open_easy(unsigned char *m, const unsigned char *c,
                               unsigned long long clen, const unsigned char *n,
                               const unsigned char *k)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_secretbox_detached(unsigned char *c, unsigned char *mac,
                              const unsigned char *m,
                              unsigned long long mlen,
                              const unsigned char *n,
                              const unsigned char *k);

SODIUM_EXPORT
int crypto_secretbox_open_detached(unsigned char *m,
                                   const unsigned char *c,
                                   const unsigned char *mac,
                                   unsigned long long clen,
                                   const unsigned char *n,
                                   const unsigned char *k)
            __attribute__ ((warn_unused_result));

/* -- NaCl compatibility interface ; Requires padding -- */

#define crypto_secretbox_ZEROBYTES crypto_secretbox_xsalsa20poly1305_ZEROBYTES
SODIUM_EXPORT
size_t  crypto_secretbox_zerobytes(void);

#define crypto_secretbox_BOXZEROBYTES crypto_secretbox_xsalsa20poly1305_BOXZEROBYTES
SODIUM_EXPORT
size_t  crypto_secretbox_boxzerobytes(void);

SODIUM_EXPORT
int crypto_secretbox(unsigned char *c, const unsigned char *m,
                     unsigned long long mlen, const unsigned char *n,
                     const unsigned char *k);

SODIUM_EXPORT
int crypto_secretbox_open(unsigned char *m, const unsigned char *c,
                          unsigned long long clen, const unsigned char *n,
                          const unsigned char *k)
            __attribute__ ((warn_unused_result));

#ifdef __cplusplus
}
#endif

#endif

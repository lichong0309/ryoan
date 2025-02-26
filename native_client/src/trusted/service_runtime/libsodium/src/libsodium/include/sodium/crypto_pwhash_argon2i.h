#ifndef crypto_pwhash_argon2i_H
#define crypto_pwhash_argon2i_H

#include <stddef.h>

#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/export.h"

#ifdef __cplusplus
# if __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_pwhash_argon2i_ALG_ARGON2I13 1
SODIUM_EXPORT
int crypto_pwhash_argon2i_alg_argon2i13(void);

#define crypto_pwhash_argon2i_SALTBYTES 16U
SODIUM_EXPORT
size_t crypto_pwhash_argon2i_saltbytes(void);

#define crypto_pwhash_argon2i_STRBYTES 128U
SODIUM_EXPORT
size_t crypto_pwhash_argon2i_strbytes(void);

#define crypto_pwhash_argon2i_STRPREFIX "$argon2i$"
SODIUM_EXPORT
const char *crypto_pwhash_argon2i_strprefix(void);

#define crypto_pwhash_argon2i_OPSLIMIT_INTERACTIVE 4ULL
SODIUM_EXPORT
size_t crypto_pwhash_argon2i_opslimit_interactive(void);

#define crypto_pwhash_argon2i_MEMLIMIT_INTERACTIVE 33554432ULL
SODIUM_EXPORT
size_t crypto_pwhash_argon2i_memlimit_interactive(void);

#define crypto_pwhash_argon2i_OPSLIMIT_MODERATE 6ULL
SODIUM_EXPORT
size_t crypto_pwhash_argon2i_opslimit_moderate(void);

#define crypto_pwhash_argon2i_MEMLIMIT_MODERATE 134217728ULL
SODIUM_EXPORT
size_t crypto_pwhash_argon2i_memlimit_moderate(void);

#define crypto_pwhash_argon2i_OPSLIMIT_SENSITIVE 8ULL
SODIUM_EXPORT
size_t crypto_pwhash_argon2i_opslimit_sensitive(void);

#define crypto_pwhash_argon2i_MEMLIMIT_SENSITIVE 536870912ULL
SODIUM_EXPORT
size_t crypto_pwhash_argon2i_memlimit_sensitive(void);

SODIUM_EXPORT
int crypto_pwhash_argon2i(unsigned char * const out,
                          unsigned long long outlen,
                          const char * const passwd,
                          unsigned long long passwdlen,
                          const unsigned char * const salt,
                          unsigned long long opslimit, size_t memlimit,
                          int alg)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_pwhash_argon2i_str(char out[crypto_pwhash_argon2i_STRBYTES],
                              const char * const passwd,
                              unsigned long long passwdlen,
                              unsigned long long opslimit, size_t memlimit)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_pwhash_argon2i_str_verify(const char str[crypto_pwhash_argon2i_STRBYTES],
                                     const char * const passwd,
                                     unsigned long long passwdlen)
            __attribute__ ((warn_unused_result));

/* ------------------------------------------------------------------------- */

int _crypto_pwhash_argon2i_pick_best_implementation(void);

#ifdef __cplusplus
}
#endif

#endif


#include <stdint.h>

#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/crypto_stream_chacha20.h"
#include "../stream_chacha20.h"

extern struct crypto_stream_chacha20_implementation
    crypto_stream_chacha20_vec_implementation;

int
crypto_stream_chacha20_vec(unsigned char *c, unsigned long long clen,
                           const unsigned char *n, const unsigned char *k);

int
crypto_stream_chacha20_vec_xor_ic(unsigned char *c, const unsigned char *m,
                                  unsigned long long mlen,
                                  const unsigned char *n, uint64_t ic,
                                  const unsigned char *k);

int
crypto_stream_chacha20_ietf_vec(unsigned char *c, unsigned long long clen,
                                const unsigned char *n, const unsigned char *k);

int
crypto_stream_chacha20_ietf_vec_xor_ic(unsigned char *c, const unsigned char *m,
                                       unsigned long long mlen,
                                       const unsigned char *n, uint32_t ic,
                                       const unsigned char *k);

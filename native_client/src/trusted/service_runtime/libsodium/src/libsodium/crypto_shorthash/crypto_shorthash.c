
#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/crypto_shorthash.h"

size_t
crypto_shorthash_bytes(void)
{
    return crypto_shorthash_BYTES;
}

size_t
crypto_shorthash_keybytes(void)
{
    return crypto_shorthash_KEYBYTES;
}

const char *
crypto_shorthash_primitive(void)
{
    return crypto_shorthash_PRIMITIVE;
}

int
crypto_shorthash(unsigned char *out, const unsigned char *in,
                 unsigned long long inlen, const unsigned char *k)
{
    return crypto_shorthash_siphash24(out, in, inlen, k);
}

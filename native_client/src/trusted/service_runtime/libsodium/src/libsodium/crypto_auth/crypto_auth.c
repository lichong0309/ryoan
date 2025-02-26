
#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/crypto_auth.h"

size_t
crypto_auth_bytes(void)
{
    return crypto_auth_BYTES;
}

size_t
crypto_auth_keybytes(void)
{
    return crypto_auth_KEYBYTES;
}

const char *
crypto_auth_primitive(void)
{
    return crypto_auth_PRIMITIVE;
}

int
crypto_auth(unsigned char *out, const unsigned char *in,
            unsigned long long inlen, const unsigned char *k)
{
    return crypto_auth_hmacsha512256(out, in, inlen, k);
}

int
crypto_auth_verify(const unsigned char *h, const unsigned char *in,
                   unsigned long long inlen,const unsigned char *k)
{
    return crypto_auth_hmacsha512256_verify(h, in, inlen, k);
}

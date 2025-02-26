
#include <errno.h>

#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/crypto_pwhash.h"

int
crypto_pwhash_alg_argon2i13(void)
{
    return crypto_pwhash_ALG_ARGON2I13;
}

int
crypto_pwhash_alg_default(void)
{
    return crypto_pwhash_ALG_ARGON2I13;
}

size_t
crypto_pwhash_saltbytes(void)
{
    return crypto_pwhash_SALTBYTES;
}

size_t
crypto_pwhash_strbytes(void)
{
    return crypto_pwhash_STRBYTES;
}

const char *
crypto_pwhash_strprefix(void)
{
    return crypto_pwhash_STRPREFIX;
}

size_t
crypto_pwhash_opslimit_interactive(void)
{
    return crypto_pwhash_OPSLIMIT_INTERACTIVE;
}

size_t
crypto_pwhash_memlimit_interactive(void)
{
    return crypto_pwhash_MEMLIMIT_INTERACTIVE;
}

size_t
crypto_pwhash_opslimit_moderate(void)
{
    return crypto_pwhash_OPSLIMIT_MODERATE;
}

size_t
crypto_pwhash_memlimit_moderate(void)
{
    return crypto_pwhash_MEMLIMIT_MODERATE;
}

size_t
crypto_pwhash_opslimit_sensitive(void)
{
    return crypto_pwhash_OPSLIMIT_SENSITIVE;
}

size_t
crypto_pwhash_memlimit_sensitive(void)
{
    return crypto_pwhash_MEMLIMIT_SENSITIVE;
}

int
crypto_pwhash(unsigned char * const out, unsigned long long outlen,
              const char * const passwd, unsigned long long passwdlen,
              const unsigned char * const salt,
              unsigned long long opslimit, size_t memlimit, int alg)
{
    if (alg != crypto_pwhash_ALG_ARGON2I13) {
        errno = EINVAL;
        return -1;
    }
    return crypto_pwhash_argon2i(out, outlen, passwd, passwdlen, salt,
                                 opslimit, memlimit, alg);
}

int
crypto_pwhash_str(char out[crypto_pwhash_STRBYTES],
                  const char * const passwd, unsigned long long passwdlen,
                  unsigned long long opslimit, size_t memlimit)
{
    return crypto_pwhash_argon2i_str(out, passwd, passwdlen,
                                     opslimit, memlimit);
}

int
crypto_pwhash_str_verify(const char str[crypto_pwhash_STRBYTES],
                         const char * const passwd,
                         unsigned long long passwdlen)
{
    return crypto_pwhash_argon2i_str_verify(str, passwd, passwdlen);
}

const char *
crypto_pwhash_primitive(void) {
    return crypto_pwhash_PRIMITIVE;
}

/*
version 20080914
D. J. Bernstein
Public domain.
*/

#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/crypto_core_hsalsa20.h"
#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/crypto_stream_salsa20.h"
#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/crypto_stream_xsalsa20.h"
#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/utils.h"

int crypto_stream_xsalsa20(
        unsigned char *c,unsigned long long clen,
  const unsigned char *n,
  const unsigned char *k
)
{
  unsigned char subkey[32];
  int ret;
  crypto_core_hsalsa20(subkey,n,k,NULL);
  ret = crypto_stream_salsa20(c,clen,n + 16,subkey);
  sodium_memzero(subkey, sizeof subkey);
  return ret;
}

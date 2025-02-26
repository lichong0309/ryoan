#ifndef sodium_version_H
#define sodium_version_H

#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/export.h"

#define SODIUM_VERSION_STRING "1.0.10"

#define SODIUM_LIBRARY_VERSION_MAJOR 9
#define SODIUM_LIBRARY_VERSION_MINOR 2

#ifdef __cplusplus
extern "C" {
#endif

SODIUM_EXPORT
const char *sodium_version_string(void);

SODIUM_EXPORT
int         sodium_library_version_major(void);

SODIUM_EXPORT
int         sodium_library_version_minor(void);

#ifdef __cplusplus
}
#endif

#endif

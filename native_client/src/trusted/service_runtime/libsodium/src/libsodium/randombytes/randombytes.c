
#include <stdlib.h>
#include <sys/types.h>

#include <assert.h>
#include <limits.h>
#include <stdint.h>

#ifdef __EMSCRIPTEN__
# include <emscripten.h>
#endif

#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/randombytes.h"
#include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/randombytes_sysrandom.h"

#ifdef __native_client__
# include "native_client/src/trusted/service_runtime/libsodium/src/libsodium/include/sodium/randombytes_nativeclient.h"
#endif

/* C++Builder defines a "random" macro */
#undef random

#ifndef __EMSCRIPTEN__
#ifdef __native_client__
static const randombytes_implementation *implementation =
    &randombytes_nativeclient_implementation;
#else
static const randombytes_implementation *implementation =
    &randombytes_sysrandom_implementation;
#endif
#else
static const randombytes_implementation *implementation = NULL;
#endif

int
randombytes_set_implementation(randombytes_implementation *impl)
{
    implementation = impl;

    return 0;
}

const char *
randombytes_implementation_name(void)
{
#ifndef __EMSCRIPTEN__
    return implementation->implementation_name();
#else
    return "js";
#endif
}

uint32_t
randombytes_random(void)
{
#ifndef __EMSCRIPTEN__
    return implementation->random();
#else
    return EM_ASM_INT_V({
        return Module.getRandomValue();
    });
#endif
}

void
randombytes_stir(void)
{
#ifndef __EMSCRIPTEN__
    if (implementation != NULL && implementation->stir != NULL) {
        implementation->stir();
    }
#else
    EM_ASM({
        if (Module.getRandomValue === undefined) {
            try {
                var window_ = "object" === typeof window ? window : self,
                    crypto_ = typeof window_.crypto !== "undefined" ? window_.crypto : window_.msCrypto,
                    randomValuesStandard = function() {
                        var buf = new Uint32Array(1);
                        crypto_.getRandomValues(buf);
                        return buf[0] >>> 0;
                    };
                randomValuesStandard();
                Module.getRandomValue = randomValuesStandard;
            } catch (e) {
                try {
                    var crypto = require('crypto'),
                        randomValueNodeJS = function() {
                            var buf = crypto.randomBytes(4);
                            return (buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3]) >>> 0;
                        };
                    randomValueNodeJS();
                    Module.getRandomValue = randomValueNodeJS;
                } catch (e) {
                    throw 'No secure random number generator found';
                }
            }
        }
    });
#endif
}

/*
 * randombytes_uniform() derives from OpenBSD's arc4random_uniform()
 * Copyright (c) 2008, Damien Miller <djm@openbsd.org>
 */
uint32_t
randombytes_uniform(const uint32_t upper_bound)
{
    uint32_t min;
    uint32_t r;

#ifdef __EMSCRIPTEN__
    if (implementation != NULL && implementation->uniform != NULL) {
        return implementation->uniform(upper_bound);
    }
#else
    if (implementation->uniform != NULL) {
        return implementation->uniform(upper_bound);
    }
#endif
    if (upper_bound < 2) {
        return 0;
    }
    min = (uint32_t) (-upper_bound % upper_bound);
    do {
        r = randombytes_random();
    } while (r < min);

    return r % upper_bound;
}

void
randombytes_buf(void * const buf, const size_t size)
{
#ifndef __EMSCRIPTEN__
    if (size > (size_t) 0U) {
        implementation->buf(buf, size);
    }
#else
    unsigned char *p = buf;
    size_t         i;

    for (i = (size_t) 0U; i < size; i++) {
        p[i] = (unsigned char) randombytes_random();
    }
#endif
}

int
randombytes_close(void)
{
    if (implementation != NULL && implementation->close != NULL) {
        return implementation->close();
    }
    return 0;
}

void
randombytes(unsigned char * const buf, const unsigned long long buf_len)
{
    assert(buf_len <= SIZE_MAX);
    randombytes_buf(buf, (size_t) buf_len);
}

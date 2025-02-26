/*
 * Copyright (c) 2011 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * RAII wrappers for nacl_sync objects.  C++ only header file
 */

#ifndef NATIVE_CLIENT_SRC_SHARED_PLATFORM_NACL_SYNC_RAII_H_
#define NATIVE_CLIENT_SRC_SHARED_PLATFORM_NACL_SYNC_RAII_H_

#include "native_client/src/include/portability.h"  // NACL_PRIxPTR etc
#include "native_client/src/shared/platform/nacl_sync_checked.h"

namespace nacl {

class MutexLocker {
 public:
  explicit MutexLocker(NaClMutex* mu)
      : mu_(mu) {
    NaClXMutexLock(mu_);
  }
  ~MutexLocker() {
    NaClXMutexUnlock(mu_);
  }
 private:
  NaClMutex* mu_;
};

}  // namespace nacl

#endif  // NATIVE_CLIENT_SRC_SHARED_PLATFORM_NACL_SYNC_RAII_H_

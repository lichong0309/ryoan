/*
 * Copyright (c) 2016 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef NATIVE_CLIENT_SRC_TRUSTED_SERVICE_RUNTIME_FILE_NAME_H_
#define NATIVE_CLIENT_SRC_TRUSTED_SERVICE_RUNTIME_FILE_NAME_H_

struct file_name {
  struct file_name *next;
  char* name;
};

#endif







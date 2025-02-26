/*
 * Copyright (c) 2016 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "native_client/src/trusted/service_runtime/sel_ldr_filename.h"

#include <errno.h>
#include <string.h>

#include "native_client/src/include/build_config.h"
#include "native_client/src/shared/platform/nacl_check.h"
#include "native_client/src/trusted/service_runtime/include/sys/errno.h"
#include "native_client/src/trusted/service_runtime/nacl_copy.h"
#include "native_client/src/trusted/service_runtime/nacl_syscall_common.h"
#include "native_client/src/trusted/service_runtime/sel_ldr.h"


/*
 * Verifies the path is prefixed by the root directory of the restricted
 * filesystem.
 *
 * @param[in] path The path being checked.
 * @param[in] path_len The length of |path|.
 * @return 0 if path does NOT contain the prefix, else 1.
 */
static int PathContainsRootPrefix(const char *path, size_t path_len) {
  /* The full path must be at least as long as the root to contain it. */
  if (path_len < NaClRootDirLen)
    return 0;
  /* The first 'NaClRootDirLen' bytes must match. */
  if (strncmp(NaClRootDir, path, NaClRootDirLen))
    return 0;
  /* If the root is "/foo", then "/foobar" should be marked as invalid. */
  if (path_len > NaClRootDirLen && path[NaClRootDirLen] != '/')
    return 0;
  return 1;
}

#if !NACL_WINDOWS
/*
 * Given a |virtual_path| (a path supplied by the user with no knowledge of
 * the mounted directory) transform it into an |absolute_path|, which is an
 * absolute path prefixed by the root mount directory.
 *
 * TODO(smklein): The virtual_path is assumed to be absolute. Change this.
 *
 * @param[in] virtual_path Virtual path supplied by user.
 * @param[out] absolute_path The absolute path referenced by the |virtual_path|.
 * @param[in] absolute_path_size The size of the |absolute_path| buffer.
 * @return 0 on success, else a negated NaCl errno.
 */
static uint32_t VirtualToAbsolutePath(const char *virtual_path,
                                      char       *absolute_path,
                                      size_t     absolute_path_max_size) {
  size_t virtual_path_len = strlen(virtual_path);
  /* Check that we have enough room to prepend the prefix (absolute case). */
  if (virtual_path_len + NaClRootDirLen + 1 > absolute_path_max_size) {
    NaClLog(LOG_ERROR, "Pathname too long: %s\n", virtual_path);
    return -NACL_ABI_ENAMETOOLONG;
  }

  /* Prefix */
  memcpy(absolute_path, NaClRootDir, NaClRootDirLen);
  /* Prefix + Virtual Path */
  memcpy(absolute_path + NaClRootDirLen, virtual_path, virtual_path_len);
  /* Prefix + Virtual Path + Terminator */
  absolute_path[virtual_path_len + NaClRootDirLen] = '\0';

  return 0;
}

/*
 * Determine if |path| points to a symbolic link.
 *
 * @param[in] path Path of file to be checked.
 * @return Nonzero if path is symbolic link.
 */
static int IsSymbolicLink(const char *path) {
  struct stat buf;
  int result = lstat(path, &buf);
  return result == 0 && S_ISLNK(buf.st_mode);
}

/*
 * Preconditions:
 *   The path is absolute (aka, it starts with "/").
 *
 * @param[in] path The path to be verified.
 * @return 0 if the path is valid, else a negated NaCl errno.
 */
static uint32_t ValidateAbsolutePath(const char *path) {
  if (strstr(path, "..")) {
    NaClLog(LOG_ERROR, "Pathname contains ..: %s\n", path);
    return -NACL_ABI_EACCES;
  }

  CHECK(PathContainsRootPrefix(path, strlen(path)));

  /*
   * This is an informal check, and we still require the users of sel_ldr to
   * ensure that no symbolic links exist in the mounted directory.
   *
   * A race condition exists that can bypass this check:
   * 1) Open (or any file access function) called on a path to a regular
   *    file (PATH_REG). "IsSymbolicLink" returns zero, but the original file
   *    access function has not yet been called.
   * 2) Outside of sel_ldr, a symbolic link is moved into PATH_REG.
   * 3) The original file access function is called on a symbolic link.
   *
   * Thus, we still require the caller of sel_ldr to guarantee that no symbolic
   * links are inside the mounted directory.
   */
  if (IsSymbolicLink(path)) {
    return -NACL_ABI_EACCES;
  }
  return 0;
}

/*
 * Transforms a raw file path from the user into an absolute path
 * prefixed by the mounted file system root. Also validates the path to
 * ensure it does not access anything outside the mount point.
 *
 * @param[in/out] dest The raw file path from the user
 * @param[in] dest_max_size The size of the buffer holding dest.
 * @return 0 on success, else a NaCl errno.
 */
static uint32_t CopyHostPathMounted(char *dest, size_t dest_max_size) {
  uint32_t retval;
  char     raw_path[NACL_CONFIG_PATH_MAX];

  if (dest_max_size <= 0 || dest[0] == '\0') {
    NaClLog(LOG_ERROR, "Dest cannot be empty path\n");
    return -NACL_ABI_ENOENT;
  } else if (dest[0] != '/') {
    /* TODO(smklein): Allow usage of relative paths. */
    NaClLog(LOG_ERROR, "Pathname is not absolute: %s\n", dest);
    return -NACL_ABI_EACCES;
  }

  CHECK(dest_max_size == NACL_CONFIG_PATH_MAX);
  CHECK(strlen(dest) < NACL_CONFIG_PATH_MAX);
  strcpy(raw_path, dest);

  /* Transform the user's raw path into an absolute path. */
  retval = VirtualToAbsolutePath(raw_path, dest, dest_max_size);
  if (retval != 0)
    return retval;

  /* Verify that the path cannot escape root. */
  return ValidateAbsolutePath(dest);
}
#endif /* !NACL_WINDOWS */

uint32_t CopyHostPathInFromUser(struct NaClApp *nap,
                                char           *dest,
                                size_t         dest_max_size,
                                uint32_t       src) {
  /*
   * NaClCopyInFromUserZStr may (try to) get bytes that is outside the
   * app's address space and generate a fault.
   */
  if (!NaClCopyInFromUserZStr(nap, dest, dest_max_size, src)) {
    if (dest[0] == '\0') {
      NaClLog(LOG_ERROR, "NaClSys: invalid address for pathname\n");
      return (uint32_t) -NACL_ABI_EFAULT;
    }

    NaClLog(LOG_ERROR, "NaClSys: pathname string too long\n");
    return (uint32_t) -NACL_ABI_ENAMETOOLONG;
  }

  /*
   * Without the '-m' option, this function should act like a simple
   * raw path copy.
   */
  if (NaClRootDir == NULL) {
    return 0;
  }
#if NACL_WINDOWS
  return 0;
#else
  return CopyHostPathMounted(dest, dest_max_size);
#endif /* NACL_WINDOWS */
}

uint32_t CopyHostPathOutToUser(struct NaClApp *nap,
                               uint32_t        dst_usr_addr,
                               char           *path) {
  /*
   * The input |path| to CopyHostPathOutToUser is the result of an operation on
   * a buffer (such as getcwd) which has a size supplied by the user. The result
   * path which is copied to dst_usr_addr must be the same length as |path| (or
   * shorter) to fit inside the user-allocated buffer.
   */
  size_t path_len = strlen(path);
  /* Check if we need to perform any path sanitization. */
  if (NaClRootDir == NULL) {
    if (!NaClCopyOutToUser(nap, dst_usr_addr, path, path_len + 1))
      return (uint32_t) -NACL_ABI_EFAULT;
    return 0;
  }

  if (!PathContainsRootPrefix(path, path_len))
    return (uint32_t) -NACL_ABI_EACCES;

  if (path[NaClRootDirLen] == '\0') {
    /*
     * In this case, the path we're copying out *is* NaClRootDir, which means
     * it doesn't end with a trailing slash, and all we want to do is return
     * a slash. Special case.
     *
     * 2 bytes copied for slash + null terminator.
     */
    if (!NaClCopyOutToUser(nap, dst_usr_addr, "/", 2))
      return (uint32_t) -NACL_ABI_EFAULT;
    return 0;
  }

  /* Copy out everything after the root dir (including the slash). */
  if (!NaClCopyOutToUser(nap, dst_usr_addr, path + NaClRootDirLen,
                         path_len - NaClRootDirLen + 1))
    return (uint32_t) -NACL_ABI_EFAULT;
  return 0;
}

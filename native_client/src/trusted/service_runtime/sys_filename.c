/*
 * Copyright (c) 2013 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "native_client/src/trusted/service_runtime/security.h"
#include "native_client/src/trusted/service_runtime/sys_filename.h"

#include <string.h>

#include "native_client/src/shared/platform/nacl_check.h"
#include "native_client/src/shared/platform/nacl_host_desc.h"
#include "native_client/src/shared/platform/nacl_host_dir.h"
#include "native_client/src/trusted/desc/nacl_desc_dir.h"
#include "native_client/src/trusted/desc/nacl_desc_io.h"
#include "native_client/src/trusted/service_runtime/hashtable.h"
#include "native_client/src/trusted/service_runtime/include/sys/errno.h"
#include "native_client/src/trusted/service_runtime/include/sys/fcntl.h"
#include "native_client/src/trusted/service_runtime/include/sys/stat.h"
#include "native_client/src/trusted/service_runtime/include/sys/unistd.h"
#include "native_client/src/trusted/service_runtime/nacl_app_thread.h"
#include "native_client/src/trusted/service_runtime/nacl_copy.h"
#include "native_client/src/trusted/service_runtime/nacl_file_buffer.h"
#include "native_client/src/trusted/service_runtime/nacl_syscall_common.h"
#include "native_client/src/trusted/service_runtime/sel_ldr.h"


static uint32_t CopyPathFromUser(struct NaClApp *nap,
                                 char           *dest,
                                 size_t         num_bytes,
                                 uintptr_t      src) {
  /*
   * NaClCopyInFromUserZStr may (try to) get bytes that is outside the
   * app's address space and generate a fault.
   */
  if (!NaClCopyInFromUserZStr(nap, dest, num_bytes, src)) {
    if (dest[0] == '\0') {
      NaClLog(LOG_ERROR, "NaClSys: invalid address for pathname\n");
      return (uint32_t) -NACL_ABI_EFAULT;
    }

    NaClLog(LOG_ERROR, "NaClSys: pathname string too long\n");
    return (uint32_t) -NACL_ABI_ENAMETOOLONG;
  }

  return 0;
}

int32_t NaClSysOpenUntainted(struct NaClAppThread  *natp,
                    char                  *path,
                    int                   flags,
                    int                   mode) {
  struct NaClApp       *nap = natp->nap;
  uint32_t             retval = (uint32_t) -NACL_ABI_EINVAL;
  nacl_host_stat_t     stbuf;
  int                  allowed_flags;

  if (!NaClAclBypassChecks) {
    return -NACL_ABI_EACCES;
  }

  allowed_flags = (NACL_ABI_O_ACCMODE | NACL_ABI_O_CREAT | NACL_ABI_O_EXCL
                   | NACL_ABI_O_TRUNC | NACL_ABI_O_APPEND
                   | NACL_ABI_O_DIRECTORY);
  if (0 != (flags & ~allowed_flags)) {
    NaClLog(LOG_WARNING, "Invalid open flags 0%o, ignoring extraneous bits\n",
            flags);
    flags &= allowed_flags;
  }
  if (0 != (mode & ~0600)) {
    NaClLog(1, "IGNORING Invalid access mode bits 0%o\n", mode);
    mode &= 0600;
  }

  /*
   * Perform a stat to determine whether the file is a directory.
   *
   * NB: it is okay for the stat to fail, since the request may be to
   * create a new file.
   *
   * There is a race conditions here: between the stat and the
   * open-as-a-file and open-as-a-dir, the type of the object that the
   * path refers to can change.
   */
  retval = NaClHostDescStat(path, &stbuf);

  /* Windows does not have S_ISDIR(m) macro */
  if (0 == retval && S_IFDIR == (S_IFDIR & stbuf.st_mode)) {
    struct NaClHostDir  *hd;
    /*
     * Directories cannot be opened with O_EXCL. Technically, due to the above
     * race condition we might no longer be dealing with a directory, but
     * until the race is fixed this is best we can do.
     */
    if (flags & NACL_ABI_O_EXCL) {
      retval = (uint32_t) -NACL_ABI_EEXIST;
      goto cleanup;
    }

    hd = malloc(sizeof *hd);
    if (NULL == hd) {
      retval = (uint32_t) -NACL_ABI_ENOMEM;
      goto cleanup;
    }
    retval = NaClHostDirOpen(hd, path);
    NaClLog(1, "NaClHostDirOpen(0x%08"NACL_PRIxPTR", %s) returned %d\n",
            (uintptr_t) hd, path, retval);
    if (0 == retval) {
      retval = NaClAppSetDescAvail(
          nap, (struct NaClDesc *) NaClDescDirDescMake(hd));
      NaClLog(1, "Entered directory into open file table at %d\n",
              retval);
    }
  } else {
    struct NaClHostDesc  *hd;

    if (flags & NACL_ABI_O_DIRECTORY) {
      retval = (uint32_t) -NACL_ABI_ENOTDIR;
      goto cleanup;
    }

    hd = malloc(sizeof *hd);
    if (NULL == hd) {
      retval = (uint32_t) -NACL_ABI_ENOMEM;
      goto cleanup;
    }
    retval = NaClHostDescOpen(hd, path, flags, mode);
    NaClLog(1,
            "NaClHostDescOpen(0x%08"NACL_PRIxPTR", %s, 0%o, 0%o) returned %d\n",
            (uintptr_t) hd, path, flags, mode, retval);
    if (0 == retval) {
      struct NaClDesc *desc = (struct NaClDesc *) NaClDescIoDescMake(hd);
      if ((flags & NACL_ABI_O_ACCMODE) == NACL_ABI_O_RDONLY) {
        /*
         * Let any read-only open be used for PROT_EXEC mmap
         * calls.  Under -a, the user informally warrants that
         * files' code segments won't be changed after open.
         */
        NaClDescSetFlags(desc,
                         NaClDescGetFlags(desc) | NACL_DESC_FLAGS_MMAP_EXEC_OK);
      }
      retval = NaClAppSetDescAvail(nap, desc);
      //NaClLog(1, "Entered into open file table at %d\n", retval);
    }
  }
cleanup:
  return retval;
}

int32_t NaClSysOpen(struct NaClAppThread  *natp,
                    uint32_t              pathname,
                    int                   flags,
                    int                   mode) {
  struct NaClApp       *nap = natp->nap;
  uint32_t             retval = (uint32_t) -NACL_ABI_EINVAL;
  char                 path[NACL_CONFIG_PATH_MAX];
  /* nacl_host_stat_t     stbuf; */
  int                  allowed_flags;
  struct NaClDesc      *desc;
  int                  in_mem;

  NaClLog(3, "NaClSysOpen(0x%08"NACL_PRIxPTR", "
          "0x%08"NACL_PRIx32", 0x%x, 0x%x)\n",
          (uintptr_t) natp, pathname, flags, mode);

  retval = CopyPathFromUser(nap, path, sizeof path, (uintptr_t) pathname);
  if (0 != retval)
    goto cleanup;
  desc = cfuhash_get(nap->file_name_tbl, path);
  in_mem = !memcmp(path, ":mem:", 5);

  // NaClSemWait(&nap->request_sem);
  if (!in_mem && !desc && !NaClAppIsConfined()) {
    //NaClLog(LOG_INFO, "Get to untaint version\n");
    // NaClSemPost(&nap->request_sem);
    return NaClSysOpenUntainted(natp, path, flags, mode);
  }
  // NaClSemPost(&nap->request_sem);

  if (!NaClAclBypassChecks) {
    return -NACL_ABI_EACCES;
  }

  allowed_flags = (NACL_ABI_O_ACCMODE | NACL_ABI_O_CREAT
                   | NACL_ABI_O_TRUNC | NACL_ABI_O_APPEND
                   | NACL_ABI_O_EXCL);
  if (0 != (flags & ~allowed_flags)) {
    NaClLog(LOG_WARNING, "Invalid open flags 0%o, ignoring extraneous bits\n",
            flags);
    flags &= allowed_flags;
  }
  if (0 != (mode & ~0600)) {
    NaClLog(1, "IGNORING Invalid access mode bits 0%o\n", mode);
    mode &= 0600;
  }

  if (NULL == desc) {
    if (in_mem) {
      struct NaClFileBuffer *fbp;
      struct NaClHostDesc   *hd;

      fbp = (struct NaClFileBuffer *) malloc(sizeof *fbp);
      if (NULL == fbp) {
        NaClLog(LOG_FATAL,
                "NaClSysOpen: no memory for file buffer\n");
      }
      if (!NaClFileBufferCtor(fbp)) {
        NaClLog(LOG_FATAL,
                ("NaClSysOpen:"
                 " NaClFileBufferCtor(0x%08"NACL_PRIxPTR") failed\n"),
                (uintptr_t) fbp);
      }
      fbp->writable = 1;
      if (flags & NACL_ABI_O_APPEND) {
        fbp->pos = fbp->len;
      } else {
        fbp->pos = 0;
      }
      hd = (struct NaClHostDesc *) malloc(sizeof *hd);
      // NaClSemWait(&nap->file_buffer_sem);
      nap->max_host_fd ++;
      NaClAppSetFileBufferSem(nap, nap->max_host_fd, fbp);
      // NaClSemPost(&nap->file_buffer_sem);
      retval = NaClHostDescCtor(hd, nap->max_host_fd, NACL_ABI_O_RDWR);
      hd->fake = 1;
      if (0 == retval) {
        struct NaClDesc *desc = (struct NaClDesc *) NaClDescIoDescMake(hd);
        retval = NaClAppSetDescAvail(nap, desc);
        //NaClLog(LOG_INFO, "Entered into open file table at %d filename %s\n", retval, path);
        cfuhash_put(nap->file_name_tbl, path, desc);
      }
    } else {
      NaClLog(LOG_ERROR, "Cannot open files that are not preloaded (%s)\n", path);
      return -NACL_ABI_EACCES;
    }
  } else {
    struct NaClFileBuffer *fbp;
    if (flags & NACL_ABI_O_EXCL) {
      retval = -NACL_ABI_EEXIST;
      goto cleanup;
    }
    NaClDescRef(desc);
    fbp = NaClAppGetFileBufferSem(nap, desc);
    if (flags & NACL_ABI_O_TRUNC) {
      fbp->len = 0;
    }
    if (flags & NACL_ABI_O_APPEND) {
      fbp->pos = fbp->len;
    } else {
      fbp->pos = 0;
    }
    NaClFileBufferSafeUnref(fbp);
    retval = NaClAppSetDescAvail(nap, desc);
    //NaClLog(LOG_INFO, "Entered into open file table at %d filename %s\n", retval, path);
    NaClDescSafeUnref(desc);
  }
cleanup:
  return retval;
}

int32_t NaClSysStatUntainted(struct NaClAppThread  *natp,
                    uint32_t              pathname,
                    uint32_t              nasp) {
  struct NaClApp      *nap = natp->nap;
  int32_t             retval = -NACL_ABI_EINVAL;
  char                path[NACL_CONFIG_PATH_MAX];
  nacl_host_stat_t    stbuf;

  NaClLog(3,
          ("Entered NaClSysStat(0x%08"NACL_PRIxPTR", 0x%08"NACL_PRIx32","
           " 0x%08"NACL_PRIx32")\n"), (uintptr_t) natp, pathname, nasp);

  if (!NaClAclBypassChecks) {
    return -NACL_ABI_EACCES;
  }

  retval = CopyPathFromUser(nap, path, sizeof path, pathname);
  if (0 != retval)
    goto cleanup;

  /*
   * Perform a host stat.
   */
  retval = NaClHostDescStat(path, &stbuf);
  if (0 == retval) {
    struct nacl_abi_stat abi_stbuf;

    retval = NaClAbiStatHostDescStatXlateCtor(&abi_stbuf, &stbuf);
    if (!NaClCopyOutToUser(nap, nasp, &abi_stbuf, sizeof abi_stbuf)) {
      retval = -NACL_ABI_EFAULT;
    }
  }
cleanup:
  return retval;
}

int32_t NaClSysStat(struct NaClAppThread  *natp,
                    uint32_t              pathname,
                    uint32_t              nasp) {
  struct NaClApp        *nap = natp->nap;
  int32_t               retval = -NACL_ABI_EINVAL;
  char                  path[NACL_CONFIG_PATH_MAX];
  nacl_host_stat_t      stbuf;
  struct NaClFileBuffer *fbp = NULL;
  struct NaClDesc       *desc = NULL;

  // NaClSemWait(&nap->request_sem);
  if (!NaClAppIsConfined()) {
    // NaClSemPost(&nap->request_sem);
    return NaClSysStatUntainted(natp, pathname, nasp);
  }
  // NaClSemPost(&nap->request_sem);

  NaClLog(3,
          ("Entered NaClSysStat(0x%08"NACL_PRIxPTR", 0x%08"NACL_PRIx32","
           " 0x%08"NACL_PRIx32")\n"), (uintptr_t) natp, pathname, nasp);

  if (!NaClAclBypassChecks) {
    return -NACL_ABI_EACCES;
  }

  retval = CopyPathFromUser(nap, path, sizeof path, pathname);
  if (0 != retval)
    goto cleanup;

  desc = cfuhash_get(nap->file_name_tbl, path);
  if (NULL != desc) {
    NaClDescRef(desc);
  } else {
    NaClLog(LOG_ERROR, "Cannot stat files that are not preloaded\n");
    return -NACL_ABI_EACCES;
  }
  fbp = NaClAppGetFileBufferSem(nap, desc);
  if (NULL == fbp) {
    retval = -NACL_ABI_EINVAL;
  }
  if (0 == retval) {
    struct nacl_abi_stat abi_stbuf;

    retval = NaClAbiStatHostDescStatXlateCtor(&abi_stbuf, &stbuf);
    if (!NaClCopyOutToUser(nap, nasp, &abi_stbuf, sizeof abi_stbuf)) {
      retval = -NACL_ABI_EFAULT;
    }
  }
  NaClDescSafeUnref(desc);
  NaClFileBufferSafeUnref(fbp);
cleanup:
  return retval;
}

int32_t NaClSysMkdir(struct NaClAppThread *natp,
                     uint32_t             pathname,
                     int                  mode) {
  struct NaClApp *nap = natp->nap;
  char           path[NACL_CONFIG_PATH_MAX];
  int32_t        retval = -NACL_ABI_EINVAL;
  int  in_mem;

  retval = CopyPathFromUser(nap, path, sizeof path, pathname);
  if (0 != retval)
    goto cleanup;
  in_mem = !memcmp(path, ":mem:", 5);

  // NaClSemWait(&nap->request_sem);
  if (!NaClAclBypassChecks || (!in_mem && NaClAppIsConfined())) {
    retval = -NACL_ABI_EACCES;
    // NaClSemPost(&nap->request_sem);
    goto cleanup;
  }
  // NaClSemPost(&nap->request_sem);

  if (in_mem) {
    retval = 0;
  } else {
    retval = NaClHostDescMkdir(path, mode);
  }
cleanup:
  return retval;
}

struct rm_args {
  const char *prefix;
  struct NaClApp *nap;
};

static int
htab_remove_func(void *key, size_t key_size, void *data, size_t data_size, void *arg) {
  char *k = (char *)key;
  struct rm_args *rmargs = (struct rm_args*)arg;
  const char *prefix = rmargs->prefix;
  struct NaClApp *nap = rmargs->nap;
  struct NaClDesc       *desc = (struct NaClDesc *)data;
  struct NaClFileBuffer *fbp;
  (void)(key_size);
  (void)(data_size);

  if (strncmp(prefix, k, strlen(prefix)) != 0) {
    return 0;
  }
  fbp = NaClAppGetFileBufferSem(nap, desc);
  if (fbp) {
    int d = ((struct NaClDescIoDesc *) desc)->hd->d;
    NaClAppSetFileBuffer(nap, d, NULL);
    NaClFileBufferUnref(fbp);
    NaClDescUnref(desc);
  }
  return 1;
}

int32_t NaClSysRmdir_internal(struct NaClApp *nap, const char *path) {
  int  in_mem;
  struct rm_args rmargs;
  int32_t        retval = -NACL_ABI_EINVAL;

  in_mem = !memcmp(path, ":mem:", 5);

  // NaClSemWait(&nap->request_sem);
  if (!NaClAclBypassChecks || (!in_mem && NaClAppIsConfined())) {
    retval = -NACL_ABI_EACCES;
    // NaClSemPost(&nap->request_sem);
    return retval;
  }
  // NaClSemPost(&nap->request_sem);

  rmargs.prefix = path;
  rmargs.nap = nap;

  if (in_mem) {
    cfuhash_foreach_remove(nap->file_name_tbl,
                           (cfuhash_remove_fn_t)htab_remove_func,
                           NULL, &rmargs);
    retval = 0;
  } else {
    retval = NaClHostDescRmdir(path);
  }
  return retval;
}

int32_t NaClSysRmdir(struct NaClAppThread *natp,
                     uint32_t             pathname) {
  struct NaClApp *nap = natp->nap;
  char           path[NACL_CONFIG_PATH_MAX];
  int32_t        retval = -NACL_ABI_EINVAL;

  retval = CopyPathFromUser(nap, path, sizeof path, pathname);
  if (0 != retval)
      return retval;
  else
      return NaClSysRmdir_internal(nap, path);
}

int32_t NaClSysChdir(struct NaClAppThread *natp,
                     uint32_t             pathname) {
  struct NaClApp *nap = natp->nap;
  char           path[NACL_CONFIG_PATH_MAX];
  int32_t        retval = -NACL_ABI_EINVAL;

  // NaClSemWait(&nap->request_sem);
  if (!NaClAclBypassChecks || NaClAppIsConfined()) {
    retval = -NACL_ABI_EACCES;
    // NaClSemPost(&nap->request_sem);
    goto cleanup;
  }
  // NaClSemPost(&nap->request_sem);

  retval = CopyPathFromUser(nap, path, sizeof path, pathname);
  if (0 != retval)
    goto cleanup;

  retval = NaClHostDescChdir(path);
cleanup:
  return retval;
}

int32_t NaClSysGetcwd(struct NaClAppThread *natp,
                      uint32_t             buffer,
                      int                  len) {
  struct NaClApp *nap = natp->nap;
  int32_t        retval = -NACL_ABI_EINVAL;
  char           path[NACL_CONFIG_PATH_MAX];

  // NaClSemWait(&nap->request_sem);
  if (!NaClAclBypassChecks || NaClAppIsConfined()) {
    retval = -NACL_ABI_EACCES;
    // NaClSemPost(&nap->request_sem);
    goto cleanup;
  }
  // NaClSemPost(&nap->request_sem);

  if (len >= NACL_CONFIG_PATH_MAX)
    len = NACL_CONFIG_PATH_MAX - 1;

  retval = NaClHostDescGetcwd(path, len);
  if (retval != 0)
    goto cleanup;

  if (!NaClCopyOutToUser(nap, buffer, &path, strlen(path) + 1))
    retval = -NACL_ABI_EFAULT;

cleanup:
  return retval;
}

int32_t NaClSysUnlinkUntainted(char *path) {
  int32_t        retval = -NACL_ABI_EINVAL;

  if (!NaClAclBypassChecks) {
    retval = -NACL_ABI_EACCES;
    goto cleanup;
  }

  retval = NaClHostDescUnlink(path);
  NaClLog(3, "NaClHostDescUnlink '%s' -> %d\n", path, retval);
cleanup:
  return retval;
}

int32_t NaClSysUnlink(struct NaClAppThread *natp,
                      uint32_t             pathname) {
  struct NaClApp *nap = natp->nap;
  char           path[NACL_CONFIG_PATH_MAX];
  int32_t        retval = -NACL_ABI_EINVAL;
  struct NaClFileBuffer *fbp = NULL;
  struct NaClDesc       *desc;

  retval = CopyPathFromUser(nap, path, sizeof path, pathname);
  if (0 != retval)
    goto cleanup;
  desc = cfuhash_get(nap->file_name_tbl, path);

  // NaClSemWait(&nap->request_sem);
  if (!NaClAppIsConfined() && !desc) {
    // NaClSemPost(&nap->request_sem);
    return NaClSysUnlinkUntainted(path);
  }
  // NaClSemPost(&nap->request_sem);

  if (NULL != desc) {
    fbp = NaClAppGetFileBufferSem(nap, desc);

    if (NULL != fbp) {
      int d = ((struct NaClDescIoDesc *) desc)->hd->d;
      cfuhash_delete(nap->file_name_tbl, path);
      NaClAppSetFileBuffer(nap, d, NULL);
      NaClFileBufferUnref(fbp);
      NaClDescUnref(desc);
    }
  }
  NaClLog(3, "NaClHostDescUnlink '%s' -> %d\n", path, retval);
cleanup:
  return retval;
}

int32_t NaClSysTruncate(struct NaClAppThread *natp,
                        uint32_t             pathname,
                        uint32_t             length_addr) {
  struct NaClApp *nap = natp->nap;
  char           path[NACL_CONFIG_PATH_MAX];
  int32_t        retval = -NACL_ABI_EINVAL;
  nacl_abi_off_t length;

  // NaClSemWait(&nap->request_sem);
  if (!NaClAclBypassChecks || NaClAppIsConfined()) {
    // NaClSemPost(&nap->request_sem);
    return -NACL_ABI_EACCES;
  }
  // NaClSemPost(&nap->request_sem);

  retval = CopyPathFromUser(nap, path, sizeof path, pathname);
  if (0 != retval)
    return retval;

  if (!NaClCopyInFromUser(nap, &length, length_addr, sizeof length))
    return -NACL_ABI_EFAULT;

  retval = NaClHostDescTruncate(path, length);
  NaClLog(3, "NaClHostDescTruncate '%s' %"NACL_PRId64" -> %d\n",
          path, length, retval);
  return retval;
}

int32_t NaClSysLstat(struct NaClAppThread  *natp,
                     uint32_t              pathname,
                     uint32_t              nasp) {
  struct NaClApp      *nap = natp->nap;
  int32_t             retval = -NACL_ABI_EINVAL;
  char                path[NACL_CONFIG_PATH_MAX];
  nacl_host_stat_t    stbuf;

  NaClLog(3,
          ("Entered NaClSysLstat(0x%08"NACL_PRIxPTR", 0x%08"NACL_PRIx32","
           " 0x%08"NACL_PRIx32")\n"), (uintptr_t) natp, pathname, nasp);

  // NaClSemWait(&nap->request_sem);
  if (!NaClAclBypassChecks || NaClAppIsConfined()) {
    // NaClSemPost(&nap->request_sem);
    return -NACL_ABI_EACCES;
  }
  // NaClSemPost(&nap->request_sem);

  retval = CopyPathFromUser(nap, path, sizeof path, pathname);
  if (0 != retval)
    return retval;

  /*
   * Perform a host stat.
   */
  retval = NaClHostDescLstat(path, &stbuf);
  if (0 == retval) {
    struct nacl_abi_stat abi_stbuf;

    retval = NaClAbiStatHostDescStatXlateCtor(&abi_stbuf, &stbuf);
    if (!NaClCopyOutToUser(nap, nasp, &abi_stbuf, sizeof abi_stbuf)) {
      return -NACL_ABI_EFAULT;
    }
  }
  return retval;
}

int32_t NaClSysLink(struct NaClAppThread *natp,
                    uint32_t              oldname,
                    uint32_t              newname) {
  struct NaClApp *nap = natp->nap;
  char           oldpath[NACL_CONFIG_PATH_MAX];
  char           newpath[NACL_CONFIG_PATH_MAX];
  int32_t        retval = -NACL_ABI_EINVAL;

  // NaClSemWait(&nap->request_sem);
  if (!NaClAclBypassChecks || NaClAppIsConfined()) {
    // NaClSemPost(&nap->request_sem);
    return -NACL_ABI_EACCES;
  }
  // NaClSemPost(&nap->request_sem);

  retval = CopyPathFromUser(nap, oldpath, sizeof oldpath, oldname);
  if (0 != retval)
    return retval;

  retval = CopyPathFromUser(nap, newpath, sizeof newpath, newname);
  if (0 != retval)
    return retval;

  return NaClHostDescLink(oldpath, newpath);
}

int32_t NaClSysRename(struct NaClAppThread *natp,
                      uint32_t             oldname,
                      uint32_t             newname) {
  struct NaClApp *nap = natp->nap;
  char           oldpath[NACL_CONFIG_PATH_MAX];
  char           newpath[NACL_CONFIG_PATH_MAX];
  int32_t        retval = -NACL_ABI_EINVAL;

  // NaClSemWait(&nap->request_sem);
  if (!NaClAclBypassChecks || NaClAppIsConfined()) {
    // NaClSemPost(&nap->request_sem);
    return -NACL_ABI_EACCES;
  }
  // NaClSemPost(&nap->request_sem);

  retval = CopyPathFromUser(nap, oldpath, sizeof oldpath, oldname);
  if (0 != retval)
    return retval;

  retval = CopyPathFromUser(nap, newpath, sizeof newpath, newname);
  if (0 != retval)
    return retval;

  return NaClHostDescRename(oldpath, newpath);
}

int32_t NaClSysSymlink(struct NaClAppThread *natp,
                       uint32_t             oldname,
                       uint32_t             newname) {
  struct NaClApp *nap = natp->nap;
  char           oldpath[NACL_CONFIG_PATH_MAX];
  char           newpath[NACL_CONFIG_PATH_MAX];
  int32_t        retval = -NACL_ABI_EINVAL;

  // NaClSemWait(&nap->request_sem);
  if (!NaClAclBypassChecks || NaClAppIsConfined()) {
    // NaClSemPost(&nap->request_sem);
    return -NACL_ABI_EACCES;
  }
  // NaClSemPost(&nap->request_sem);

  retval = CopyPathFromUser(nap, oldpath, sizeof oldpath, oldname);
  if (0 != retval)
    return retval;

  retval = CopyPathFromUser(nap, newpath, sizeof newpath, newname);
  if (0 != retval)
    return retval;

  return NaClHostDescSymlink(oldpath, newpath);
}

int32_t NaClSysChmod(struct NaClAppThread *natp,
                     uint32_t             path,
                     nacl_abi_mode_t      mode) {
  struct NaClApp *nap = natp->nap;
  char           pathname[NACL_CONFIG_PATH_MAX];
  int32_t        retval = -NACL_ABI_EINVAL;

  // NaClSemWait(&nap->request_sem);
  if (!NaClAclBypassChecks || NaClAppIsConfined()) {
    // NaClSemPost(&nap->request_sem);
    return -NACL_ABI_EACCES;
  }
  // NaClSemPost(&nap->request_sem);

  retval = CopyPathFromUser(nap, pathname, sizeof pathname, path);
  if (0 != retval)
    return retval;

  return NaClHostDescChmod(pathname, mode);
}

int32_t NaClSysAccess(struct NaClAppThread *natp,
                      uint32_t             path,
                      int                  amode) {
  struct NaClApp *nap = natp->nap;
  char           pathname[NACL_CONFIG_PATH_MAX];
  int32_t        retval = -NACL_ABI_EINVAL;

  // NaClSemWait(&nap->request_sem);
  if (!NaClAclBypassChecks || NaClAppIsConfined()) {
    // NaClSemPost(&nap->request_sem);
    return -NACL_ABI_EACCES;
  }
  // NaClSemPost(&nap->request_sem);

  /*
   * amode must either be F_OK or some combination of the three permission bits.
   */
  if (amode != NACL_ABI_F_OK
      && (amode & ~(NACL_ABI_R_OK | NACL_ABI_W_OK | NACL_ABI_X_OK)) != 0)
    return -NACL_ABI_EINVAL;

  retval = CopyPathFromUser(nap, pathname, sizeof pathname, path);
  if (0 != retval)
    return retval;

  retval = NaClHostDescAccess(pathname, amode);
  NaClLog(3, "NaClHostDescAccess '%s' %d -> %d\n", pathname, amode, retval);
  return retval;
}

int32_t NaClSysReadlink(struct NaClAppThread *natp,
                        uint32_t             path,
                        uint32_t             buffer,
                        uint32_t             buffer_size) {
  struct NaClApp *nap = natp->nap;
  char           pathname[NACL_CONFIG_PATH_MAX];
  char           realpath[NACL_CONFIG_PATH_MAX];
  int32_t        retval = -NACL_ABI_EINVAL;
  uint32_t       result_size;

  // NaClSemWait(&nap->request_sem);
  if (!NaClAclBypassChecks || NaClAppIsConfined()) {
    // NaClSemPost(&nap->request_sem);
    return -NACL_ABI_EACCES;
  }
  // NaClSemPost(&nap->request_sem);

  retval = CopyPathFromUser(nap, pathname, sizeof pathname, path);
  if (0 != retval)
    return retval;

  retval = NaClHostDescReadlink(pathname, realpath, sizeof(realpath));
  if (retval < 0)
    return retval;
  result_size = retval;
  CHECK(result_size <= sizeof(realpath));  /* Sanity check */

  if (result_size == sizeof(realpath)) {
    /*
     * The result either got truncated or it fit exactly.  Treat it as
     * truncation.
     *
     * We can't distinguish an exact fit from truncation without doing
     * another readlink() call.  If result_size == buffer_size, we could
     * return success here, but there's little point, because untrusted
     * code can't distinguish the two either and we don't currently allow
     * using a larger buffer.
     */
    return -NACL_ABI_ENAMETOOLONG;
  }

  if (result_size > buffer_size)
    result_size = buffer_size;
  if (!NaClCopyOutToUser(nap, buffer, realpath, result_size))
    return -NACL_ABI_EFAULT;

  return result_size;
}

int32_t NaClSysUtimes(struct NaClAppThread *natp,
                      uint32_t             path,
                      uint32_t             times) {
  struct NaClApp          *nap = natp->nap;
  char                    kern_path[NACL_CONFIG_PATH_MAX];
  struct nacl_abi_timeval kern_times[2];
  int32_t                 retval = -NACL_ABI_EINVAL;

  NaClLog(3,
          ("Entered NaClSysUtimes(0x%08"NACL_PRIxPTR", 0x%08"NACL_PRIxPTR","
           " 0x%08"NACL_PRIxPTR")\n"),
          (uintptr_t) natp, (uintptr_t) path, (uintptr_t) times);

  // NaClSemWait(&nap->request_sem);
  if (!NaClAclBypassChecks || NaClAppIsConfined()) {
    retval = -NACL_ABI_EACCES;
    // NaClSemPost(&nap->request_sem);
    goto cleanup;
  }
  // NaClSemPost(&nap->request_sem);

  retval = CopyPathFromUser(nap, kern_path, sizeof kern_path, path);
  if (0 != retval) {
    goto cleanup;
  }

  if (times != 0 &&
      !NaClCopyInFromUser(nap, &kern_times, (uintptr_t) times,
                          sizeof kern_times)) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  retval = NaClHostDescUtimes(kern_path, (times != 0) ? kern_times : NULL);
cleanup:
  return retval;
}

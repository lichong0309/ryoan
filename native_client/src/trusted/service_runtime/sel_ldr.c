/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>

/*
 * NaCl Simple/secure ELF loader (NaCl SEL).
 */
#include "native_client/src/include/build_config.h"
#include "native_client/src/include/portability.h"
#include "native_client/src/include/portability_io.h"
#include "native_client/src/include/portability_string.h"
#include "native_client/src/include/nacl_macros.h"

#include "native_client/src/public/nacl_app.h"
#include "native_client/src/public/secure_service.h"

#include "native_client/src/shared/gio/gio.h"
#include "native_client/src/shared/platform/nacl_check.h"
#include "native_client/src/shared/platform/nacl_exit.h"
#include "native_client/src/shared/platform/nacl_log.h"
#include "native_client/src/shared/platform/nacl_sync.h"
#include "native_client/src/shared/platform/nacl_sync_checked.h"
#include "native_client/src/shared/platform/nacl_time.h"
#include "native_client/src/shared/srpc/nacl_srpc.h"

#include "native_client/src/trusted/desc/nacl_desc_base.h"
#include "native_client/src/trusted/desc/nacl_desc_conn_cap.h"
#include "native_client/src/trusted/desc/nacl_desc_imc.h"
#include "native_client/src/trusted/desc/nacl_desc_io.h"
#include "native_client/src/trusted/desc/nrd_xfer.h"
#include "native_client/src/trusted/fault_injection/fault_injection.h"
#include "native_client/src/trusted/fault_injection/test_injection.h"
#include "native_client/src/trusted/interval_multiset/nacl_interval_range_tree_intern.h"
#include "native_client/src/trusted/service_runtime/arch/sel_ldr_arch.h"
#include "native_client/src/trusted/service_runtime/hashtable.h"
#include "native_client/src/trusted/service_runtime/include/bits/nacl_syscalls.h"
#include "native_client/src/trusted/service_runtime/include/sys/fcntl.h"
#include "native_client/src/trusted/service_runtime/include/sys/stat.h"
#include "native_client/src/trusted/service_runtime/include/sys/time.h"
#include "native_client/src/trusted/service_runtime/nacl_app.h"
#include "native_client/src/trusted/service_runtime/nacl_app_thread.h"
#include "native_client/src/trusted/service_runtime/nacl_buffer.h"
#include "native_client/src/trusted/service_runtime/nacl_desc_effector_ldr.h"
#include "native_client/src/trusted/service_runtime/nacl_file_buffer.h"
#include "native_client/src/trusted/service_runtime/nacl_globals.h"
#include "native_client/src/trusted/service_runtime/nacl_resource.h"
#include "native_client/src/trusted/service_runtime/nacl_syscall_common.h"
#include "native_client/src/trusted/service_runtime/nacl_syscall_list.h"
#include "native_client/src/trusted/service_runtime/nacl_valgrind_hooks.h"
#include "native_client/src/trusted/service_runtime/sel_addrspace.h"
#include "native_client/src/trusted/service_runtime/sel_ldr.h"
#include "native_client/src/trusted/service_runtime/sel_memory.h"
#include "native_client/src/trusted/service_runtime/sel_ldr_thread_interface.h"
#include "native_client/src/trusted/simple_service/nacl_simple_rservice.h"
#include "native_client/src/trusted/simple_service/nacl_simple_service.h"
#include "native_client/src/trusted/threading/nacl_thread_interface.h"
#include "native_client/src/trusted/validator/rich_file_info.h"

static int IsEnvironmentVariableSet(char const *env_name) {
  return NULL != getenv(env_name);
}

static int ShouldEnableDyncodeSyscalls(void) {
  return !IsEnvironmentVariableSet("NACL_DISABLE_DYNCODE_SYSCALLS");
}

static int ShouldEnableDynamicLoading(void) {
  return !IsEnvironmentVariableSet("NACL_DISABLE_DYNAMIC_LOADING");
}

int NaClAppWithEmptySyscallTableCtor(struct NaClApp *nap) {
  struct NaClDescEffectorLdr  *effp;
  int i;

  /* Zero-initialize in case we miss any fields below. */
  memset(nap, 0, sizeof(*nap));

  /* The validation cache will be injected later, if it exists. */
  nap->validation_cache = NULL;

  nap->validator = NaClCreateValidator();

  /* Get the set of features that the CPU we're running on supports. */
  /* These may be adjusted later in sel_main.c for fixed-feature CPU mode. */
  nap->cpu_features = (NaClCPUFeatures *) malloc(
      nap->validator->CPUFeatureSize);
  if (NULL == nap->cpu_features) {
    goto cleanup_none;
  }
  nap->validator->GetCurrentCPUFeatures(nap->cpu_features);
  nap->fixed_feature_cpu_mode = 0;

  nap->addr_bits = NACL_MAX_ADDR_BITS;

  nap->stack_size = NACL_DEFAULT_STACK_MAX;
  nap->initial_nexe_max_code_bytes = 0;

  nap->mem_start = 0;

#if (NACL_ARCH(NACL_BUILD_ARCH) == NACL_x86 \
     && NACL_BUILD_SUBARCH == 32)
  nap->pcrel_thunk = 0;
  nap->pcrel_thunk_end = 0;
#endif
#if (NACL_ARCH(NACL_BUILD_ARCH) == NACL_x86 \
     && NACL_BUILD_SUBARCH == 64)
  nap->nacl_syscall_addr = 0;
  nap->get_tls_fast_path1_addr = 0;
  nap->get_tls_fast_path2_addr = 0;
#endif

  nap->static_text_end = 0;
  nap->dynamic_text_start = 0;
  nap->dynamic_text_end = 0;
  nap->rodata_start = 0;
  nap->data_start = 0;
  nap->data_end = 0;

  nap->initial_entry_pt = 0;
  nap->user_entry_pt = 0;
  nap->defer_count = 0;
  nap->max_host_fd = 0;
  nap->num_wait_for = 0;
  nap->num_finished = 0;

  if (!DynArrayCtor(&nap->read_buffer_tbl, 2)) {
    goto cleanup_read_buffer;
  }
  if (!DynArrayCtor(&nap->write_buffer_tbl, 2)) {
    goto cleanup_write_buffer;
  }
  if (!DynArrayCtor(&nap->file_buffer_tbl, 2)) {
    goto cleanup_file_buffer;
  }
  if ((nap->file_name_tbl = cfuhash_new_with_initial_size(16)) == NULL) {
    goto cleanup_file_name;
  }
  if (!DynArrayCtor(&nap->defer_tbl, 2)) {
    goto cleanup_defer;
  }
  if (!DynArrayCtor(&nap->threads, 2)) {
    goto cleanup_cpu_features;
  }
  if (!DynArrayCtor(&nap->desc_tbl, 2)) {
    goto cleanup_threads;
  }
  if (!NaClVmmapCtor(&nap->mem_map)) {
    goto cleanup_desc_tbl;
  }

  nap->mem_io_regions = (struct NaClIntervalMultiset *) malloc(
      sizeof(struct NaClIntervalRangeTree));
  if (NULL == nap->mem_io_regions) {
    goto cleanup_mem_map;
  }

  if (!NaClIntervalRangeTreeCtor((struct NaClIntervalRangeTree *)
                                 nap->mem_io_regions)) {
    free(nap->mem_io_regions);
    nap->mem_io_regions = NULL;
    goto cleanup_mem_map;
  }

  effp = (struct NaClDescEffectorLdr *) malloc(sizeof *effp);
  if (NULL == effp) {
    goto cleanup_mem_io_regions;
  }
  if (!NaClDescEffectorLdrCtor(effp, nap)) {
    goto cleanup_effp_free;
  }
  nap->effp = (struct NaClDescEffector *) effp;

  nap->enable_dyncode_syscalls = ShouldEnableDyncodeSyscalls();
  nap->use_shm_for_dynamic_text = ShouldEnableDynamicLoading();
  nap->text_shm = NULL;
  if (!NaClMutexCtor(&nap->dynamic_load_mutex)) {
    goto cleanup_effp_free;
  }
  nap->dynamic_page_bitmap = NULL;

  nap->dynamic_regions = NULL;
  nap->num_dynamic_regions = 0;
  nap->dynamic_regions_allocated = 0;
  nap->dynamic_delete_generation = 0;

  nap->dynamic_mapcache_offset = 0;
  nap->dynamic_mapcache_size = 0;
  nap->dynamic_mapcache_ret = 0;

  nap->service_port = NULL;
  nap->service_address = NULL;
  nap->secure_service_port = NULL;
  nap->secure_service_address = NULL;
  nap->bootstrap_channel = NULL;
  nap->secure_service = NULL;
  nap->main_exe_prevalidated = 0;

  if (!NaClResourceNaClAppInit(&nap->resources, nap)) {
    goto cleanup_dynamic_load_mutex;
  }

  if (!NaClMutexCtor(&nap->mu)) {
    goto cleanup_dynamic_load_mutex;
  }
  if (!NaClCondVarCtor(&nap->cv)) {
    goto cleanup_mu;
  }

#if NACL_WINDOWS
  nap->vm_hole_may_exist = 0;
  nap->threads_launching = 0;
#endif

  for (i = 0; i < NACL_MAX_SYSCALLS; ++i) {
    nap->syscall_table[i].handler = &NaClSysNotImplementedDecoder;
  }

  nap->module_initialization_state = NACL_MODULE_UNINITIALIZED;
  nap->module_load_status = LOAD_OK;

  nap->ignore_validator_result = 0;
  nap->skip_validator = 0;
  nap->validator_stub_out_mode = 0;

  if (IsEnvironmentVariableSet("NACL_DANGEROUS_ENABLE_FILE_ACCESS")) {
    NaClInsecurelyBypassAllAclChecks();
    NaClLog(LOG_INFO, "DANGER: ENABLED FILE ACCESS\n");
  }

  nap->enable_list_mappings = 0;
  if (IsEnvironmentVariableSet("NACL_DANGEROUS_ENABLE_LIST_MAPPINGS")) {
    /*
     * This syscall is not actually know to be dangerous, but is not yet
     * exposed by our public API.
     */
    NaClLog(LOG_INFO, "DANGER: ENABLED LIST_MAPPINGS\n");
    nap->enable_list_mappings = 1;
  }
  nap->pnacl_mode = 0;

  if (!NaClMutexCtor(&nap->threads_mu)) {
    goto cleanup_cv;
  }
  nap->num_threads = 0;
  if (!NaClFastMutexCtor(&nap->desc_mu)) {
    goto cleanup_threads_mu;
  }
  if (!NaClMutexCtor(&nap->defer_desc_mu)) {
    goto cleanup_defer_desc_mu;
  }

  nap->running = 0;
  nap->exit_status = -1;
  nap->nonce = NULL;
  nap->fake_app_label = NULL;
  nap->output_to_user = 0;
  nap->get_user_input = 0;
  nap->no_enc = 0;

#if NACL_ARCH(NACL_BUILD_ARCH) == NACL_x86 && NACL_BUILD_SUBARCH == 32
  nap->code_seg_sel = 0;
  nap->data_seg_sel = 0;
#endif

  nap->debug_stub_callbacks = NULL;

  nap->main_nexe_desc = NULL;
  nap->irt_nexe_desc = NULL;

  nap->exception_handler = 0;
  if (!NaClMutexCtor(&nap->exception_mu)) {
    goto cleanup_desc_mu;
  }
  if (!NaClSemCtor(&nap->file_buffer_sem, 1)) {
    goto cleanup_file_buffer_sem;
  }
  if (!NaClSemCtor(&nap->request_sem, 1)) {
    goto cleanup_request_sem;
  }
  nap->enable_exception_handling = 0;
#if NACL_WINDOWS
  nap->debug_exception_handler_state = NACL_DEBUG_EXCEPTION_HANDLER_NOT_STARTED;
  nap->attach_debug_exception_handler_func = NULL;
#endif
  nap->enable_faulted_thread_queue = 0;
  nap->faulted_thread_count = 0;
#if NACL_WINDOWS
  nap->faulted_thread_event = INVALID_HANDLE_VALUE;
#else
  nap->faulted_thread_fd_read = -1;
  nap->faulted_thread_fd_write = -1;
#endif


#if NACL_LINUX || NACL_OSX
  /*
   * Try to pre-cache information that we can't obtain with the outer
   * sandbox on.  If the outer sandbox has already been enabled, this
   * will just set sc_nprocessors_onln to -1, and it is the
   * responsibility of the caller to replace this with a sane value
   * after the Ctor returns.
   */
  nap->sc_nprocessors_onln = sysconf(_SC_NPROCESSORS_ONLN);
#endif

#if !NACL_LINUX
  if (!NaClMutexCtor(&nap->futex_wait_list_mu)) {
    goto cleanup_exception_mu;
  }
  nap->futex_wait_list_head.next = &nap->futex_wait_list_head;
  nap->futex_wait_list_head.prev = &nap->futex_wait_list_head;
#endif

  return 1;

#if !NACL_LINUX
 cleanup_exception_mu:
  NaClMutexDtor(&nap->exception_mu);
#endif
 cleanup_request_sem:
  NaClSemDtor(&nap->request_sem);
 cleanup_file_buffer_sem:
  NaClSemDtor(&nap->file_buffer_sem);
 cleanup_desc_mu:
  NaClFastMutexDtor(&nap->desc_mu);
 cleanup_defer_desc_mu:
  NaClMutexDtor(&nap->defer_desc_mu);
 cleanup_threads_mu:
  NaClMutexDtor(&nap->threads_mu);
 cleanup_cv:
  NaClCondVarDtor(&nap->cv);
 cleanup_mu:
  NaClMutexDtor(&nap->mu);
 cleanup_dynamic_load_mutex:
  NaClMutexDtor(&nap->dynamic_load_mutex);
 cleanup_effp_free:
  free(nap->effp);
 cleanup_mem_io_regions:
  NaClIntervalMultisetDelete(nap->mem_io_regions);
  nap->mem_io_regions = NULL;
 cleanup_mem_map:
  NaClVmmapDtor(&nap->mem_map);
 cleanup_desc_tbl:
  DynArrayDtor(&nap->desc_tbl);
 cleanup_threads:
  DynArrayDtor(&nap->threads);
 cleanup_cpu_features:
  free(nap->cpu_features);
 cleanup_defer:
  DynArrayDtor(&nap->defer_tbl);
 cleanup_file_name:
  cfuhash_destroy(nap->file_name_tbl);
 cleanup_file_buffer:
  DynArrayDtor(&nap->file_buffer_tbl);
 cleanup_write_buffer:
  DynArrayDtor(&nap->write_buffer_tbl);
 cleanup_read_buffer:
  DynArrayDtor(&nap->read_buffer_tbl);
 cleanup_none:
  return 0;
}

int NaClAppCtor(struct NaClApp *nap) {
  if (!NaClAppWithEmptySyscallTableCtor(nap))
    return 0;
  NaClAppRegisterDefaultSyscalls(nap);
  return 1;
}

struct NaClApp *NaClAppCreate(void) {
  struct NaClApp *nap = malloc(sizeof(struct NaClApp));
  if (nap == NULL)
    NaClLog(LOG_FATAL, "Failed to allocate NaClApp\n");
  if (!NaClAppCtor(nap))
    NaClLog(LOG_FATAL, "NaClAppCtor() failed\n");
  return nap;
}

/*
 * unaligned little-endian load.  precondition: nbytes should never be
 * more than 8.
 */
static uint64_t NaClLoadMem(uintptr_t addr,
                            size_t    user_nbytes) {
  uint64_t      value = 0;

  CHECK(0 != user_nbytes && user_nbytes <= 8);

  do {
    value = value << 8;
    value |= ((uint8_t *) addr)[--user_nbytes];
  } while (user_nbytes > 0);
  return value;
}

#define GENERIC_LOAD(bits) \
  static uint ## bits ## _t NaClLoad ## bits(uintptr_t addr) { \
    return (uint ## bits ## _t) NaClLoadMem(addr, sizeof(uint ## bits ## _t)); \
  }

#if NACL_BUILD_SUBARCH == 32
GENERIC_LOAD(32)
#endif
GENERIC_LOAD(64)

#undef GENERIC_LOAD

/*
 * unaligned little-endian store
 */
static void NaClStoreMem(uintptr_t  addr,
                         size_t     nbytes,
                         uint64_t   value) {
  size_t i;

  CHECK(nbytes <= 8);

  for (i = 0; i < nbytes; ++i) {
    ((uint8_t *) addr)[i] = (uint8_t) value;
    value = value >> 8;
  }
}

#define GENERIC_STORE(bits) \
  static void NaClStore ## bits(uintptr_t addr, \
                                uint ## bits ## _t v) { \
    NaClStoreMem(addr, sizeof(uint ## bits ## _t), v); \
  }

GENERIC_STORE(16)
GENERIC_STORE(32)
GENERIC_STORE(64)

#undef GENERIC_STORE

struct NaClPatchInfo *NaClPatchInfoCtor(struct NaClPatchInfo *self) {
  if (NULL != self) {
    memset(self, 0, sizeof *self);
  }
  return self;
}

/*
 * This function is called by NaClLoadTrampoline and NaClLoadSpringboard to
 * patch a single memory location specified in NaClPatchInfo structure.
 */
void  NaClApplyPatchToMemory(struct NaClPatchInfo  *patch) {
  size_t    i;
  size_t    offset;
  int64_t   reloc;
  uintptr_t target_addr;

  memcpy((void *) patch->dst, (void *) patch->src, patch->nbytes);

  reloc = patch->dst - patch->src;


  for (i = 0; i < patch->num_abs64; ++i) {
    offset = patch->abs64[i].target - patch->src;
    target_addr = patch->dst + offset;
    NaClStore64(target_addr, patch->abs64[i].value);
  }

  for (i = 0; i < patch->num_abs32; ++i) {
    offset = patch->abs32[i].target - patch->src;
    target_addr = patch->dst + offset;
    NaClStore32(target_addr, (uint32_t) patch->abs32[i].value);
  }

  for (i = 0; i < patch->num_abs16; ++i) {
    offset = patch->abs16[i].target - patch->src;
    target_addr = patch->dst + offset;
    NaClStore16(target_addr, (uint16_t) patch->abs16[i].value);
  }

  for (i = 0; i < patch->num_rel64; ++i) {
    offset = patch->rel64[i] - patch->src;
    target_addr = patch->dst + offset;
    NaClStore64(target_addr, NaClLoad64(target_addr) - reloc);
  }

  /*
   * rel32 is only supported on 32-bit architectures. The range of a relative
   * relocation in untrusted space is +/- 4GB. This can be represented as
   * an unsigned 32-bit value mod 2^32, which is handy on a 32 bit system since
   * all 32-bit pointer arithmetic is implicitly mod 2^32. On a 64 bit system,
   * however, pointer arithmetic is implicitly modulo 2^64, which isn't as
   * helpful for our purposes. We could simulate the 32-bit behavior by
   * explicitly modding all relative addresses by 2^32, but that seems like an
   * expensive way to save a few bytes per reloc.
   */
#if NACL_BUILD_SUBARCH == 32
  for (i = 0; i < patch->num_rel32; ++i) {
    offset = patch->rel32[i] - patch->src;
    target_addr = patch->dst + offset;
    NaClStore32(target_addr,
      (uint32_t) NaClLoad32(target_addr) - (int32_t) reloc);
  }
#endif
}


/*
 * Install syscall trampolines at all possible well-formed entry
 * points within the trampoline pages.  Many of these syscalls will
 * correspond to unimplemented system calls and will just abort the
 * program.
 */
void  NaClLoadTrampoline(struct NaClApp *nap, enum NaClAslrMode aslr_mode) {
  int         num_syscalls;
  int         i;
  uintptr_t   addr;

#if NACL_ARCH(NACL_BUILD_ARCH) == NACL_x86 && NACL_BUILD_SUBARCH == 32
  if (!NaClMakePcrelThunk(nap, aslr_mode)) {
    NaClLog(LOG_FATAL, "NaClMakePcrelThunk failed!\n");
  }
#else
  UNREFERENCED_PARAMETER(aslr_mode);
#endif
#if NACL_ARCH(NACL_BUILD_ARCH) == NACL_x86 && NACL_BUILD_SUBARCH == 64
  if (!NaClMakeDispatchAddrs(nap)) {
    NaClLog(LOG_FATAL, "NaClMakeDispatchAddrs failed!\n");
  }
#endif
  NaClFillTrampolineRegion(nap);

  /*
   * Do not bother to fill in the contents of page 0, since we make it
   * inaccessible later (see sel_addrspace.c, NaClMemoryProtection)
   * anyway to help detect NULL pointer errors, and we might as well
   * not dirty the page.
   *
   * The last syscall entry point is used for springboard code.
   */
  num_syscalls = ((NACL_TRAMPOLINE_END - NACL_SYSCALL_START_ADDR)
                  / NACL_SYSCALL_BLOCK_SIZE) - 1;

  NaClLog(2, "num_syscalls = %d (0x%x)\n", num_syscalls, num_syscalls);

  for (i = 0, addr = nap->mem_start + NACL_SYSCALL_START_ADDR;
       i < num_syscalls;
       ++i, addr += NACL_SYSCALL_BLOCK_SIZE) {
    NaClPatchOneTrampoline(nap, addr);
  }
#if NACL_ARCH(NACL_BUILD_ARCH) == NACL_x86 && NACL_BUILD_SUBARCH == 64
  NaClPatchOneTrampolineCall(nap->get_tls_fast_path1_addr,
                             nap->mem_start + NACL_SYSCALL_START_ADDR
                             + NACL_SYSCALL_BLOCK_SIZE * NACL_sys_tls_get);
  NaClPatchOneTrampolineCall(nap->get_tls_fast_path2_addr,
                             nap->mem_start + NACL_SYSCALL_START_ADDR
                             + (NACL_SYSCALL_BLOCK_SIZE *
                                NACL_sys_second_tls_get));
#endif

  NACL_TEST_INJECTION(ChangeTrampolines, (nap));
}

struct NaClDesc *NaClAppGetDescMu(struct NaClApp *nap,
                                  int            d) {
  struct NaClDesc *result;

  result = (struct NaClDesc *) DynArrayGet(&nap->desc_tbl, d);
  if (NULL != result) {
    NaClDescRef(result);
  }

  return result;
}

void NaClAppSetDescMu(struct NaClApp   *nap,
                      int              d,
                      struct NaClDesc  *ndp) {
  struct NaClDesc *result;

  result = (struct NaClDesc *) DynArrayGet(&nap->desc_tbl, d);
  NaClDescSafeUnref(result);

  if (!DynArraySet(&nap->desc_tbl, d, ndp)) {
    NaClLog(LOG_FATAL,
            "NaClAppSetDesc: could not set descriptor %d to 0x%08"
            NACL_PRIxPTR"\n",
            d,
            (uintptr_t) ndp);
  }
}

void NaClAppSetDescDeferMu(struct NaClApp  *nap,
                           int             d,
                           struct NaClDesc *ndp) {
  struct NaClDesc *result;

  result = (struct NaClDesc *) DynArrayGet(&nap->defer_tbl, d);
  NaClDescSafeUnref(result);

  if (!DynArraySet(&nap->defer_tbl, d, ndp)) {
    NaClLog(LOG_FATAL,
            "NaClAppSetDescDefer: could not add 0x%08"
            NACL_PRIxPTR"\n",
            (uintptr_t) ndp);
  }
}

struct NaClDesc *NaClAppGetDescDeferMu(struct NaClApp *nap,
                                       int            d) {
  struct NaClDesc *result;
  result = (struct NaClDesc *) DynArrayGet(&nap->defer_tbl, d);
  if (NULL != result) {
    NaClDescRef(result);
  }

  return result;
}

void NaClAppSetReadBufferSem(struct NaClApp        *nap,
                             int                   d,
                             struct NaClBuffer     *buff) {
  struct NaClBuffer* result;
  result = (struct NaClBuffer *) DynArrayGet(&nap->read_buffer_tbl, d);
  NaClBufferSafeUnref(result);

  if (!DynArraySet(&nap->read_buffer_tbl, d, buff)) {
    NaClLog(LOG_FATAL,
            "NaClAppSetReadBuffer: could not set read buffer of descriptor %d\n",
            d);
  }
}

void NaClAppSetWriteBufferSem(struct NaClApp         *nap,
                              int                    d,
                              struct NaClBuffer      *buff) {
  struct NaClBuffer* result;
  result = (struct NaClBuffer *) DynArrayGet(&nap->write_buffer_tbl, d);
  NaClBufferSafeUnref(result);

  if (!DynArraySet(&nap->write_buffer_tbl, d, buff)) {
    NaClLog(LOG_FATAL,
            "NaClAppSetWriteBuffer: could not set write buffer of descriptor %d\n",
            d);
  }
}

void NaClAppSetFileBufferSem(struct NaClApp        *nap,
                             int                   d,
                             struct NaClFileBuffer *buff) {
  struct NaClFileBuffer* result;
  result = (struct NaClFileBuffer *) DynArrayGet(&nap->file_buffer_tbl, d);
  NaClFileBufferSafeUnref(result);

  if (!DynArraySet(&nap->file_buffer_tbl, d, buff)) {
    NaClLog(LOG_FATAL,
            "NaClAppSetFileBuffer: could not set file buffer of descriptor %d\n",
            d);
  }
}

struct NaClBuffer* NaClAppGetReadBufferByFdSem(struct NaClApp *nap,
                                                   int            fd) {
  struct NaClBuffer* result;
  NaClLog(4,
          "NaClAppGetReadBufferByFdSem(0x%08"NACL_PRIxPTR", %d)\n",
          (uintptr_t)nap, fd);
  result =
    (struct NaClBuffer *)DynArrayGet(&nap->read_buffer_tbl, fd);
  if (NULL != result) {
    NaClBufferRef(result);
  }
  return result;
}

struct NaClBuffer* NaClAppGetReadBufferSem(struct NaClApp  *nap,
                                           struct NaClDesc *ndp) {
  int d;
  NaClLog(4, 
          "NaClAppGetReadBufferSem(0x%08"NACL_PRIxPTR", 0x%08"NACL_PRIxPTR")\n)",
          (uintptr_t) nap, (uintptr_t) ndp);
  d = ((struct NaClDescIoDesc *) ndp)->hd->d;
  /* NaClLog(LOG_INFO, "Getting read buffer of %d\n", d); */
  return NaClAppGetReadBufferByFdSem(nap, d);
}

struct NaClBuffer* NaClAppGetWriteBufferByFdSem(struct NaClApp *nap,
                                                     int            fd) {
  struct NaClBuffer* result;
  NaClLog(4,
          "NaClAppGetWriteBufferByFdSem(0x%08"NACL_PRIxPTR", %d)\n",
          (uintptr_t)nap, fd);
  result =
    (struct NaClBuffer*)DynArrayGet(&nap->write_buffer_tbl, fd);
  if (NULL != result) {
    NaClBufferRef(result);
  }
  return result;
}

struct NaClBuffer* NaClAppGetWriteBufferSem(struct NaClApp  *nap,
                                                 struct NaClDesc *ndp) {
  int d;
  NaClLog(4, 
          "NaClAppGetWriteBufferSem(0x%08"NACL_PRIxPTR", 0x%08"NACL_PRIxPTR")\n",
          (uintptr_t) nap, (uintptr_t) ndp);
  d = ((struct NaClDescIoDesc *) ndp)->hd->d;
  return NaClAppGetWriteBufferByFdSem(nap, d);
}

struct NaClFileBuffer* NaClAppGetFileBufferByFdSem(struct NaClApp *nap,
                                                   int            fd) {
  struct NaClFileBuffer* result;
  NaClLog(4,
          "NaClAppGetFileBufferByFdSem(0x%08"NACL_PRIxPTR", %d)\n",
          (uintptr_t)nap, fd);
  result =
    (struct NaClFileBuffer*) DynArrayGet(&nap->file_buffer_tbl, fd);
  if (NULL != result) {
    NaClFileBufferRef(result);
  }
  return result;
}

struct NaClFileBuffer* NaClAppGetFileBufferSem(struct NaClApp  *nap,
                                               struct NaClDesc *ndp) {
  int d;
  NaClLog(4,
          "NaClAppGetFileBufferSem(0x%08"NACL_PRIxPTR", 0x%08"NACL_PRIxPTR")\n",
          (uintptr_t) nap, (uintptr_t) ndp);
  d = ((struct NaClDescIoDesc *) ndp)->hd->d;
  return NaClAppGetFileBufferByFdSem(nap, d);
}

int32_t NaClAppSetDescAvailMu(struct NaClApp  *nap,
                              struct NaClDesc *ndp) {
  size_t pos;

  pos = DynArrayFirstAvail(&nap->desc_tbl);

  if (pos > INT32_MAX) {
    NaClLog(LOG_FATAL,
            ("NaClAppSetDescAvailMu: DynArrayFirstAvail returned a value"
             " that is greather than 2**31-1.\n"));
  }

  NaClAppSetDescMu(nap, (int) pos, ndp);

  return (int32_t) pos;
}

struct NaClDesc *NaClAppGetDesc(struct NaClApp *nap,
                                int            d) {
  struct NaClDesc *res;

  NaClFastMutexLock(&nap->desc_mu);
  res = NaClAppGetDescMu(nap, d);
  NaClFastMutexUnlock(&nap->desc_mu);
  return res;
}

void NaClAppSetDesc(struct NaClApp   *nap,
                    int              d,
                    struct NaClDesc  *ndp) {
  NaClFastMutexLock(&nap->desc_mu);
  NaClAppSetDescMu(nap, d, ndp);
  NaClFastMutexUnlock(&nap->desc_mu);
}

void NaClAppSetReadBuffer(struct NaClApp    *nap,
                          int               d,
                          struct NaClBuffer *buff) {
  NaClAppSetReadBufferSem(nap, d, buff);
}

void NaClAppSetWriteBuffer(struct NaClApp    *nap,
                           int               d,
                           struct NaClBuffer *buff) {
  NaClAppSetWriteBufferSem(nap, d, buff);
}

void NaClAppSetFileBuffer(struct NaClApp        *nap,
                          int                   d,
                          struct NaClFileBuffer *buff) {
  NaClSemWait(&nap->file_buffer_sem);
  NaClAppSetFileBufferSem(nap, d, buff);
  NaClSemPost(&nap->file_buffer_sem);
}

int32_t NaClAppSetDescAvail(struct NaClApp  *nap,
                            struct NaClDesc *ndp) {
  int32_t pos;

  NaClFastMutexLock(&nap->desc_mu);
  pos = NaClAppSetDescAvailMu(nap, ndp);
  NaClFastMutexUnlock(&nap->desc_mu);

  return pos;
}

int NaClAddThreadMu(struct NaClApp        *nap,
                    struct NaClAppThread  *natp) {
  size_t pos;

  pos = DynArrayFirstAvail(&nap->threads);

  if (!DynArraySet(&nap->threads, pos, natp)) {
    NaClLog(LOG_FATAL,
            "NaClAddThreadMu: DynArraySet at position %"NACL_PRIuS" failed\n",
            pos);
  }
  ++nap->num_threads;
  return (int) pos;
}

int NaClAddThread(struct NaClApp        *nap,
                  struct NaClAppThread  *natp) {
  int pos;

  NaClXMutexLock(&nap->threads_mu);
  pos = NaClAddThreadMu(nap, natp);
  NaClXMutexUnlock(&nap->threads_mu);

  return pos;
}

void NaClRemoveThreadMu(struct NaClApp  *nap,
                        int             thread_num) {
  if (NULL == DynArrayGet(&nap->threads, thread_num)) {
    NaClLog(LOG_FATAL,
            "NaClRemoveThreadMu:: thread to be removed is not in the table\n");
  }
  if (nap->num_threads == 0) {
    NaClLog(LOG_FATAL,
            "NaClRemoveThreadMu:: num_threads cannot be 0!!!\n");
  }
  --nap->num_threads;
  if (!DynArraySet(&nap->threads, thread_num, (struct NaClAppThread *) NULL)) {
    NaClLog(LOG_FATAL,
            "NaClRemoveThreadMu:: DynArraySet at position %d failed\n",
            thread_num);
  }
}

void NaClRemoveThread(struct NaClApp  *nap,
                      int             thread_num) {
  NaClXMutexLock(&nap->threads_mu);
  NaClRemoveThreadMu(nap, thread_num);
  NaClXMutexUnlock(&nap->threads_mu);
}

struct NaClAppThread *NaClGetThreadMu(struct NaClApp  *nap,
                                      int             thread_num) {
  return (struct NaClAppThread *) DynArrayGet(&nap->threads, thread_num);
}

void NaClAddHostDescriptor(struct NaClApp *nap,
                           int            host_os_desc,
                           int            flag,
                           int            nacl_desc) {
  struct NaClDescIoDesc *dp;

  NaClLog(4,
          "NaClAddHostDescriptor: host %d as nacl desc %d, flag 0x%x\n",
          host_os_desc,
          nacl_desc,
          flag);
  dp = NaClDescIoDescMake(NaClHostDescPosixMake(host_os_desc, flag));
  if (NULL == dp) {
    NaClLog(LOG_FATAL, "NaClAddHostDescriptor: NaClDescIoDescMake failed\n");
  }
  NaClAppSetDesc(nap, nacl_desc, (struct NaClDesc *) dp);
}

void NaClAddHostIOBuffer(struct NaClApp *nap,
                         int            host_os_desc,
                         int            flag) {
  struct NaClBuffer* rb;
  struct NaClBuffer* wb;
  if (flag == O_RDONLY) {
    NaClLog(4,
            "NaClAddHostIOBuffer: add read buffer for host %d, flag 0x%x\n",
            host_os_desc,
            flag);
    rb = (struct NaClBuffer *) malloc(sizeof *rb);
    if (NULL == rb) {
      NaClLog(LOG_FATAL,
              "NaClAddHostIOBuffer: no memory for read buffer\n"); 
    }
    if (!NaClBufferCtor(rb)) {
      NaClLog(LOG_FATAL,
              ("NaClAddHostIOBuffer:"
               " NaClReadBufferCtor(0x%08"NACL_PRIxPTR") failed\n"),
              (uintptr_t) rb);
    }
    NaClAppSetReadBuffer(nap, host_os_desc, rb);
  } else if (flag == O_WRONLY) {
    NaClLog(4,
            "NaClAddHostIOBuffer: add write buffer for host %d, flag 0x%x\n",
            host_os_desc,
            flag);
    wb = (struct NaClBuffer *) malloc(sizeof *wb);
    if (NULL == wb) {
      NaClLog(LOG_FATAL,
              "NaClAddHostIOBuffer: no memory for write buffer\n");
    }
    if (!NaClBufferCtor(wb)) {
      NaClLog(LOG_FATAL,
              ("NaClAddHostIOBuffer:"
               " NaClWriteBufferCtor(0x%08"NACL_PRIxPTR") failed\n"),
              (uintptr_t) wb);

    }
    NaClAppSetWriteBuffer(nap, host_os_desc, wb);
  }
}

int NaClFileBufferLoadFile(struct NaClDesc       *ndp,
                           struct NaClFileBuffer *fbp) {
  struct nacl_abi_stat stbuf;
  int32_t retval;
  uint32_t read = 0;
  retval =(*((struct NaClDescVtbl const *) ndp->base.vtbl)->
           Fstat) (ndp, &stbuf);
  if (0 == retval) {
    if (!NaClFileBufferCtor_fixed_len(fbp, stbuf.nacl_abi_st_size)) {
      NaClLog(LOG_FATAL,
          ("NaClAddHostFileBuffer:"
           " NaClFileBufferCtor_fixed_len(0x%08"NACL_PRIxPTR") failed\n"),
          (uintptr_t) fbp);
    }

    NaClLog(LOG_INFO,
            "Preloaded file length is %d\n", (int)fbp->len);

    while (read != stbuf.nacl_abi_st_size) {
      retval = (*((struct NaClDescVtbl const *) ndp->base.vtbl)->
       Read)(ndp, fbp->buffer + read, stbuf.nacl_abi_st_size - read);
      if (retval < 0) {
        perror("Read");
        NaClLog(LOG_ERROR,
                ("NaClFileBufferLoadFile:"
                 " Read(0x%08"NACL_PRIxPTR", 0x%08"NACL_PRIxPTR", %d) failed\n"),
                (uintptr_t) ndp,
                (uintptr_t)(fbp->buffer + read),
                (int)(stbuf.nacl_abi_st_size - read));
        return 0;
      }
      read += retval;
    }
    return 1;
  } else {
    perror("Fstat");
    NaClLog(LOG_ERROR,
            ("NaClFileBufferLoadFile:"
             " Fstat(0x%08"NACL_PRIxPTR", 0x%08"NACL_PRIxPTR") failed\n"),
            (uintptr_t)ndp, (uintptr_t)&stbuf);
    return 0;
  }
}

void NaClAddHostFileBufferAndLoadFile(struct NaClApp  *nap,
                                      struct NaClDesc *ndp) {
  struct NaClFileBuffer* fbp;
  int d;
  d = ((struct NaClDescIoDesc *) ndp)->hd->d;
  NaClLog(LOG_INFO,
          "NaClAddHostFileBuffer: add file buffer for host %d\n", d);
  if (d > 0 && (uint32_t) d > nap->max_host_fd) {
    nap->max_host_fd = d;
  }
  fbp = (struct NaClFileBuffer *) malloc(sizeof *fbp);
  if (NULL == fbp) {
    NaClLog(LOG_FATAL,
            "NaClAddHostFileBuffer: no memory for file buffer\n");
  }
  if (!NaClFileBufferLoadFile(ndp, fbp)) {
    NaClLog(LOG_FATAL,
            "NaClFileBufferLoadFile: fail to load file\n");
  }
  NaClAppSetFileBuffer(nap, d, fbp);
}

void NaClAddImcHandle(struct NaClApp  *nap,
                      NaClHandle      h,
                      int             nacl_desc) {
  struct NaClDescImcDesc  *dp;

  NaClLog(4,
          ("NaClAddImcHandle: importing NaClHandle %"NACL_PRIxPTR
           " as nacl desc %d\n"),
          (uintptr_t) h,
          nacl_desc);
  dp = (struct NaClDescImcDesc *) malloc(sizeof *dp);
  if (NACL_FI_ERROR_COND("NaClAddImcHandle__malloc", NULL == dp)) {
    NaClLog(LOG_FATAL, "NaClAddImcHandle: no memory\n");
  }
  if (NACL_FI_ERROR_COND("NaClAddImcHandle__ctor",
                         !NaClDescImcDescCtor(dp, h))) {
    NaClLog(LOG_FATAL, ("NaClAddImcHandle: cannot construct"
                        " IMC descriptor object\n"));
  }
  NaClAppSetDesc(nap, nacl_desc, (struct NaClDesc *) dp);
}


static struct {
  int         d;
  char const  *env_name;
  int         nacl_flags;
  int         mode;
} const g_nacl_redir_control[] = {
  { 0, "NACL_EXE_STDIN",
    NACL_ABI_O_RDONLY, 0, },
  { 1, "NACL_EXE_STDOUT",
    NACL_ABI_O_WRONLY | NACL_ABI_O_APPEND | NACL_ABI_O_CREAT, 0777, },
  { 2, "NACL_EXE_STDERR",
    NACL_ABI_O_WRONLY | NACL_ABI_O_APPEND | NACL_ABI_O_CREAT, 0777, },
};

/*
 * Process I/O redirection/inheritance from the environment.
 *
 * File redirection is impossible if an outer sandbox is in place.  For the
 * command-line embedding, we sometimes have an outer sandbox: on OSX, it is
 * enabled after loading the file is loaded. We handle this situation by
 * allowing the NaClAppInitialDescriptorHookup to fail in which case in falls
 * back to default inheritance. This means dup'ing descriptors 0-2 and making
 * them available to the NaCl App.
 *
 * When standard input is inherited, this could result in a NaCl module
 * competing for input from the terminal; for graphical / browser plugin
 * environments, this never is allowed to happen, and having this is useful for
 * debugging, and for potential standalone text-mode applications of NaCl.
 *
 * TODO(bsy): consider whether default inheritance should occur only
 * in debug mode.
 */
void NaClAppInitialDescriptorHookup(struct NaClApp *nap) {
  size_t          ix;
  char const      *env;
  struct NaClDesc *ndp;

  NaClLog(4, "Processing I/O redirection/inheritance from environment\n");

  for (ix = 0; ix < NACL_ARRAY_SIZE(g_nacl_redir_control); ++ix) {
    ndp = NULL;
    if (NULL != (env = getenv(g_nacl_redir_control[ix].env_name))) {
      NaClLog(4, "getenv(%s) -> %s\n", g_nacl_redir_control[ix].env_name, env);
      ndp = NaClResourceOpen((struct NaClResource *) &nap->resources,
                             env,
                             g_nacl_redir_control[ix].nacl_flags,
                             g_nacl_redir_control[ix].mode);
      NaClLog(4, " NaClResourceOpen returned %"NACL_PRIxPTR"\n",
              (uintptr_t) ndp);
    }

    if (NULL != ndp) {
      NaClLog(4, "Setting descriptor %d\n", (int) ix);
      NaClAppSetDesc(nap, (int) ix, ndp);
    } else {
      /*
       * Environment not set or redirect failed -- handle default inheritance.
       */
      NaClAddHostDescriptor(nap, DUP(g_nacl_redir_control[ix].d),
                            g_nacl_redir_control[ix].nacl_flags, (int) ix);
    }
  }

  NaClLog(4, "... done.\n");
}

void NaClCreateServiceSocket(struct NaClApp *nap) {
  struct NaClDesc *secure_pair[2];
  struct NaClDesc *pair[2];

  NaClLog(3, "Entered NaClCreateServiceSocket\n");

  if (NACL_FI_ERROR_COND("NaClCreateServiceSocket__secure_boundsock",
                         0 != NaClCommonDescMakeBoundSock(secure_pair))) {
    NaClLog(LOG_FATAL, "Cound not create secure service socket\n");
  }
  NaClLog(4,
          "got bound socket at 0x%08"NACL_PRIxPTR", "
          "addr at 0x%08"NACL_PRIxPTR"\n",
          (uintptr_t) secure_pair[0],
          (uintptr_t) secure_pair[1]);

  NaClDescSafeUnref(nap->secure_service_port);
  nap->secure_service_port = secure_pair[0];

  NaClDescSafeUnref(nap->secure_service_address);
  nap->secure_service_address = secure_pair[1];

  if (NACL_FI_ERROR_COND("NaClCreateServiceSocket__boundsock",
                         0 != NaClCommonDescMakeBoundSock(pair))) {
    NaClLog(LOG_FATAL, "Cound not create service socket\n");
  }
  NaClLog(4,
          "got bound socket at 0x%08"NACL_PRIxPTR", "
          "addr at 0x%08"NACL_PRIxPTR"\n",
          (uintptr_t) pair[0],
          (uintptr_t) pair[1]);
  NaClAppSetDesc(nap, NACL_SERVICE_PORT_DESCRIPTOR, pair[0]);
  NaClAppSetDesc(nap, NACL_SERVICE_ADDRESS_DESCRIPTOR, pair[1]);

  NaClDescSafeUnref(nap->service_port);

  nap->service_port = pair[0];
  NaClDescRef(nap->service_port);

  NaClDescSafeUnref(nap->service_address);

  nap->service_address = pair[1];
  NaClDescRef(nap->service_address);

  NaClLog(4, "Leaving NaClCreateServiceSocket\n");
}

/*
 * Import the |inherited_desc| descriptor as an IMC handle, save a
 * reference to it at nap->bootstrap_channel, then send the
 * service_address over that channel.
 */
void NaClSetUpBootstrapChannel(struct NaClApp  *nap,
                               NaClHandle      inherited_desc) {
  struct NaClDescImcDesc      *channel;
  struct NaClImcTypedMsgHdr   hdr;
  struct NaClDesc             *descs[2];
  ssize_t                     rv;

  NaClLog(4,
          "NaClSetUpBootstrapChannel(0x%08"NACL_PRIxPTR", %"NACL_PRIdPTR")\n",
          (uintptr_t) nap,
          (uintptr_t) inherited_desc);

  channel = (struct NaClDescImcDesc *) malloc(sizeof *channel);
  if (NULL == channel) {
    NaClLog(LOG_FATAL, "NaClSetUpBootstrapChannel: no memory\n");
  }
  if (!NaClDescImcDescCtor(channel, inherited_desc)) {
    NaClLog(LOG_FATAL,
            ("NaClSetUpBootstrapChannel: cannot construct IMC descriptor"
             " object for inherited descriptor %"NACL_PRIdPTR"\n"),
            (uintptr_t) inherited_desc);
    return;
  }
  if (NULL == nap->secure_service_address) {
    NaClLog(LOG_FATAL,
            "NaClSetUpBootstrapChannel: secure service address not set\n");
    return;
  }
  if (NULL == nap->service_address) {
    NaClLog(LOG_FATAL,
            "NaClSetUpBootstrapChannel: service address not set\n");
    return;
  }
  /*
   * service_address and service_port are set together.
   */
  descs[0] = nap->secure_service_address;
  descs[1] = nap->service_address;

  hdr.iov = (struct NaClImcMsgIoVec *) NULL;
  hdr.iov_length = 0;
  hdr.ndescv = descs;
  hdr.ndesc_length = NACL_ARRAY_SIZE(descs);

  rv = (*NACL_VTBL(NaClDesc, channel)->SendMsg)((struct NaClDesc *) channel,
                                                &hdr, 0);
  NaClXMutexLock(&nap->mu);
  if (NULL != nap->bootstrap_channel) {
    NaClLog(LOG_FATAL,
            "NaClSetUpBootstrapChannel: cannot have two bootstrap channels\n");
  }
  nap->bootstrap_channel = (struct NaClDesc *) channel;
  channel = NULL;
  NaClXMutexUnlock(&nap->mu);

  NaClLog(1,
          ("NaClSetUpBootstrapChannel: descriptor %"NACL_PRIdPTR
           ", error %"NACL_PRIdS"\n"),
          (uintptr_t) inherited_desc,
          rv);
  if (NACL_FI_ERROR_COND("NaClSetUpBootstrapChannel__SendMsg", 0 != rv)) {
    NaClLog(LOG_FATAL,
            "NaClSetUpBootstrapChannel: SendMsg failed, rv = %"NACL_PRIdS"\n",
            rv);
  }
}

enum NaClModuleInitializationState NaClGetInitState(struct NaClApp *nap) {
  enum NaClModuleInitializationState state;
  NaClXMutexLock(&nap->mu);
  state = nap->module_initialization_state;
  NaClXMutexUnlock(&nap->mu);
  return state;
}

void NaClSetInitState(struct NaClApp *nap,
                      enum NaClModuleInitializationState state) {
  NaClXMutexLock(&nap->mu);
  /* The initialization state should be increasing monotonically. */
  CHECK(state > nap->module_initialization_state);
  nap->module_initialization_state = state;
  NaClXCondVarBroadcast(&nap->cv);
  NaClXMutexUnlock(&nap->mu);
}

NaClErrorCode NaClWaitForLoadModuleCommand(struct NaClApp *nap) {
  NaClErrorCode status;

  NaClLog(4, "NaClWaitForLoadModuleCommand started\n");
  NaClXMutexLock(&nap->mu);
  while (nap->module_initialization_state < NACL_MODULE_LOADED) {
    NaClXCondVarWait(&nap->cv, &nap->mu);
  }
  status = nap->module_load_status;
  NaClXMutexUnlock(&nap->mu);
  NaClLog(4, "NaClWaitForLoadModuleCommand finished\n");

  return status;
}

void NaClRememberLoadStatus(struct NaClApp *nap, NaClErrorCode status) {
  NaClXMutexLock(&nap->mu);
  /* Remember the first error we encountered. */
  if (nap->module_load_status == LOAD_OK) {
    nap->module_load_status = status;
  }
  NaClXMutexUnlock(&nap->mu);
}


NaClErrorCode NaClGetLoadStatus(struct NaClApp *nap) {
  NaClErrorCode status;
  NaClXMutexLock(&nap->mu);
  status = nap->module_load_status;
  NaClXMutexUnlock(&nap->mu);
  return status;
}

NaClErrorCode NaClWaitForStartModuleCommand(struct NaClApp *nap) {
  NaClErrorCode status;

  NaClLog(4, "NaClWaitForStartModuleCommand started\n");
  NaClXMutexLock(&nap->mu);
  while (nap->module_initialization_state < NACL_MODULE_STARTED) {
    NaClXCondVarWait(&nap->cv, &nap->mu);
  }
  status = nap->module_load_status;
  NaClXMutexUnlock(&nap->mu);
  NaClLog(4, "NaClWaitForStartModuleCommand finished\n");

  return status;
}

void NaClBlockIfCommandChannelExists(struct NaClApp *nap) {
  if (NULL != nap->secure_service) {
    for (;;) {
      struct nacl_abi_timespec req;
      req.tv_sec = 1000;
      req.tv_nsec = 0;
      NaClNanosleep(&req, (struct nacl_abi_timespec *) NULL);
    }
  }
}

void NaClSecureCommandChannel(struct NaClApp *nap) {
  struct NaClSecureService *secure_command_server;

  NaClLog(4, "Entered NaClSecureCommandChannel\n");

  secure_command_server = (struct NaClSecureService *) malloc(
      sizeof *secure_command_server);
  if (NACL_FI_ERROR_COND("NaClSecureCommandChannel__malloc",
                         NULL == secure_command_server)) {
    NaClLog(LOG_FATAL, "Out of memory for secure command channel\n");
  }
  if (NACL_FI_ERROR_COND("NaClSecureCommandChannel__NaClSecureServiceCtor",
                         !NaClSecureServiceCtor(secure_command_server,
                                                nap,
                                                nap->secure_service_port,
                                                nap->secure_service_address))) {
    NaClLog(LOG_FATAL, "NaClSecureServiceCtor failed\n");
  }
  nap->secure_service = secure_command_server;

  NaClLog(4, "NaClSecureCommandChannel: starting service thread\n");
  if (NACL_FI_ERROR_COND(
          "NaClSecureCommandChannel__NaClSimpleServiceStartServiceThread",
          !NaClSimpleServiceStartServiceThread((struct NaClSimpleService *)
                                               secure_command_server))) {
    NaClLog(LOG_FATAL,
            "Could not start secure command channel service thread\n");
  }

  NaClLog(4, "Leaving NaClSecureCommandChannel\n");
}


void NaClAppLoadModule(struct NaClApp   *nap,
                       struct NaClDesc  *nexe,
                       void             (*load_cb)(void *instance_data,
                                                   NaClErrorCode status),
                       void             *instance_data) {
  NaClErrorCode status = LOAD_OK;
  int is_double_init = NaClGetInitState(nap) != NACL_MODULE_UNINITIALIZED;

  NaClLog(4,
          ("Entered NaClAppLoadModule: nap 0x%"NACL_PRIxPTR","
           " nexe 0x%"NACL_PRIxPTR"\n"),
          (uintptr_t) nap, (uintptr_t) nexe);

  if (NULL != load_cb) {
    NaClErrorCode cb_status;
    if (is_double_init) {
      cb_status = LOAD_DUP_LOAD_MODULE;
    } else {
      cb_status = LOAD_OK;
    }
    (*load_cb)(instance_data, cb_status);
  }

  if (is_double_init) {
    NaClLog(LOG_ERROR, "NaClAppLoadModule: repeated invocation\n");
    return;
  }

  NaClSetInitState(nap, NACL_MODULE_LOADING);

  /*
   * Ref was passed by value into |nexe| parameter, so up the refcount.
   * Be sure to unref when the parameter's copy goes out of scope
   * (when returning).
   */
  NaClDescRef(nexe);

  NaClXMutexLock(&nap->mu);

  /* Transfer ownership from nexe to nap->main_nexe_desc. */
  CHECK(nap->main_nexe_desc == NULL);
  nap->main_nexe_desc = nexe;
  nexe = NULL;

  status = NACL_FI_VAL("load_module", NaClErrorCode,
                       NaClAppLoadFile(nap->main_nexe_desc, nap));

  if (LOAD_OK != status) {
    NaClDescUnref(nap->main_nexe_desc);
    nap->main_nexe_desc = NULL;
  }
  NaClXMutexUnlock(&nap->mu);  /* NaClAppPrepareToLaunch takes mu */

  if (LOAD_OK != status) {
    NaClRememberLoadStatus(nap, status);
    NaClSetInitState(nap, NACL_MODULE_ERROR);
    return;
  }

  /***************************************************************************
   * TODO(bsy): Remove/merge the code invoking NaClAppPrepareToLaunch
   * and NaClGdbHook below with sel_main's main function.  See comment
   * there.
   ***************************************************************************/

  /*
   * Finish setting up the NaCl App.
   */
  status = NaClAppPrepareToLaunch(nap);
  NaClRememberLoadStatus(nap, status);
  NaClSetInitState(nap, NACL_MODULE_LOADED);

  /* Give debuggers a well known point at which xlate_base is known.  */
  NaClGdbHook(nap);
}

void NaClAppStartModule(struct NaClApp  *nap,
                        void            (*start_cb)(void *instance_data,
                                                    NaClErrorCode status),
                        void            *instance_data) {
  NaClErrorCode status;

  NaClLog(4,
          ("Entered NaClAppStartModule, nap 0x%"NACL_PRIxPTR","
           " start_cb 0x%"NACL_PRIxPTR", instance_data 0x%"NACL_PRIxPTR"\n"),
          (uintptr_t) nap, (uintptr_t) start_cb, (uintptr_t) instance_data);

  /*
   * When module is loading, we have to block and wait till it is
   * fully loaded before we can proceed with start module.
   */
  NaClXMutexLock(&nap->mu);
  if (NACL_MODULE_LOADING == nap->module_initialization_state) {
    while (NACL_MODULE_LOADING == nap->module_initialization_state) {
      NaClXCondVarWait(&nap->cv, &nap->mu);
    }
  }
  status = nap->module_load_status;
  if (nap->module_initialization_state != NACL_MODULE_LOADED) {
    if (NACL_MODULE_ERROR == nap->module_initialization_state) {
      NaClLog(LOG_ERROR, "NaClAppStartModule: error loading module\n");
    } else if (nap->module_initialization_state > NACL_MODULE_LOADED) {
      NaClLog(LOG_ERROR, "NaClAppStartModule: repeated invocation\n");
      status = LOAD_DUP_START_MODULE;
    } else if (nap->module_initialization_state < NACL_MODULE_LOADED) {
      NaClLog(LOG_ERROR, "NaClAppStartModule: module not loaded\n");
      status = LOAD_INTERNAL;
    }
    NaClXMutexUnlock(&nap->mu);
    if (NULL != start_cb) {
      (*start_cb)(instance_data, status);
    }
    return;
  }
  NaClXMutexUnlock(&nap->mu);

  NaClSetInitState(nap, NACL_MODULE_STARTING);

  NaClLog(4, "NaClSecureChannelStartModule: load status %d\n", status);

  /*
   * We need to invoke the callback now, before we signal the main thread
   * to possibly start by setting the state to NACL_MODULE_STARTED, since
   * in the case of failure the main thread may quickly exit; if the main
   * thread does this before we sent the reply, than the plugin (or any
   * other runtime host interface) will be left without an aswer. The
   * NACL_MODULE_STARTING state is used as an intermediate state to prevent
   * double invocations violating the protocol.
   */
  if (NULL != start_cb) {
    (*start_cb)(instance_data, status);
  }

  NaClSetInitState(nap, NACL_MODULE_STARTED);
}

void NaClAppShutdown(struct NaClApp     *nap,
                     int                exit_status) {
  NaClLog(4, "NaClAppShutdown: nap 0x%"NACL_PRIxPTR
          ", exit_status %d\n", (uintptr_t) nap, exit_status);

  NaClXMutexLock(&nap->mu);
  nap->exit_status = exit_status;
  NaClXMutexUnlock(&nap->mu);
  if (NULL != nap->debug_stub_callbacks) {
    nap->debug_stub_callbacks->process_exit_hook();
  }
  NaClExit(0);
}

/*
 * It is fine to have multiple I/O operations read from memory in Write
 * or SendMsg like operations.
 */
void NaClVmIoWillStart(struct NaClApp *nap,
                       uint32_t addr_first_usr,
                       uint32_t addr_last_usr) {
  NaClXMutexLock(&nap->mu);
  (*nap->mem_io_regions->vtbl->AddInterval)(nap->mem_io_regions,
                                            addr_first_usr,
                                            addr_last_usr);
  NaClXMutexUnlock(&nap->mu);
}


void NaClVmIoHasEnded(struct NaClApp *nap,
                      uint32_t addr_first_usr,
                      uint32_t addr_last_usr) {
  NaClXMutexLock(&nap->mu);
  (*nap->mem_io_regions->vtbl->RemoveInterval)(nap->mem_io_regions,
                                               addr_first_usr,
                                               addr_last_usr);
  NaClXMutexUnlock(&nap->mu);
}

void NaClVmIoPendingCheck_mu(struct NaClApp *nap,
                             uint32_t addr_first_usr,
                             uint32_t addr_last_usr) {
  if ((*nap->mem_io_regions->vtbl->OverlapsWith)(nap->mem_io_regions,
                                                 addr_first_usr,
                                                 addr_last_usr)) {
    NaClLog(LOG_FATAL,
            "NaClVmIoWillStart: program mem write race detected. ABORTING\n");
  }
}

/*
 * GDB's canonical overlay managment routine.
 * We need its symbol in the symbol table so don't inline it.
 * TODO(dje): add some explanation for the non-GDB person.
 */
#if NACL_WINDOWS
__declspec(dllexport noinline)
#endif
#ifdef __GNUC__
__attribute__((noinline))
#endif
void _ovly_debug_event (void) {
#ifdef __GNUC__
  /*
   * The asm volatile is here as instructed by the GCC docs.
   * It's not enough to declare a function noinline.
   * GCC will still look inside the function to see if it's worth calling.
   */
  __asm__ volatile ("");
#elif NACL_WINDOWS
  /*
   * Visual Studio inlines empty functions even with noinline attribute,
   * so we need a compile memory barrier to make this function not to be
   * inlined. Also, it guarantees that nacl_global_xlate_base initialization
   * is not reordered. This is important for gdb since it sets breakpoint on
   * this function and reads nacl_global_xlate_base value.
   */
  _ReadWriteBarrier();
#endif
}

static void StopForDebuggerInit (uintptr_t mem_start) {
  /* Put xlate_base in a place where gdb can find it.  */
  nacl_global_xlate_base = mem_start;

  NaClSandboxMemoryStartForValgrind(mem_start);

  _ovly_debug_event();
}

void NaClGdbHook(struct NaClApp const *nap) {
  StopForDebuggerInit(nap->mem_start);
}

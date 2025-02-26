/* Copyright (C) 2002-2014 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@redhat.com>, 2002.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

/* Default stack size.  */
#define ARCH_STACK_DEFAULT_SIZE	(2 * 1024 * 1024)

/* Required stack pointer alignment at beginning.  SSE requires 16
   bytes.  */
#define STACK_ALIGN		16

/* Minimal stack size after allocating thread descriptor and guard size.  */
#define MINIMAL_REST_STACK	2048

/* Alignment requirement for TCB.

   We need to store post-AVX vector registers in the TCB and we want the
   storage to be aligned to at least 32 bytes.

   Some processors such as Intel Atom pay a big penalty on every
   access using a segment override if that segment's base is not
   aligned to the size of a cache line.  (See Intel 64 and IA-32
   Architectures Optimization Reference Manual, section 13.3.3.3,
   "Segment Base".)  On such machines, a cache line is 64 bytes.  */
#define TCB_ALIGNMENT		64


/* Location of current stack frame.  The frame pointer is not usable.  */
#define CURRENT_STACK_FRAME \
  ({ register char *frame __asm__("rsp"); frame; })


/* XXX Until we have a better place keep the definitions here.  */

/* While there is no such syscall.  */
#define __exit_thread_inline(val) \
  asm volatile ("movq syscalldb@GOTPCREL(%%rip), %%rbx\n"\
                "jmp *%%rbx":: "a" (__NR_exit), "D" (val))

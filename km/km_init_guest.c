/*
 * Copyright 2021 Kontain Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * After the guest is loaded on memory, initialize it's execution environment.
 * This includes passing parameters to main.
 */

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "km.h"
#include "km_exec.h"
#include "km_gdb.h"
#include "km_guest.h"
#include "km_mem.h"
#include "km_proc.h"
#include "km_syscall.h"
#include "x86_cpu.h"

/*
 * Allocate stack for main thread and initialize it according to ABI:
 * https://www.uclibc.org/docs/psABI-x86_64.pdf,
 * https://software.intel.com/sites/default/files/article/402129/mpx-linux64-abi.pdf (not sure which
 * is newer so keep both references).
 *
 * We are not freeing the stack until we get to km_machine_fini(), similar to memory allocated by
 * km_load_elf() for executable and data part of the program.
 *
 * Care needs to be taken to distinguish between addresses seen in the guest and in km. We have two
 * sets of variables for each structure we deal with, like stack_top and stack_top_kma, former and
 * latter correspondingly. The guest addresses are, as everywhere in km, of km_gva_t (aka uint64_t).
 *
 * See TLS description as referenced in gcc manual https://gcc.gnu.org/onlinedocs/gcc-9.2.0/gcc.pdf
 * in section 6.64 Thread-Local Storage, at the time of this writing pointing at
 * https://www.akkadia.org/drepper/tls.pdf.
 *
 * Return the location of argv in guest
 */

km_gva_t km_init_main(km_vcpu_t* vcpu, int argc, char* const argv[], int envc, char* const envp[])
{
   km_gva_t map_base;

   if ((map_base = km_guest_mmap_simple(GUEST_STACK_SIZE)) == FAILED_GA) {
      km_err(1, "Failed to allocate memory for main stack");
   }
   km_gva_t stack_top = map_base + GUEST_STACK_SIZE;
   km_kma_t stack_top_kma = km_gva_to_kma_nocheck(stack_top);

   // Set environ - copy strings and prep the envp array
   char* km_envp[envc + 1];
   memcpy(km_envp, envp, sizeof(km_envp) - sizeof(char*));
   for (int i = 0; i < envc - 1; i++) {
      int len = strnlen(km_envp[i], PATH_MAX) + 1;

      stack_top -= len;
      stack_top_kma -= len;
      if (map_base + GUEST_STACK_SIZE - stack_top > GUEST_ARG_MAX) {
         km_errx(2, "Environment list is too large");
      }
      strncpy(stack_top_kma, km_envp[i], len);
      km_envp[i] = (char*)stack_top;
   }
   stack_top = rounddown(stack_top, sizeof(void*));
   stack_top_kma = km_gva_to_kma_nocheck(stack_top);

   // copy arguments and form argv array
   km_gva_t argv_km[argc + 1];   // argv to copy to guest stack_init
   argv_km[argc] = 0;
   for (int i = argc - 1; i >= 0; i--) {
      int len = strnlen(argv[i], PATH_MAX) + 1;

      stack_top -= len;
      if (map_base + GUEST_STACK_SIZE - stack_top > GUEST_ARG_MAX) {
         km_errx(2, "Argument list is too large");
      }
      argv_km[i] = stack_top;
      stack_top_kma -= len;
      strncpy(stack_top_kma, argv[i], len);
   }

   static const_string_t pstr = "X86_64";
   int pstr_len = strlen(pstr) + 1;
   stack_top -= pstr_len;
   stack_top_kma -= pstr_len;
   strcpy(stack_top_kma, pstr);
   km_gva_t platform_gva = stack_top;

   // AT_RANDOM random data for seeds (16 bytes)
   km_gva_t at_random = 0;
   pstr_len = 16;
   stack_top -= pstr_len;
   stack_top_kma -= pstr_len;
   ssize_t rc = getrandom(stack_top_kma, pstr_len, 0);
   if (rc != pstr_len) {
      km_warnx("getrandom() didn't return enough bytes, expected %d, got %ld", pstr_len, rc);
      stack_top += pstr_len;
      stack_top_kma += pstr_len;
   } else {
      at_random = stack_top;
   }

   /*
    * ABI wants the stack 16 bytes aligned at the end of this, when we place
    * argc on the stack. From this point down we are going to place a null AUXV
    * entry (8 bytes), AUXV, env pointers and zero entry, argv pointers and zero
    * entry, and finally argc. AUXV entries are 16 bytes so they don't change the
    * alignment. env and argv pointers are 8 bytes though, so we adjust for
    * evenness of them together.
    *
    * Reference: https://www.uclibc.org/docs/psABI-x86_64.pdf
    */
   stack_top = rounddown(stack_top, sizeof(void*) * 2);
   if ((argc + envc) % 2 != 0) {
      stack_top -= sizeof(void*);
   }
   stack_top_kma = km_gva_to_kma_nocheck(stack_top);
   void* auxv_end = stack_top_kma;
   // AUXV
#define NEW_AUXV_ENT(type, val)                                                                    \
   {                                                                                               \
      stack_top -= 2 * sizeof(void*);                                                              \
      stack_top_kma -= 2 * sizeof(void*);                                                          \
      uint64_t* ptr = stack_top_kma;                                                               \
      ptr[0] = (type);                                                                             \
      ptr[1] = (uint64_t)(val);                                                                    \
   }

   NEW_AUXV_ENT(0, 0);
   NEW_AUXV_ENT(AT_PLATFORM, platform_gva);
   NEW_AUXV_ENT(AT_EXECFN, argv_km[0]);
   if (at_random != 0) {
      NEW_AUXV_ENT(AT_RANDOM, at_random);
   }
   NEW_AUXV_ENT(AT_SECURE, 0);
   NEW_AUXV_ENT(AT_EGID, 0);
   NEW_AUXV_ENT(AT_GID, 0);
   NEW_AUXV_ENT(AT_EUID, 0);
   NEW_AUXV_ENT(AT_UID, 0);
   NEW_AUXV_ENT(AT_ENTRY, km_guest.km_ehdr.e_entry + km_guest.km_load_adjust);
   NEW_AUXV_ENT(AT_FLAGS, 0);
   if (km_dynlinker.km_filename != 0) {
      NEW_AUXV_ENT(AT_BASE, km_dynlinker.km_load_adjust);
   }
   NEW_AUXV_ENT(AT_PHNUM, km_guest.km_ehdr.e_phnum);
   NEW_AUXV_ENT(AT_PHENT, km_guest.km_ehdr.e_phentsize);
   /*
    * Set AT_PHDR. Prefer PT_PHDR if it exists use it. If no PT_PHDR exists
    * set based on first PT_LOAD found.
    */
   int phdr_found = 0;
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      if (km_guest.km_phdr[i].p_type == PT_PHDR) {
         NEW_AUXV_ENT(AT_PHDR, km_guest.km_phdr[i].p_vaddr + km_guest.km_load_adjust);
         phdr_found = 1;
         break;
      }
   }
   if (phdr_found == 0) {
      for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
         if (km_guest.km_phdr[i].p_type == PT_LOAD) {
            NEW_AUXV_ENT(AT_PHDR,
                         km_guest.km_ehdr.e_phoff + km_guest.km_phdr[i].p_vaddr +
                             km_guest.km_load_adjust);
            phdr_found = 1;
            break;
         }
      }
   }
   km_assert(phdr_found != 0);
   NEW_AUXV_ENT(AT_CLKTCK, sysconf(_SC_CLK_TCK));
   NEW_AUXV_ENT(AT_PAGESZ, KM_PAGE_SIZE);
   // TODO: AT_HWCAP
   if (km_vvar_vdso_base[1] != 0) {
      NEW_AUXV_ENT(AT_SYSINFO_EHDR, km_vvar_vdso_base[1]);
   }
#undef NEW_AUXV_ENT

   // A safe copy of auxv for coredump (if needed)
   machine.auxv_size = auxv_end - stack_top_kma;
   machine.auxv = malloc(machine.auxv_size);
   km_assert(machine.auxv);
   memcpy(machine.auxv, stack_top_kma, machine.auxv_size);

   // place envp array
   size_t envp_sz = sizeof(km_envp[0]) * envc;
   stack_top_kma -= envp_sz;
   stack_top -= envp_sz;
   memcpy(stack_top_kma, km_envp, envp_sz);

   // place argv array
   stack_top_kma -= sizeof(argv_km);
   stack_top -= sizeof(argv_km);
   memcpy(stack_top_kma, argv_km, sizeof(argv_km));
   // place argc
   stack_top_kma -= sizeof(uint64_t);
   stack_top -= sizeof(uint64_t);
   *(uint64_t*)stack_top_kma = argc;

   vcpu->stack_top = stack_top;
   return stack_top;   // argv in the guest
}

/*
 * vcpu was obtained by km_vcpu_get(), state is STARTING. The registers and memory is fully prepared
 * to go. We need to create a thread if this is brand new vcpu, or signal the thread if it is reused
 * vcpu. Setting state to HYPERCALL signals the thread to start running.
 */
int km_run_vcpu_thread(km_vcpu_t* vcpu)
{
   int rc = 0;

   if (vcpu->vcpu_thread == 0) {
      pthread_attr_t att;

      km_attr_init(&att);
      km_attr_setstacksize(&att, 16 * KM_PAGE_SIZE);
      km_lock_vcpu_thr(vcpu);
      vcpu->state = HYPERCALL;
      if ((rc = -pthread_create(&vcpu->vcpu_thread, &att, (void* (*)(void*))km_vcpu_run, vcpu)) != 0) {
         vcpu->state = STARTING;
      }
      km_unlock_vcpu_thr(vcpu);
      km_attr_destroy(&att);
   } else {
      km_lock_vcpu_thr(vcpu);
      vcpu->state = HYPERCALL;
      km_cond_signal(&vcpu->thr_cv);
      km_unlock_vcpu_thr(vcpu);
   }
   if (rc != 0) {
      km_info(KM_TRACE_VCPU, "run_vcpu_thread: failed activating vcpu thread");
   }
   return rc;
}

void km_vcpu_stopped(km_vcpu_t* vcpu)
{
   km_lock_vcpu_thr(vcpu);
   km_exit(vcpu);   // release user space thread list lock, do delayed stack unmap
   km_vcpu_put(vcpu);

   while (vcpu->state != HYPERCALL) {
      km_cond_wait(&vcpu->thr_cv, &vcpu->thr_mtx);
   }
   km_unlock_vcpu_thr(vcpu);
}

int km_clone(km_vcpu_t* vcpu,
             unsigned long flags,
             uint64_t child_stack,
             km_gva_t ptid,
             km_gva_t ctid,
             unsigned long newtls)
{
   km_assert((flags & CLONE_THREAD) != 0);   // we only have thread clone here.

   child_stack &= ~0x7;
   if (km_gva_to_kma(child_stack - 8) == NULL) {   // check if the stack points to legitimate memory
      return -EINVAL;
   }
   km_vcpu_t* new_vcpu = km_vcpu_get();
   if (new_vcpu == NULL) {
      return -EAGAIN;
   }

   if ((flags & CLONE_CHILD_SETTID) != 0) {
      new_vcpu->set_child_tid = ctid;
   }
   if ((flags & CLONE_CHILD_CLEARTID) != 0) {
      new_vcpu->clear_child_tid = ctid;
   }
   new_vcpu->stack_top = child_stack;
   new_vcpu->guest_thr = newtls;
   int rc = km_vcpu_clone_to_run(vcpu, new_vcpu);
   if (rc < 0) {
      km_vcpu_put(new_vcpu);
      return rc;
   }

   // Obey parent set tid protocol
   if ((flags & CLONE_PARENT_SETTID) != 0) {
      int* lptid = km_gva_to_kma(ptid);
      if (lptid != NULL) {
         *lptid = km_vcpu_get_tid(new_vcpu);
      }
   }

   // Obey set_child_tid protocol for pthreads. See 'clone(2)'
   int* gtid;
   if ((gtid = km_gva_to_kma(new_vcpu->set_child_tid)) != NULL) {
      *gtid = km_vcpu_get_tid(new_vcpu);
   }

   km_infox(KM_TRACE_VCPU,
            "starting new thread on vcpu-%d: RIP 0x%0llx RSP 0x%0llx",
            new_vcpu->vcpu_id,
            new_vcpu->regs.rip,
            new_vcpu->regs.rsp);
   if (km_run_vcpu_thread(new_vcpu) < 0) {
      km_vcpu_put(new_vcpu);
      return -EAGAIN;
   }

   return km_vcpu_get_tid(new_vcpu);
}

long km_clone3(km_vcpu_t* vcpu, struct clone_args* cl_args)
{
   if (cl_args->set_tid_size != 0 || cl_args->cgroup != 0) {
      km_warnx("Not supported clone3 args: tid_size, cgroup %llu, %llu",
               cl_args->set_tid_size,
               cl_args->cgroup);
      return -ENOSYS;
   }
   return km_clone(vcpu,
                   cl_args->flags | cl_args->exit_signal,
                   cl_args->stack + cl_args->stack_size,
                   cl_args->parent_tid,
                   cl_args->child_tid,
                   cl_args->tls);
}

uint64_t km_set_tid_address(km_vcpu_t* vcpu, km_gva_t tidptr)
{
   vcpu->clear_child_tid = tidptr;
   return km_vcpu_get_tid(vcpu);
}

void km_exit(km_vcpu_t* vcpu)
{
   if (vcpu->clear_child_tid != 0) {
      // See 'man 2 set_tid_address'
      int* ctid = km_gva_to_kma(vcpu->clear_child_tid);
      if (ctid != NULL) {
         *ctid = 0;
         __syscall_6(SYS_futex, (uintptr_t)ctid, FUTEX_WAKE, 1, 0, 0, 0);
      }
   }
   km_delayed_munmap(vcpu);
}

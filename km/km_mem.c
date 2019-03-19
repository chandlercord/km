/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Guest memory-related code
 */

#define _GNU_SOURCE
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>

#include "km.h"
#include "km_mem.h"
#include "x86_cpu.h"

/*
 * Physical memory layout:
 *
 * - 4k hole
 * - 63 pages reserved area, used for pml4, pdpt, gdt, idt
 * - hole till 2MB, 0x200000
 * - User space, starting with 2MB region, each next increasing by two:
 *   2MB, 4MB, 8MB ... 512MB, 1GB, ... 256GB.
 *   We start with just 2MB region, then add the subsequent ones as necessary.
 * - hole from the end of user space to (guest_max_physmem - 1GB)
 * - last GB is reserved for stacks. The first thread stack is 2MB at the top of
 *   that GB
 *
 * We are forced to stay within width of the CPU physical memory bus. We
 * determine that the by analyzing CPUID and store in machine.guest_max_physmem
 */

static inline km_kma_t km_resv_kma(void)
{
   return (km_kma_t)machine.vm_mem_regs[KM_RSRV_MEMSLOT].userspace_addr;
}

static void pml4e_set(x86_pml4e_t* pml4e, uint64_t pdpt)
{
   pml4e->p = 1;
   pml4e->r_w = 1;
   pml4e->u_s = 1;
   pml4e->pdpt = pdpt >> 12;
}

static void pdpte_set(x86_pdpte_t* pdpe, uint64_t pd)
{
   pdpe->p = 1;
   pdpe->r_w = 1;
   pdpe->u_s = 1;
   pdpe->pd = pd >> 12;
}

static void pdpte_1g_set(x86_pdpte_1g_t* pdpe, uint64_t addr)
{
   pdpe->p = 1;
   pdpe->r_w = 1;
   pdpe->u_s = 1;
   pdpe->ps = 1;
   pdpe->page = addr >> 30;
}

static void pde_2mb_set(x86_pde_2m_t* pde, u_int64_t addr)
{
   pde->p = 1;
   pde->r_w = 1;
   pde->u_s = 1;
   pde->ps = 1;
   pde->page = addr >> 21;
}

/*
 * Virtual Memory layout:
 *
 * Virtual address space starts from virtual address GUEST_MEM_START_VA (2MB) and ends at virtual
 * address GUEST_MEM_TOP_VA which is chosen to be, somewhat randonly, at the last 2MB of the
 * first half of 48 bit wide virtual address space. See km.h for values.
 *
 * The virtual address space contains 2 regions - bottom, growing up (for guest
 * text and data) and top, growing down (for stacks and mmap-ed regions).
 *
 * Bottom region: guest text and data region starts from the beginning of the virtual address space.
 * It grows (upwards) and shrinks at the guest requests via brk() call. The end of this region for
 * the purposes of brk() is machine.brk.
 *
 * Top region: stack and mmap-allocated region starts from the end of the virtual address space. It
 * grows (downwards) and shrinks at the guest requests via mmap() call, as well as Monitor requests
 * via km_guest_mmap_simple() call, usually used for stack(s) allocation. The end (i.e. the lowest
 * address) of this region is machine.tbrk.
 *
 * Total amount of guest virtual memory (bottom region + top region) is currently limited to
 * 'guest_max_physical_mem-4MB' (i.e. 512GB-4MB).  Virtual to physical is essentially 1:1
 * mapping, but the top region virtual addresses are equal to physical_addresses + GUEST_VA_OFFSET.
 *
 * We use 2MB pages for the first and last GB of virtual space, and 1GB pages for the rest. With the
 * guest_max_physmem == 512GB we need two pml4 entries, #0 and #255. #0 covers the text and data (up
 * to machine.brk), the #255 mmap and stack area (from machine.tbrk up). There are correspondingly two
 * pdpt pages. Since we allow 2MB pages only for the first and last GB, we use only 2 pdp tables. The
 * rest of VA is covered by 1GB-wide slots in relevant PDPT. The first entry in the
 * first PDP table, representing first 2MB of address space, it always empty. Same for the last
 * entry in the second PDP table - it is always empty. Usable virtual memory starts from 2MB and
 * ends at GUEST_MEM_TOP_VA-2MB. Usable physical memory starts at 2MB and ends at 512GB-2MB.
 * Initially there is no memory allocated, then it expands with brk (left to right in the picture
 * below) or with mmap (right to left).
 *
 * If we ever go over the 512GB limit there will be a need for more pdpt pages. The first entry
 * points to the pd page that covers the first GB.
 *
 * The picture illustrates layout with the constants values as set.
 * The code below is a little more flexible, allowing one or two pdpt pages and
 * pml4 entries, as well as variable locations in pdpt table
 *
 * // clang-format off
 *
 *           0|      |255
 *           [----------------] pml4 page
 *            |      |
 *         __/        \___________________
 *        /                               \
 *       |                                 |511
 *      [----------------] [----------------] pdpt pages
 *       |                                 |
 *       |\________                        |
 *       |         \                       |
 *      [----------------] [----------------] pd pages
 *        |                                |
 *      2 - 4 MB                 512GB-2MB - 512GB    <==== physical addresses
 *
 * // clang-format on
 */

/* virtual address space covered by one pml4 entry */
static const uint64_t PML4E_REGION = 512 * GIB;
/* Same for pdpt entry */
static const uint64_t PDPTE_REGION = GIB;
/* same for pd entry */
static const uint64_t PDE_REGION = 2 * MIB;

/*
 * Slot number in PDE table for gva '_addr'.
 * Logically: (__addr % PDPTE_REGION) / PDE_REGION
 */
static inline int PDE_SLOT(km_gva_t __addr)
{
   return ((__addr)&0x3ffffffful) >> 21;
}

/*
 * Slot number for 1G chunk in PDPT table for gva '_addr'
 * Logically:  (__addr % PML4E_REGION) / PDPTE_REGION
 */
static inline int PDPTE_SLOT(km_gva_t __addr)
{
   return ((__addr)&0x7ffffffffful) >> 30;
}

static void init_pml4(km_kma_t mem)
{
   x86_pml4e_t* pml4e;
   x86_pdpte_t* pdpe;
   int idx;

   // The code assumes that a single PML4 slot can cover all available physical memory.
   assert(machine.guest_max_physmem <= PML4E_REGION);
   // The code assumes that PA and VA are aligned within PDE table (which covers PDPTE_REGION)
   assert(GUEST_VA_OFFSET % PDPTE_REGION == 0);

   pml4e = mem + RSV_PML4_OFFSET;
   memset(pml4e, 0, PAGE_SIZE);
   pml4e_set(pml4e, RSV_GUEST_PA(RSV_PDPT_OFFSET));   // entry #0

   pdpe = mem + RSV_PDPT_OFFSET;
   memset(pdpe, 0, PAGE_SIZE);
   pdpte_set(pdpe, RSV_GUEST_PA(RSV_PD_OFFSET));   // first entry for the first GB

   // Since in our mem layout the two mem regions are always more
   // than 512 GB apart, make the second pml4 entry pointing to the second pdpt page
   idx = (GUEST_MEM_TOP_VA - 1) / PML4E_REGION;
   pml4e_set(pml4e + idx, RSV_GUEST_PA(RSV_PDPT2_OFFSET));
   pdpe = mem + RSV_PDPT2_OFFSET;
   memset(pdpe, 0, PAGE_SIZE);

   idx = PDE_SLOT(GUEST_MEM_TOP_VA);
   pdpte_set(pdpe + idx, RSV_GUEST_PA(RSV_PD2_OFFSET));

   memset(mem + RSV_PD_OFFSET, 0, PAGE_SIZE);    // clear page, no usable entries
   memset(mem + RSV_PD2_OFFSET, 0, PAGE_SIZE);   // clear page, no usable entries
}

void* km_page_malloc(size_t size)
{
   km_kma_t addr;

   if ((size & (PAGE_SIZE - 1)) != 0) {
      errno = EINVAL;
      return NULL;
   }
   if ((addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) ==
       MAP_FAILED) {
      return NULL;
   }
   return addr;
}

void km_page_free(void* addr, size_t size)
{
   munmap(addr, size);
}

/* simple wrapper to avoid polluting all callers with mmap.h */
km_gva_t km_guest_mmap_simple(size_t size)
{
   return km_guest_mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
}

/*
 * Create reserved memory, initialize PML4 and brk.
 */
void km_mem_init(void)
{
   kvm_mem_reg_t* reg;
   void* ptr;

   reg = &machine.vm_mem_regs[KM_RSRV_MEMSLOT];
   if ((ptr = km_page_malloc(RSV_MEM_SIZE)) == NULL) {
      err(1, "KVM: no memory for reserved pages");
   }
   reg->userspace_addr = (typeof(reg->userspace_addr))ptr;
   reg->slot = KM_RSRV_MEMSLOT;
   reg->guest_phys_addr = RSV_MEM_START;
   reg->memory_size = RSV_MEM_SIZE;
   reg->flags = 0;   // set to KVM_MEM_READONLY for readonly
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      err(1, "KVM: set reserved region failed");
   }
   init_pml4((km_kma_t)reg->userspace_addr);

   machine.brk = GUEST_MEM_START_VA - 1;   // last allocated byte
   machine.tbrk = GUEST_MEM_TOP_VA;
   machine.guest_mid_physmem = machine.guest_max_physmem >> 1;
   machine.mid_mem_idx = MEM_IDX(machine.guest_mid_physmem - 1);
   // Place for the last 2MB of PA. We do not allocate it to make memregs mirrored
   machine.last_mem_idx = (machine.mid_mem_idx << 1) + 1;
   km_guest_mmap_init();
}

/*
 * Set and clear pml4 hierarchy entries to reflect addition or removal of reg,
 * following the setup in init_pml4(). Set/Clears pdpte or pde depending on the
 * size.
 */
static void set_pml4_hierarchy(kvm_mem_reg_t* reg, int upper_va)
{
   size_t size = reg->memory_size;
   km_gva_t base = reg->guest_phys_addr;

   if (size < PDPTE_REGION) {
      x86_pde_2m_t* pde = km_resv_kma() + (upper_va ? RSV_PD2_OFFSET : RSV_PD_OFFSET);
      for (uint64_t addr = base; addr < base + size; addr += PDE_REGION) {
         // virtual and physical mem aligned the same on PDE_REGION, so we can use use addr for virt.addr
         pde_2mb_set(pde + PDE_SLOT(addr), addr);
      }
   } else {
      assert(machine.pdpe1g != 0);
      x86_pdpte_1g_t* pdpe = km_resv_kma() + (upper_va ? RSV_PDPT2_OFFSET : RSV_PDPT_OFFSET);
      uint64_t gva = gpa_to_upper_gva(base);
      for (uint64_t addr = gva; addr < gva + size; addr += PDPTE_REGION, base += PDPTE_REGION) {
         pdpte_1g_set(pdpe + PDPTE_SLOT(addr), base);
      }
   }
}

static void clear_pml4_hierarchy(kvm_mem_reg_t* reg, int upper_va)
{
   uint64_t size = reg->memory_size;
   uint64_t base = reg->guest_phys_addr;

   if (size < PDPTE_REGION) {
      x86_pde_2m_t* pde = km_resv_kma() + (upper_va ? RSV_PD2_OFFSET : RSV_PD_OFFSET);
      for (uint64_t addr = base; addr < base + size; addr += PDE_REGION) {
         // virtual and physical mem aligned the same on PDE_REGION, so we can use phys. address in
         // PDE_SLOT()
         memset(pde + PDE_SLOT(addr), 0, sizeof(*pde));
      }
   } else {
      assert(machine.pdpe1g != 0);   // no 1GB pages support
      x86_pdpte_1g_t* pdpe = km_resv_kma() + (upper_va ? RSV_PDPT2_OFFSET : RSV_PDPT_OFFSET);
      uint64_t gva = gpa_to_upper_gva(base);
      for (uint64_t addr = gva; addr < gva + size; addr += PDPTE_REGION, base += PDPTE_REGION) {
         memset(pdpe + PDPTE_SLOT(addr), 0, sizeof(*pdpe));
      }
   }
}

/*
 * Allocate memory 'size' and configure it as mem region 'idx', supporting
 * VA at from idx->guest_phys_addr + va_offset.
 *
 * Return 0 for success and -errno for errors
 */
static int km_alloc_region(int idx, size_t size, int upper_va)
{
   void* ptr;
   kvm_mem_reg_t* reg = &machine.vm_mem_regs[idx];
   km_gva_t base = memreg_base(idx);

   assert(reg->memory_size == 0 && reg->userspace_addr == 0);
   assert(machine.pdpe1g || (base < GIB || base >= machine.guest_max_physmem - GIB));
   if ((ptr = km_page_malloc(size)) == NULL) {
      return -ENOMEM;
   }
   reg->userspace_addr = (typeof(reg->userspace_addr))ptr;
   reg->slot = idx;
   reg->guest_phys_addr = base;
   reg->memory_size = size;
   reg->flags = 0;
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      km_page_free(ptr, size);
      return -errno;
   }
   set_pml4_hierarchy(reg, upper_va);
   return 0;
}

static void km_free_region(int idx, int upper_va)
{
   kvm_mem_reg_t* reg = &machine.vm_mem_regs[idx];
   uint64_t size = reg->memory_size;

   assert(size != 0);
   clear_pml4_hierarchy(reg, upper_va);
   reg->memory_size = 0;
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      err(1, "KVM: failed to unplug memory region %d", idx);
   }
   km_page_free((void*)reg->userspace_addr, size);
   reg->userspace_addr = 0;
   reg->guest_phys_addr = 0;
}

/*
 * brk() call implementation.
 *
 * Move machine.brk up or down, adding or removing memory regions as required.
 */
km_gva_t km_mem_brk(km_gva_t brk)
{
   int idx;
   uint64_t size;
   int ret;
   int error = 0;

   if (brk == 0) {
      return machine.brk;
   }
   if (brk > GUEST_MEM_START_VA + GUEST_MEM_ZONE_SIZE_VA) {
      return -ENOMEM;
   }
   km_mem_lock();
   km_gva_t ceiling = gva_to_gpa(machine.tbrk);   // ceiling for PA addresses avail
   if (brk > ceiling) {
      km_mem_unlock();
      return -ENOMEM;
   }

   idx = gva_to_memreg_idx(machine.brk);
   for (km_gva_t m_brk = MIN(brk, memreg_top(idx)); m_brk < brk; m_brk = MIN(brk, memreg_top(idx))) {
      /* Not enough room in the allocated memreg, allocate and add new ones */
      idx++;
      if ((size = memreg_size(idx)) > ceiling - memreg_base(idx) ||   // conflict with tbrk
          (ret = km_alloc_region(idx, size, 0)) != 0) {               // OOM
         // Config the loop below to free up just allocated regions
         idx--;
         brk = machine.brk;
         error = ENOMEM;
         break;
      }
   }
   for (; idx > gva_to_memreg_idx(brk); idx--) {
      /* brk moved down and left one or more memory regions. Remove and free */
      km_free_region(idx, 0);
   }
   machine.brk = brk;
   km_mem_unlock();
   return error == 0 ? brk : -error;
}

/*
 * Extends tbrk down in the upper virtual address (VA) space, mirroring behavior of km_mem_brk which
 * extends allocated space up in the bottom VA.
 * Note that gva in the upper VA are shifted up GUEST_VA_OFFSET from related gpa.
 */
km_gva_t km_mem_tbrk(km_gva_t tbrk)
{
   int idx;
   int ret;
   uint64_t size;
   int error = 0;

   if (tbrk == 0) {
      return machine.tbrk;
   }
   if (tbrk < GUEST_MEM_TOP_VA - GUEST_MEM_ZONE_SIZE_VA) {
      return -ENOMEM;
   }
   km_mem_lock();
   km_gva_t floor = gva_to_gpa(machine.brk);   // bottom for PA addresses avail
   if (gva_to_gpa(tbrk) <= floor) {   // More checks will follow, but this case is certainly ENOMEM
      km_mem_unlock();
      return -ENOMEM;
   }

   idx = gva_to_memreg_idx(machine.tbrk);
   for (km_gva_t m_brk = MAX(tbrk, gpa_to_upper_gva(memreg_base(idx))); m_brk > tbrk;
        m_brk = MAX(tbrk, gpa_to_upper_gva(memreg_base(idx)))) {
      /* Not enough room in the allocated memreg, allocate and add new ones */
      idx--;
      if ((size = memreg_size(idx)) > memreg_top(idx) - floor ||   // conflict with brk
          (ret = km_alloc_region(idx, size, 1)) != 0) {            // OOM
         // Config the loop below to free up  just allocated regions
         idx++;
         tbrk = machine.tbrk;
         error = ENOMEM;
         break;
      }
   }
   for (; idx < gva_to_memreg_idx(tbrk); idx++) {
      /* tbrk moved up and left one or more memory regions. Remove and free */
      km_free_region(idx, 1);
   }
   machine.tbrk = tbrk;
   km_mem_unlock();
   return error == 0 ? tbrk : -error;
}
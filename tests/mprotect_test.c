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
 * Test reaching out above brk to see if mprotect catches it
 * Also tests that mprotect/munmap/mmap combination sets proper mprotect-ed areas
 *
 * Note that mmap_test.c is the test coverign mmap() protection flag test, as well as basic
 * protection for munmapped areas, so here we mainly focus on brk() and mprotect() call
 */
#define _GNU_SOURCE /* See feature_test_macros(7) */
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include "syscall.h"

#include "mmap_test.h"

// check that write() syscall is propertly handing wrong address, and that reaching above brk() fails
TEST brk_test()
{
   char* ptr_above_brk = (char*)roundup((unsigned long)sbrk(0), 0x1000);
   int ret;

   if (write(1, ptr_above_brk, 1024) >= 0 || errno != EFAULT) {
      FAILm("Write above brk() should fail with EFAULT");
   }

   signal(SIGSEGV, sig_handler);
   if ((ret = sigsetjmp(jbuf, 1)) == 0) {
      strcpy((char*)ptr_above_brk, "writing above brk area");
      FAILm("Write successful and should be not");   // return
   }

   signal(SIGSEGV, SIG_DFL);
   ASSERT_EQm("Did not get expected signal", ret, SIGSEGV);
   if (fail == 1) {
      FAILm("signal handler reported a failure");
   }
   PASSm("Handled SIGSEGV above brk() successfully...\n");
}

// get mmap(PROT_NONE), cut sections with different protections and check read/write there
TEST simple_test()   // TODO
{
   static mmap_test_t mprotect_tests[] = {
       {__LINE__, "1. Large Empty mmap", TYPE_MMAP, 0, 64 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "2. mprotect PROT_READ", TYPE_MPROTECT, 8 * MIB, 10 * MIB, PROT_READ, flags, OK},
       {__LINE__, "2a.should fail to write", TYPE_WRITE, 8 * MIB, 1 * MIB, '2', 0, SIGSEGV},
       {__LINE__, "2a.OK to read", TYPE_READ, 9 * MIB, 1 * MIB, 0, 0, OK},
       {__LINE__, "3. mprotect PROT_WRITE", TYPE_MPROTECT, 20 * MIB, 10 * MIB, PROT_WRITE, flags, OK},
       {__LINE__, "3a.OK to write", TYPE_WRITE, 21 * MIB, 1 * MIB, '3', 0, OK},
       {__LINE__, "3a. OK to read", TYPE_READ, 23 * MIB, 2 * MIB, 0, 0, OK},
       {__LINE__, "3a. OK to read", TYPE_READ, 23 * MIB, 2 * MIB, 0, 0, OK},
       {__LINE__,
        "4. mprotect large READ|WRITE",
        TYPE_MPROTECT,
        6 * MIB,
        32 * MIB,
        PROT_READ | PROT_WRITE,
        flags,
        OK},
       {__LINE__, "4a.OK to write", TYPE_WRITE, 8 * MIB, 1 * MIB, '4', 0, OK},
       {__LINE__, "4a.OK to read", TYPE_READ, 16 * MIB, 2 * MIB, 0, 0, OK},
       {__LINE__, "5.cleanup unmap", TYPE_MUNMAP, 0, 64 * MIB, PROT_NONE, flags, OK},

       // test mprotect merge
       {__LINE__, "6. Large Empty mmap", TYPE_MMAP, 0, 2 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "6a. mprotect PROT_READ", TYPE_MPROTECT, 1 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {__LINE__, "6b. mprotect PROT_READ", TYPE_MPROTECT, 0 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {__LINE__, "7.cleanup unmap", TYPE_MUNMAP, 0, 2 * MIB, PROT_NONE, flags, OK},

       {__LINE__, NULL},
   };

   printf("Running %s\n", __FUNCTION__);
   CHECK_CALL(mmap_test(mprotect_tests));
   PASS();
}

// test tricky combinations of mmap/munmap/mprotect zones.
TEST complex_test()
{
   static mmap_test_t mprotect_tests[] = {
       {__LINE__, "1. Large Empty mmap", TYPE_MMAP, 0, 64 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "1a.Poke a hole - unmap", TYPE_MUNMAP, 1 * MIB, 1 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "1b.Should fail - inside", TYPE_MPROTECT, 1 * MIB, 10 * MIB, PROT_NONE, flags, ENOMEM},
       {__LINE__, "1c.Should fail  - aligned", TYPE_MPROTECT, 1 * MIB, 10 * MIB, PROT_NONE, flags, ENOMEM},
       {__LINE__, "1d.Unmap more from 0", TYPE_MUNMAP, 0, 1 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "1e. Seal the map Empty mmap", TYPE_MMAP, 0, 2 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "2. Large mprotect", TYPE_MPROTECT, 8 * MIB, 22 * MIB, PROT_READ | PROT_WRITE, flags, OK},
       {__LINE__, "3. Hole1 mprotect", TYPE_MPROTECT, 16 * MIB, 1 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "4. Hole2 mprotect", TYPE_MPROTECT, 20 * MIB, 1 * MIB, PROT_NONE, flags, OK},
       {__LINE__,
        "5. Glue holes mprotect",
        TYPE_MPROTECT,
        1 * MIB,
        63 * MIB,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        flags,
        OK},
       {__LINE__, "6. Hole unmap", TYPE_MUNMAP, 12 * MIB, 14 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "6a.Should fail on gap1", TYPE_MPROTECT, 10 * MIB, 10 * MIB, PROT_NONE, flags, ENOMEM},
       {__LINE__, "6b.Should fail of gap2", TYPE_MPROTECT, 12 * MIB, 3 * MIB, PROT_NONE, flags, ENOMEM},
       {__LINE__, "7. Hole3 mprotect  ", TYPE_MPROTECT, 10 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {__LINE__, "8. Unmap over unmap", TYPE_MUNMAP, 10 * MIB, 14 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "9. Should fail on gaps", TYPE_MPROTECT, 8 * MIB, 16 * MIB, PROT_READ, flags, ENOMEM},
       {__LINE__, "10.Gaps1", TYPE_MPROTECT, 0, 2 * GIB, PROT_NONE, flags, ENOMEM},
       {__LINE__, "11.Gaps2", TYPE_MPROTECT, 10 * MIB, 2 * MIB, PROT_NONE, flags, ENOMEM},
       {__LINE__, "12.fill in mmap", TYPE_MMAP, 0, 16 * MIB, PROT_NONE, flags, OK},   // should fill
                                                                                      // in the hole
       {__LINE__, "13.fill in mprotect", TYPE_MMAP, 0, 64 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "14.cleanup unmap", TYPE_MUNMAP, 0, 64 * MIB, PROT_NONE, flags, OK},

       {__LINE__, NULL},
   };

   printf("Running %s\n", __FUNCTION__);
   CHECK_CALL(mmap_test(mprotect_tests));
   PASS();
}

// helper to test glue
TEST concat_test()
{
   SKIPm("Concat is not needed for busy maps");
   static mmap_test_t mprotect_tests[] = {
       {__LINE__, "1. mmap", TYPE_MMAP, 0, 1 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "2. mmap", TYPE_MMAP, 0, 2 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "3. mmap", TYPE_MMAP, 0, 3 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "4. mmap", TYPE_MMAP, 0, 4 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "5. mmap", TYPE_MMAP, 0, 5 * MIB, PROT_NONE, flags, OK},
       {__LINE__, "6. mprotect", TYPE_MPROTECT, 1 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {__LINE__, "7. mprotect", TYPE_MPROTECT, 2 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {__LINE__, "8. mprotect", TYPE_MPROTECT, 3 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {__LINE__, "9. mprotect", TYPE_MPROTECT, 4 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {__LINE__, "10.mprotect", TYPE_MPROTECT, 5 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {__LINE__, "11.mprotect", TYPE_MPROTECT, 6 * MIB, 1 * MIB, PROT_READ, flags, OK},

       // TODO: automate checking for the mmaps concatenation. For now check with `print_tailq
       // &machine.mmaps.busy ` in gdb
       {__LINE__, "12.cleanup unmap", TYPE_MUNMAP, 0, 15 * MIB, PROT_NONE, flags, OK},

       {__LINE__, NULL},
   };

   printf("Running %s\n", __FUNCTION__);
   CHECK_CALL(mmap_test(mprotect_tests));
   PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();
   greatest_set_verbosity(1);

   RUN_TEST(brk_test);
   RUN_TEST(simple_test);
   RUN_TEST(concat_test);
   RUN_TEST(complex_test);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}

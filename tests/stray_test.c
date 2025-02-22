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
 * This program generates exceptions in guest in order to test guest coredumps
 * and other processing.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "km_hcalls.h"
#include "syscall.h"

#define MIB (1024ul * 1024ul)

char* cmdname = "?";

// make the data and bss segments large to force a region boundary crossing in coredump.
int bigarray_data[10000000] = {1, 2, 3};
int bigarray_bss[10000000];

pthread_mutex_t mt = PTHREAD_MUTEX_INITIALIZER;

int stray_reference(int optind, int argc, char* argv[]);
int div0(int optind, int argc, char* argv[]);
int undefined_op(int optind, int argc, char* argv[]);
int protection_test(int optind, int argc, char* argv[]);
int abort_test(int optind, int argc, char* argv[]);
int quit_test(int optind, int argc, char* argv[]);
int term_test(int optind, int argc, char* argv[]);
int signal_abort_test(int optind, int argc, char* argv[]);
int block_segv_test(int optind, int argc, char* argv[]);
int sigpipe_test(int optind, int argc, char* argv[]);
int hc_test(int optind, int argc, char* argv[]);
int hc_badarg_test(int optind, int argc, char* argv[]);
int close_test(int optind, int argc, char* argv[]);
int thread_test(int optind, int argc, char* argv[]);
int syscall_test(int optind, int argc, char* argv[]);
int trunc_mmap_test(int optind, int argc, char* argv[]);
int hc_overhead_test(int optind, int argc, char* argv[]);
int redir_std_test(int optind, int argc, char* argv[]);
int halt_test(int optind, int argc, char* argv[]);

struct stray_op {
   char* op;                                          // Operation name on command line
   int (*func)(int optind, int argc, char* argv[]);   // operation function
   char* description;
} operations[] = {
    {.op = "stray",
     .func = stray_reference,
     .description = "- make a stray memory reference that will generate a SIGSEGV."},
    {.op = "div0", .func = div0, .description = "- divide by 0 and generate a SIGFPE."},
    {.op = "ud", .func = undefined_op, .description = "- undefined instruction, generate a SIGILL."},
    {.op = "prot",
     .func = protection_test,
     .description = "- try to write to instruction memory. Generate a SIGSEGV."},
    {.op = "abort", .func = abort_test, .description = "- Call abort(2). Generate a SIGABRT."},
    {.op = "quit", .func = quit_test, .description = "- Generate a SIGQUIT."},
    {.op = "term", .func = term_test, .description = "- Generate a SIGTERM."},
    {.op = "signal", .func = signal_abort_test, .description = "- abort(2) from a signal handler."},
    {.op = "block-segv", .func = block_segv_test, .description = "- block SIGSEGV and them generate it."},
    {.op = "sigpipe", .func = sigpipe_test, .description = "- ignore SIGPIPE and then generate it."},
    {.op = "hc",
     .func = hc_test,
     .description = "<call> - make hypercall with number <call>. exit 0 means success."},
    {.op = "hc-badarg",
     .func = hc_badarg_test,
     .description = "<call> - make hypercall with number <call> and a bad argument."},
    {.op = "close", .func = close_test, .description = "<fd> - close file descriptor fd"},
    {.op = "thread", .func = thread_test, .description = "test pthread create/join"},
    {.op = "syscall", .func = syscall_test, .description = "Generate a SYSCALL instruction"},
    {.op = "trunc_mmap", .func = trunc_mmap_test, .description = "Truncate an mmaped file and crash"},
    {.op = "hc_overhead", .func = hc_overhead_test, .description = "Test overhead of Syscalls"},
    {.op = "redir_std", .func = redir_std_test, .description = "Test redisrect std err"},
    {.op = "halt", .func = halt_test, .description = "Execute HLT instruction"},
    {.op = NULL, .func = NULL, .description = NULL},
};

void usage()
{
   fprintf(stderr, "usage: %s <operation>\n", cmdname);
   fprintf(stderr, "operations:\n");

   struct stray_op* sop = operations;
   while (sop->op != NULL) {
      fprintf(stderr, " %s %s\n", sop->op, sop->description);
      sop++;
   }
}

// Generate a X86 #PF exception (page fault)
int stray_reference(int optind, int argc, char* argv[])
{
   char* ptr = (char*)-16;
   *ptr = 'a';
   return 0;
}

// Generate a X86 #DE exception divide error)
int div0(int optind, int argc, char* argv[])
{
   asm volatile("xor %rcx, %rcx\n\t"
                "div %rcx");
   return 0;
}

// Generate a X86 #UD excpetion (invalid opcode)
int undefined_op(int optind, int argc, char* argv[])
{
   asm volatile("ud2");
   return 0;
}

void write_text(void* ptr)
{
   *((char*)ptr) = 'a';
}

// Generate a X86 #GP exception (general protection)
int protection_test(int optind, int argc, char* argv[])
{
   write_text(usage);
   return 0;
}

int abort_test(int optind, int argc, char* argv[])
{
   abort();
   return 0;
}

int quit_test(int optind, int argc, char* argv[])
{
   kill(0, SIGQUIT);
   return 0;
}

int term_test(int optind, int argc, char* argv[])
{
   kill(0, SIGTERM);
   return 0;
}

void signal_abort_handler(int sig)
{
   abort();
}

int signal_abort_test(int optind, int argc, char* argv[])
{
   signal(SIGUSR1, signal_abort_handler);
   kill(0, SIGUSR1);
   return 0;
}

// Block SIGSEGV and then generate one. Should drop core drop a core.
int block_segv_test(int optind, int argc, char* argv[])
{
   sigset_t mask;
   sigemptyset(&mask);
   sigaddset(&mask, SIGSEGV);
   if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
      err(1, "sigprocmask failed");
   }
   stray_reference(optind, argc, argv);
   return 0;
}

// Ignore SIGPIP and generate a one. Should be ignored
int sigpipe_test(int optind, int argc, char* argv[])
{
   int pfd[2];
   if (pipe(pfd) < 0) {
      return errno;
   }
   close(pfd[0]);

   char* buf = "hello world";
   size_t buflen = strlen(buf) + 1;

   signal(SIGPIPE, SIG_IGN);

   // genreate sigpipe event (should be ignored)
   write(pfd[1], buf, buflen);
   return 0;
}

// Generate a hypercall.
int hc_test(int optind, int optarg, char* argv[])
{
   char* ep = NULL;
   int callid = strtol(argv[optind], &ep, 0);
   if (ep == NULL || *ep != '\0') {
      fprintf(stderr, "callid '%s' is not a number", argv[optind]);
      usage();
      return 100;
   }
   if (syscall(callid, 0) < 0) {
      return errno;
   }
   return 0;
}

// Generate a hypercall with a known bad argument.
int hc_badarg_test(int optind, int optarg, char* argv[])
{
   char* ep = NULL;
   int callid = strtol(argv[optind], &ep, 0);
   if (ep == NULL || *ep != '\0') {
      fprintf(stderr, "callid '%s' is not a number", argv[optind]);
      usage();
      return 100;
   }
   km_hcall(callid, (km_hc_args_t*)-1LL);
   return 0;
}

int close_test(int optind, int optarg, char* argv[])
{
   char* ep = NULL;
   int fd = strtol(argv[optind], &ep, 0);
   if (ep == NULL || *ep != '\0') {
      fprintf(stderr, "fd '%s' is not a number", argv[optind]);
      usage();
      return 100;
   }
   if (close(fd) < 0) {
      return errno;
   }
   return 0;
}

void* thread_main2(void* arg)
{
   fprintf(stderr, "%s\n", __FUNCTION__);
   return NULL;
}

int thread_test(int optind, int optarg, char* argv[])
{
   pthread_t thr;
   pthread_create(&thr, NULL, thread_main2, NULL);
   void* ret;
   pthread_join(thr, &ret);
   return 0;
}

/*
 * Print using SYSCALL instruction.
 * Make sure that a real syscall instruction in the payload gets mapped to
 * a hypercall.
 */
int syscall_test(int optind, int optarg, char* argv[])
{
   int syscall_num = 1;   // write()
   int fd = 1;            // stdout
   char* msg = "Hello from SYSCALL\n";
   size_t msgsz = strlen(msg);
   int rc;

   asm volatile("\tsyscall" : "=a"(rc) : "a"(syscall_num), "D"(fd), "S"(msg), "d"(msgsz));
   if (rc < 0) {
      fprintf(stderr, "write to stdout failed, %s\n", strerror(errno));
      return 1;
   }
   if (rc != msgsz) {
      fprintf(stderr, "write length wrong, expected %ld, got %d\n", msgsz, rc);
      return 1;
   }
   return 0;
}

/*
 * Recreate Issue #459 - Short write in coredump.
 * This occurs when a file is truncated while mmap'ed.
 */
int trunc_mmap_test(int optind, int argc, char* argv[])
{
   // create a  2 page file.
   int fd = open(argv[optind], O_RDWR | O_CREAT, 0666);
   if (fd < 0) {
      perror("open");
      return 1;
   }
   if (ftruncate(fd, 8192) < 0) {
      perror("ftruncate");
      return 1;
   }

   // mmap it
   void* ptr = mmap(NULL, 8192, PROT_READ, MAP_SHARED, fd, 0);
   if (ptr == MAP_FAILED) {
      perror("mmap");
      return 1;
   }

   // truncate file and drop core.
   if (ftruncate(fd, 1024) < 0) {
      perror("ftruncate2");
      return 1;
   }
   abort();

   return 0;
}

int hc_overhead_test(int optind, int argc, char* argv[])
{
   int do_syscall = 0;
   int uid;

   if (strcmp(argv[optind], "-s") == 0) {
      do_syscall = 1;
      optind++;
   }
   int loops = atoi(argv[optind]);

   for (int i = 0; i < loops; i++) {
      if (do_syscall) {
         int syscall_num = 102;   // getuid
         asm volatile("\tsyscall" : "=a"(uid) : "a"(syscall_num));
      } else {
         uid = getuid();
      }
      (void)uid;
   }
   return 0;
}

int redir_std_test(int optind, int argc, char* argv[])
{
   int fd = open("/dev/null", O_RDWR);
   if (fd < 0) {
      perror("open /dev/null");
      return 1;
   }
   int sav_stdout = dup(2);
   if (sav_stdout < 0) {
      perror("dup stdout - sav");
      return 1;
   }
   fprintf(stdout, "Before redir\n");
   fflush(stdout);
   if (dup2(fd, 1) < 0) {
      perror("dup3 stdout - redir");
      return 1;
   }
   fprintf(stdout, "From redir\n");
   fflush(stdout);
   if (dup3(sav_stdout, 1, 0) < 0) {
      perror("dup3 stdout - restore");
      return 1;
   }
   fprintf(stdout, "After restore\n");
   return 0;
}

int halt_test(int optind, int argc, char* argv[])
{
   asm("hlt");
   return 0;
}

// A thread to have laying around. Make the coredumps more interesting.
void* thread_main(void* arg)
{
   pthread_mutex_lock(&mt);
   return NULL;
}

int main(int argc, char** argv)
{
   extern int optind;
   int c;
   char* op;

   cmdname = argv[0];
   while ((c = getopt(argc, argv, "h")) != -1) {
      switch (c) {
         case 'h':
            usage();
            return 0;
         default:
            fprintf(stderr, "unrecognized option %c\n", c);
            usage();
            return 1;
      }
   }
   if (optind >= argc) {
      usage();
      return 1;
   }
   op = argv[optind];
   optind++;

   pthread_t thr;
   pthread_mutex_lock(&mt);
   if (pthread_create(&thr, NULL, thread_main, NULL) != 0) {
      err(1, "pthread_create failed");
   }

   /*
    * Map a fairly large region to force the coredump to do a large write.
    */
   if (mmap(0, 10 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) {
      errx(1, "large mmap failed");
   }

   /*
    * Make sure we have a couple of non-readable maps
    */
   if (mmap(0, 200 * MIB, PROT_NONE, MAP_SHARED | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) {
      errx(1, "large PROT_NONE mmap ");
   }

   if (mmap(0, 10 * MIB, PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) {
      errx(1, "large PROT_NONE mmap ");
   }

   /*
    * malloc some space
    */
   int msz = MIB / 16;   // 64K
   void* ptr = malloc(msz);
   memset(ptr, 'a', msz);
   ;

   struct stray_op* nop = operations;
   while (nop->op != NULL) {
      if (strcmp(op, nop->op) == 0) {
         return nop->func(optind, argc, argv);
      }
      nop++;
   }

   fprintf(stderr, "Unrecognized operation '%s'\n", op);
   usage();
   return 1;
}

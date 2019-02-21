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
 */

#undef _FORTIFY_SOURCE
#define _FORTIFY_SOURCE 0

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "syscall.h"

volatile int var;
pthread_mutex_t mt = PTHREAD_MUTEX_INITIALIZER;
const long step = 1l << 24;
const long run_count = 1l << 26;

void* run(void* arg)
{
   int x, y;
   struct timespec tp, tp0;
   char* msg = arg == NULL ? " child " : "parent ";

   clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp0);

   for (long i = 0; i < run_count; i++) {
      pthread_mutex_lock(&mt);
      if (arg == NULL) {
         x = ++var;
      } else {
         x = --var;
      }
      y = var;
      pthread_mutex_unlock(&mt);
      if (x != y) {
         printf("%sx: %d - %d\n", msg, x, y);
      }
      if ((i & (step - 1)) == 0) {
         clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp);
         printf("%s: %ld, %ld ms\n",
                msg,
                i / step,
                tp.tv_nsec > tp0.tv_nsec
                    ? (tp.tv_sec - tp0.tv_sec) * 1000 + (tp.tv_nsec - tp0.tv_nsec) / 1000000
                    : (tp.tv_sec - tp0.tv_sec) * 1000 +
                          (1000000000 - tp0.tv_nsec + tp.tv_nsec) / 1000000);
         tp0 = tp;
      }
   }
   return NULL;
}

int main()
{
   pthread_t pt;

   pthread_create(&pt, NULL, run, NULL);
   run((void*)1);
   exit(0);
}

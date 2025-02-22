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

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

static jmp_buf buf;

static void jump(void)
{
   longjmp(buf, 17);
}

void func(void)
{
   jump();
}

static const char msg[] = "Hello,";

int main(int argc, char** argv)
{
   char* msg2 = "world";
   int pass;

   pass = setjmp(buf);
   printf("=== %d === %s %s\n", pass, msg, msg2);
   for (int i = 0; i < argc; i++) {
      printf("%s argv[%d] = '%s'\n", msg, i, argv[i]);
   }
   if (pass) {
      exit(0);
   }
   func();
}

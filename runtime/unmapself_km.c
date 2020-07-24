#include <stdlib.h>
#include "km_hcalls.h"
#include "syscall.h"

_Noreturn void __unmapself(void* base, size_t size)
{
   while (1) {
      syscall(HC_unmapself, base, size);
   }
}

#include "bench_module.h"
#ifndef KERNEL_SYMBOL
#error KERNEL_SYMBOL must identify the cartridge kernel
#endif
extern void KERNEL_SYMBOL(int16_t *, int16_t *, unsigned char);
static int setup(size_t n, int inverse) {
  unsigned long vlenb;
  __asm__ volatile("csrr %0, vlenb" : "=r"(vlenb));
  return inverse == MODULE_INVERSE && n == MODULE_POINTS && vlenb == 32 ? 0 : -1;
}
const bench_module selected_module = {
  MODULE_NAME, MODULE_CONTRACT, setup, KERNEL_SYMBOL, MODULE_APPROXIMATE
};

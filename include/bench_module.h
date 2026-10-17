#ifndef BENCH_MODULE_H
#define BENCH_MODULE_H
#include <stddef.h>
#include <stdint.h>

/* Every cartridge uses the same interleaved signed-int16 complex ABI.
 * setup performs configuration outside timing; run contains only the module.
 * A setup failure means unsupported transform/size, never silent fallback. */
typedef struct {
  const char *name;
  const char *contract;
  int (*setup)(size_t points, int inverse);
  void (*run)(int16_t *input, int16_t *output, unsigned char scale);
  int approximate;
} bench_module;
extern const bench_module selected_module;
extern const bench_module reference_module;
#endif

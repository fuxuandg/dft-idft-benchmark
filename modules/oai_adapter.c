#include "bench_module.h"
#include "tools_defs.h"
extern int dfts_autoinit(void);
extern void dft_implementation(uint8_t, int16_t *, int16_t *, unsigned char);
extern void idft_implementation(uint8_t, int16_t *, int16_t *, unsigned char);
static uint8_t index_selected;
static int inverse_selected;
static int tables_ready;
static int setup(size_t n, int inverse) {
  int index = -1;
  if (inverse) {
    switch (n) {
#define MAP_IDFT(S) case S: index = IDFT_##S; break;
      FOREACH_IDFTSZ(MAP_IDFT)
#undef MAP_IDFT
      default: return -1;
    }
  } else {
    /* dft12 has an upstream special ABI (8 parallel transforms), so it
     * is intentionally not exposed through the single-transform contract. */
    if (n == 12) return -1;
    switch (n) {
#define MAP_DFT(S) case S: index = DFT_##S; break;
      FOREACH_DFTSZ(MAP_DFT)
#undef MAP_DFT
      default: return -1;
    }
  }
  if (n != 64 && !tables_ready) {
    if (dfts_autoinit() != 0) return -1;
    tables_ready = 1;
  }
  index_selected = (uint8_t)index;
  inverse_selected = inverse;
  return 0;
}
static void run(int16_t *x, int16_t *y, unsigned char scale) {
  if (inverse_selected) idft_implementation(index_selected, x, y, scale);
  else dft_implementation(index_selected, x, y, scale);
}
const bench_module reference_module = {
  "oai-standard-simde", "OAI fixed-point baseline; unmodified SIMDe 0.8.4; SIMDE_NO_NATIVE", setup, run, 0
};
#ifdef SELECT_OAI
const bench_module selected_module = {
  "oai-standard-simde", "OAI fixed-point baseline; unmodified SIMDe 0.8.4; SIMDE_NO_NATIVE", setup, run, 0
};
#endif

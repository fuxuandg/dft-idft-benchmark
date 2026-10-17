#include <stdint.h>
#include <stdio.h>
#include <string.h>

void idft64_rvv(int16_t *x, int16_t *y, unsigned char scale);
void idft64_rvv_orig(int16_t *x, int16_t *y, unsigned char scale);

static uint32_t rng_state = 0x12345678u;

static uint32_t xorshift32(void) {
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state = x;
  return x;
}

static int compare_case(int16_t *x, unsigned char scale, int case_no) {
  static int16_t yo_space[129] __attribute__((aligned(64)));
  static int16_t yn_space[129] __attribute__((aligned(64)));
  int16_t *yo = yo_space + (case_no & 1);
  int16_t *yn = yn_space + (case_no & 1);
  memset(yo_space, 0x5a, sizeof(yo_space));
  memset(yn_space, 0xa5, sizeof(yn_space));
  idft64_rvv_orig(x, yo, scale);
  idft64_rvv(x, yn, scale);
  for (int lane = 0; lane < 128; lane++) {
    if (yo[lane] != yn[lane]) {
      fprintf(stderr,
              "FAIL case=%d scale=%u lane=%d orig=%d opt=%d input_offset=%d output_offset=%d\n",
              case_no, scale, lane, yo[lane], yn[lane], case_no & 1,
              case_no & 1);
      return 1;
    }
  }
  return 0;
}

int main(void) {
  static int16_t x_space[129] __attribute__((aligned(64)));
  static const int16_t edge[] = {-32768, -32767, -1, 0, 1, 32766, 32767};
  static const unsigned char scales[] = {0, 1, 2, 255};
  int cases = 0;

  for (unsigned s = 0; s < sizeof(scales); s++) {
    for (int pattern = 0; pattern < 12; pattern++) {
      int16_t *x = x_space + (cases & 1);
      for (int lane = 0; lane < 128; lane++) {
        switch (pattern) {
          case 0: x[lane] = 0; break;
          case 1: x[lane] = 32767; break;
          case 2: x[lane] = -32768; break;
          case 3: x[lane] = (lane & 1) ? -32768 : 32767; break;
          case 4: x[lane] = edge[lane % 7]; break;
          case 5: x[lane] = (lane == 0) ? 32767 : 0; break;
          case 6: x[lane] = (lane == 1) ? -32768 : 0; break;
          case 7: x[lane] = (int16_t)(lane * 509 - 32768); break;
          case 8: x[lane] = (int16_t)((lane & 3) * 21845 - 32768); break;
          case 9: x[lane] = (lane & 1) ? 500 : -500; break;
          case 10: x[lane] = (lane & 1) ? 4095 : -4095; break;
          default: x[lane] = (int16_t)xorshift32(); break;
        }
      }
      if (compare_case(x, scales[s], cases)) return 1;
      cases++;
    }

    for (int trial = 0; trial < 100000; trial++) {
      int16_t *x = x_space + (cases & 1);
      for (int lane = 0; lane < 128; lane++) x[lane] = (int16_t)xorshift32();
      if (compare_case(x, scales[s], cases)) return 1;
      cases++;
    }
  }

  printf("PASS: %d cases x 128 lanes, scales={0,1,2,255}, aligned+unaligned\n", cases);
  return 0;
}

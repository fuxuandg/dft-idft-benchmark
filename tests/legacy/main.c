/* ============================================================================
 * main.c —— idft64_rvv 的独立自检 + 性能测试
 * ============================================================================
 *
 * 【怎么跑】
 *     make && ./idft64_rvv           自检 + 性能
 *     make && ./idft64_rvv --check   只自检（快，秒出）
 *
 * 【它验证什么】
 *   1. 正确性：把 3 个内置用例喂给 idft64_rvv()，输出与 **OAI 原版在 x86 AVX2
 *      上产生的参考值**逐位比对（bit-exact）。差异必须是 0 个字。
 *   2. 性能：批内计时 10000 次，给出每次的纳秒数与 TSC 周期数。
 *
 * 【为什么正确性用"逐位"而不是"差不多"】
 *   OAI 的 DFT 全程是定点整数运算（饱和加减、算术移位、整数乘加），
 *   没有浮点误差的模糊地带，所以只要移植正确，输出**必须**一模一样。
 *   逐位比对是能想得到的最强判据，任何一位错都藏不住。
 * ==========================================================================*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "idft64_rvv.h"
#include "golden.h"

/* ---------------------------------------------------------------------------
 * 计时器：RISC-V 的 time CSR（rdtime）
 *
 * 注意它是**定频**计数器（这块板子是 24 MHz），跟 CPU 实际主频无关，
 * 所以它量出来的是真实墙钟时间。频率在下面运行时标定，不写死。
 *
 * 也别指望单次测量的分辨率：24 MHz 意味着 1 格 = 41.7 ns，
 * 而一次 64 点变换只有约 600 ns（≈14 格）——单次测误差可能到 7%。
 * 所以必须**批内计时**：一次计时覆盖 10000 次调用，分辨率随之提高 10000 倍。
 * -------------------------------------------------------------------------*/
static inline uint64_t rdtime_now(void) {
  uint64_t v;
  __asm__ __volatile__("rdtime %0" : "=r"(v));
  return v;
}
static double wall_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* 运行时标定 rdtime 频率（0.2 秒窗口，误差可忽略）*/
static double calibrate_ticks_per_us(void) {
  uint64_t t0 = rdtime_now();
  double w0 = wall_now();
  double w;
  do { w = wall_now(); } while (w - w0 < 0.2);
  uint64_t t1 = rdtime_now();
  double w1 = wall_now();
  return (double)(t1 - t0) / ((w1 - w0) * 1e6);
}

/* ---------------------------------------------------------------------------
 * 正确性自检：逐位比对
 * -------------------------------------------------------------------------*/
static int selfcheck(void) {
  printf("=== 正确性自检：与 OAI 原版输出逐位比对 ===\n");
  printf("    参考来源: OAI oai_dfts.c 在 x86(AVX2) 上的输出\n");
  printf("    用例数  : %d，每例比对 128 个 int16 (64 个复数)\n\n", NGOLDEN);

  int cases_bad = 0;

  for (int c = 0; c < NGOLDEN; c++) {
    /* 静态 + 64 字节对齐：RVV 的 vle16 虽然是非对齐安全的，但对齐访问最快；
     * 另外这也是原版 OAI 输出缓冲的对齐要求（32 字节）。 */
    static int16_t x[128] __attribute__((aligned(64)));
    static int16_t y[128] __attribute__((aligned(64)));

    memcpy(x, GOLDEN[c].x, sizeof(x));
    memset(y, 0, sizeof(y));

    idft64_rvv(x, y, 1);

    int bad = 0, first = -1, worst = 0;
    for (int i = 0; i < 128; i++) {
      int d = (int)y[i] - (int)GOLDEN[c].y[i];
      if (d) {
        bad++;
        if (first < 0) first = i;
        if (abs(d) > abs(worst)) worst = d;
      }
    }

    if (bad == 0) {
      printf("  [PASS] %-30s 128/128 逐位一致\n", GOLDEN[c].name);
    } else {
      printf("  [FAIL] %-30s %d/128 不一致  (max|diff|=%d, 首个在 #%d)\n",
             GOLDEN[c].name, bad, abs(worst), first);
      /* 打印前几个不一致点，便于定位 */
      int shown = 0;
      for (int i = 0; i < 128 && shown < 6; i++) {
        int d = (int)y[i] - (int)GOLDEN[c].y[i];
        if (d) {
          printf("          y[%3d] = %6d   期望 %6d   差 %+d\n",
                 i, y[i], GOLDEN[c].y[i], d);
          shown++;
        }
      }
      cases_bad++;
    }
  }

  printf("\n  ==> %d/%d 用例通过", NGOLDEN - cases_bad, NGOLDEN);
  if (cases_bad == 0) printf("   ✅ 全部逐位一致\n\n");
  else                printf("   ❌ 有 %d 个用例不一致\n\n", cases_bad);

  return cases_bad;
}

/* ---------------------------------------------------------------------------
 * 性能：批内计时
 * -------------------------------------------------------------------------*/
static void benchmark(int iters, int reps) {
  static int16_t x[128] __attribute__((aligned(64)));
  static int16_t y[128] __attribute__((aligned(64)));

  /* 用第一个 golden 用例的输入，保证测的是"典型数据"而不是全 0 */
  memcpy(x, GOLDEN[0].x, sizeof(x));

  double ticks_per_us = calibrate_ticks_per_us();

  printf("=== 性能（批内计时：1 对计时覆盖 %d 次调用）===\n", iters);
  printf("    计时器: rdtime @ %.3f MHz (1 格 = %.3f ns)\n",
         ticks_per_us, 1000.0 / ticks_per_us);
  printf("    锁核  : sched_setaffinity 未使用（本程序单线程跑在一个核上）\n\n");

  uint64_t best = ~0ULL;
  for (int r = 0; r < reps; r++) {
    uint64_t t0 = rdtime_now();
    for (int i = 0; i < iters; i++) {
      idft64_rvv(x, y, 1);
    }
    uint64_t t1 = rdtime_now();
    uint64_t d = t1 - t0;
    if (d < best) best = d;
    printf("    第 %2d 轮: %8llu 格 / %8d 次 = %8.2f 格/次 = %9.2f ns/次\n",
           r + 1, (unsigned long long)d, iters,
           (double)d / iters, (double)d / iters / ticks_per_us * 1000.0);
  }

  double ns = (double)best / iters / ticks_per_us * 1000.0;
  double cyc = ns * 2.2;   /* 这块板子(X100)钉死在 2.2 GHz */
  printf("\n    最优(取最小): %.2f ns/次   ≈ %.0f 个核周期 @2.2GHz\n", ns, cyc);
  printf("    （取最小是为了排除中断/调度干扰：最小值 = 硬件真实下限）\n\n");
}

int main(int argc, char **argv) {
  int only_check = (argc > 1 && !strcmp(argv[1], "--check"));

  printf("=======================================================================\n");
  printf(" idft64_rvv —— 纯手写 RVV 的 64 点 IDFT（自包含，零 OAI / 零 SIMDe）\n");
  printf("=======================================================================\n\n");

  int bad = selfcheck();

  if (!only_check) {
    benchmark(10000, 5);
  } else {
    printf("（--check 模式：跳过性能测试）\n\n");
  }

  printf("=======================================================================\n");
  printf(" 结论: %s\n", bad == 0 ? "正确性通过 —— 输出与 OAI 原版逐位一致"
                                 : "正确性失败 —— 见上面的差异列表");
  printf("=======================================================================\n");
  return bad ? 1 : 0;
}

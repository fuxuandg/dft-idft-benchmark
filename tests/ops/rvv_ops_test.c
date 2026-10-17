/* ============================================================================
 * rvv_ops_test.c —— 手写 RVV 算子库的"地基验收"
 *
 * 思路：**先验证积木，再盖房子**。
 * 对 rvv_ops.h 里每一个算子，用 SIMDe（AVX2 官方语义）当参考实现，
 * 同一组输入各算一遍，**逐位**比对。全部通过之后，才敢拿去组装 idft64。
 *
 * 这样做的好处：不用先把整个 idft64 写完才发现某个算子错了——
 * 那时错误会被淹没在几百行代码里，极难定位。
 *
 * 数据在 simde__m256i 与 vint16m1_t 之间靠**内存**传递（测试不关心性能，
 * 只关心语义等价）——这也顺带说明了为什么生产代码不能这么传（会有访存开销）。
 * ==========================================================================*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <riscv_vector.h>
#include <simde/x86/avx2.h>

#include "rvv_ops.h"

static int g_fail = 0;

static void check(const char *name, const int16_t *got, const int16_t *want, int n) {
  int bad = 0, first = -1;
  for (int i = 0; i < n; i++) {
    if (got[i] != want[i]) { bad++; if (first < 0) first = i; }
  }
  printf("  %-30s %s\n", name, bad ? "FAIL" : "OK");
  if (bad) {
    g_fail = 1;
    printf("      %d/%d differ, first @%d:\n", bad, n, first);
    for (int i = first; i < n && i < first + 4; i++)
      printf("        [%2d]  rvv=%7d   simde=%7d\n", i, got[i], want[i]);
  }
}

/* 固定伪随机（保证可复现） */
static uint32_t g_rng = 0x12345678u;
static uint32_t xr(void) { uint32_t x = g_rng; x ^= x<<13; x ^= x>>17; x ^= x<<5; return g_rng = x; }

/* 造两组测试数据：边界值（测饱和）+ 随机值 */
static void fill(int16_t *a, int16_t *b, int mode) {
  if (mode == 0) {           /* 边界：饱和/溢出 */
    static const int16_t edge[16] = {32767,-32768,0,-1,1,32766,-32767,16384,
                                     -16384,255,-255,32767,-32768,1,0,-1};
    memcpy(a, edge, 32); memcpy(b, edge, 32);
  } else {                    /* 随机 */
    for (int i = 0; i < 16; i++) { a[i] = (int16_t)(xr() & 0xFFFF); b[i] = (int16_t)(xr() & 0xFFFF); }
  }
}

int main(void) {
  size_t vl = RVV_CFG();
  printf("RVV 算子库验收（VL=%zu, VLEN=%zu bit）\n", vl, vl * 16);
  printf("每个算子都比对 SIMDe(AVX2) 的官方语义，逐位要求一致\n\n");

  for (int mode = 0; mode < 2; mode++) {
    printf("---- 数据模式 %s ----\n", mode == 0 ? "边界值(测饱和)" : "随机值");

    int16_t a[16], b[16], got[16], want[16];

    /* ---------- 算术 ---------- */
    fill(a, b, mode);
    { simde__m256i sa, sb, sr; memcpy(&sa,a,32); memcpy(&sb,b,32);
      sr = simde_mm256_adds_epi16(sa, sb); memcpy(want,&sr,32); }
    rvv_store16(got, rvv_adds_epi16(rvv_load16(a,vl), rvv_load16(b,vl), vl), vl);
    check("adds_epi16 (饱和加)", got, want, 16);

    fill(a, b, mode);
    { simde__m256i sa, sb, sr; memcpy(&sa,a,32); memcpy(&sb,b,32);
      sr = simde_mm256_subs_epi16(sa, sb); memcpy(want,&sr,32); }
    rvv_store16(got, rvv_subs_epi16(rvv_load16(a,vl), rvv_load16(b,vl), vl), vl);
    check("subs_epi16 (饱和减)", got, want, 16);

    fill(a, b, mode);
    { simde__m256i sa, sr; memcpy(&sa,a,32);
      sr = simde_mm256_srai_epi16(sa, 3); memcpy(want,&sr,32); }
    rvv_store16(got, rvv_srai_epi16(rvv_load16(a,vl), 3, vl), vl);
    check("srai_epi16 (算术右移 3)", got, want, 16);

    /* ---------- 符号 ---------- */
    fill(a, b, mode);
    { simde__m256i sa, sb, sr; memcpy(&sa,a,32); memcpy(&sb,b,32);
      sr = simde_mm256_sign_epi16(sa, sb); memcpy(want,&sr,32); }
    rvv_store16(got, rvv_sign_epi16(rvv_load16(a,vl), rvv_load16(b,vl), vl), vl);
    check("sign_epi16", got, want, 16);

    /* ---------- 重排（双源） ---------- */
    fill(a, b, mode);
    { simde__m256i sa, sb, sr; memcpy(&sa,a,32); memcpy(&sb,b,32);
      sr = simde_mm256_unpacklo_epi64(sa, sb); memcpy(want,&sr,32); }
    rvv_store16(got, rvv_unpacklo_epi64(rvv_load16(a,vl), rvv_load16(b,vl), vl), vl);
    check("unpacklo_epi64", got, want, 16);

    fill(a, b, mode);
    { simde__m256i sa, sb, sr; memcpy(&sa,a,32); memcpy(&sb,b,32);
      sr = simde_mm256_unpackhi_epi64(sa, sb); memcpy(want,&sr,32); }
    rvv_store16(got, rvv_unpackhi_epi64(rvv_load16(a,vl), rvv_load16(b,vl), vl), vl);
    check("unpackhi_epi64", got, want, 16);

    fill(a, b, mode);
    { simde__m256i sa, sb, sr; memcpy(&sa,a,32); memcpy(&sb,b,32);
      sr = simde_mm256_unpacklo_epi32(sa, sb); memcpy(want,&sr,32); }
    rvv_store16(got, rvv_unpacklo_epi32(rvv_load16(a,vl), rvv_load16(b,vl), vl), vl);
    check("unpacklo_epi32", got, want, 16);

    fill(a, b, mode);
    { simde__m256i sa, sb, sr; memcpy(&sa,a,32); memcpy(&sb,b,32);
      sr = simde_mm256_unpackhi_epi32(sa, sb); memcpy(want,&sr,32); }
    rvv_store16(got, rvv_unpackhi_epi32(rvv_load16(a,vl), rvv_load16(b,vl), vl), vl);
    check("unpackhi_epi32", got, want, 16);

    /* ---------- 按 32 位元素重排（跨 lane） ---------- */
    fill(a, b, mode);
    { simde__m256i sa, sb, sr; memcpy(&sa,a,32); memcpy(&sb,b,32);
      sr = simde_mm256_permutevar8x32_epi32(sa, sb); memcpy(want,&sr,32); }
    rvv_store16(got, rvv_permutevar8x32_epi32(rvv_load16(a,vl), rvv_load16(b,vl), vl), vl);
    check("permutevar8x32_epi32", got, want, 16);

    /* ---------- 字节重排 ---------- */
    fill(a, b, mode);
    { /* 用 idft64 实际使用的 complex_shuffle 作为字节索引：
         它在 16 位视角下就是"交换每对相邻 int16"（实部/虚部互换） */
      static const int8_t cs[32] = {2,3,0,1, 6,7,4,5, 10,11,8,9, 14,15,12,13,
                                    18,19,16,17, 22,23,20,21, 26,27,24,25, 30,31,28,29};
      simde__m256i sa, sb, sr;
      memcpy(&sa,a,32); memcpy(&sb,cs,32);
      sr = simde_mm256_shuffle_epi8(sa, sb); memcpy(want,&sr,32); }
    rvv_store16(got, rvv_shuffle_epi8_cplxswap(rvv_load16(a,vl), vl), vl);
    check("shuffle_epi8 (cplx swap)", got, want, 16);
    printf("\n");
  }

  /* ---------- lane 提取/插入（只用低位 128 位比对） ---------- */
  printf("---- lane 操作（比 128 位） ----\n");
  {
    int16_t a[16], b[16], got[16], want[16];
    fill(a, b, 1);
    { simde__m256i sa; simde__m128i r0;
      memcpy(&sa,a,32); r0 = simde_mm256_extracti128_si256(sa, 0);
      memset(want,0,32); memcpy(want, &r0, 16); }
    rvv_store16(got, rvv_extracti128_0(rvv_load16(a,vl), vl), vl);
    check("extracti128 lane0", got, want, 8);

    fill(a, b, 1);
    { simde__m256i sa; simde__m128i r1;
      memcpy(&sa,a,32); r1 = simde_mm256_extracti128_si256(sa, 1);
      memset(want,0,32); memcpy(want, &r1, 16); }
    rvv_store16(got, rvv_extracti128_1(rvv_load16(a,vl), vl), vl);
    check("extracti128 lane1", got, want, 8);

    fill(a, b, 1);
    { simde__m256i sa, sr; simde__m128i sb;
      memcpy(&sa,a,32); memcpy(&sb,b,16);
      sr = simde_mm256_insertf128_si256(sa, sb, 0); memcpy(want,&sr,32); }
    rvv_store16(got, rvv_insertf128_0(rvv_load16(a,vl), rvv_load16(b,vl), vl), vl);
    check("insertf128 lane0", got, want, 16);

    fill(a, b, 1);
    { simde__m256i sa, sr; simde__m128i sb;
      memcpy(&sa,a,32); memcpy(&sb,b,16);
      sr = simde_mm256_insertf128_si256(sa, sb, 1); memcpy(want,&sr,32); }
    rvv_store16(got, rvv_insertf128_1(rvv_load16(a,vl), rvv_load16(b,vl), vl), vl);
    check("insertf128 lane1", got, want, 16);
  }

  printf("\n==================== %s ====================\n",
         g_fail ? "有算子未通过" : "全部算子通过（地基可靠）");
  return g_fail;
}

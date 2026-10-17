/* rvv_idft64_test.c —— 手写 RVV 版 idft64 vs 原版 OAI idft64，逐位对比 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern void idft64    (int16_t *x, int16_t *y, unsigned char scale);
extern void idft64_rvv(int16_t *x, int16_t *y, unsigned char scale);

static uint32_t g_rng = 0x12345678u;
static uint32_t xr(void){uint32_t x=g_rng;x^=x<<13;x^=x>>17;x^=x<<5;return g_rng=x;}

static int check_case(const char *name, int16_t *x, unsigned char scale) {
  static int16_t ya[128] __attribute__((aligned(64)));
  static int16_t yb[128] __attribute__((aligned(64)));
  memset(ya,0,256); memset(yb,0,256);
  idft64(x, ya, scale);
  idft64_rvv(x, yb, scale);
  int bad=0, first=-1, maxd=0;
  for (int i=0;i<128;i++) {
    int d=(int)yb[i]-(int)ya[i];
    int ad = d < 0 ? -d : d;
    if (d) { bad++; if(first<0) first=i; if (ad > maxd) maxd = ad; }
  }
  printf("  %-30s %s", name, bad?"FAIL":"OK");
  if (bad) {
    printf("   (%d/128 differ, max|d|=%d)\n", bad, maxd);
    for (int i=first;i<128 && i<first+6;i++)
      printf("       [%3d]  rvv=%7d   oai=%7d\n", i, yb[i], ya[i]);
  } else printf("\n");
  return bad;
}

int main(void) {
  int fail=0;
  static int16_t x[128] __attribute__((aligned(64)));
  printf("RVV idft64  vs  原版 OAI idft64   （逐位要求一致）\n\n");

  /* 1. 稀疏 ±364 —— 原版 main() 用的信号形状 */
  memset(x,0,256);
  for (int i=2;i<36;i++)  x[i]=(xr()&1)?364:-364;
  for (int i=92;i<128;i++) x[i]=(xr()&1)?364:-364;
  fail |= check_case("稀疏 ±364 (scale=1)", x, 1);
  fail |= check_case("稀疏 ±364 (scale=0)", x, 0);

  /* 2. 随机，逐级放大（测饱和路径） */
  int mags[] = {364, 1024, 2048, 4095};
  for (int k=0;k<4;k++) {
    int mag=mags[k];
    for (int i=0;i<128;i++) x[i]=(int16_t)((int)(xr()%(2*mag+1))-mag);
    char nm[64]; snprintf(nm,sizeof nm,"随机 ±%d (scale=1)", mag);
    fail |= check_case(nm, x, 1);
  }

  /* 3. 满量程随机 —— 强制走饱和 */
  for (int i=0;i<128;i++) x[i]=(int16_t)(xr()&0xFFFF);
  fail |= check_case("满量程随机 (scale=1)", x, 1);

  /* 4. 特殊形状 */
  memset(x,0,256); x[0]=4095;
  fail |= check_case("冲激 (scale=1)", x, 1);
  for (int i=0;i<128;i++) x[i]=500;
  fail |= check_case("直流 500 (scale=1)", x, 1);
  for (int i=0;i<128;i++) x[i]=(int16_t)(32767*(i&1?1:-1));
  fail |= check_case("满幅交替 (scale=1)", x, 1);

  printf("\n==================== %s ====================\n",
         fail ? "有差异 —— 需要排查" : "逐位完全一致（手写 RVV 版正确）");
  return fail;
}

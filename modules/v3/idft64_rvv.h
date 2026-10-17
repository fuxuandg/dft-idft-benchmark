/* idft64_rvv.h —— 对外唯一的接口声明
 *
 * 就这一个函数，签名与 OAI 原版 idft64() 完全一致，可以直接替换。
 */
#ifndef IDFT64_RVV_H
#define IDFT64_RVV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 64 点逆 DFT
 *   x     : 输入，128 个 int16 = 64 个复数（re,im 交错）
 *   y     : 输出，同格式（不可与 x 相同，内部先读后写）
 *   scale : 1 → 结果再算术右移 3 位；0 → 不移位
 */
void idft64_rvv(int16_t *x, int16_t *y, unsigned char scale);

#ifdef __cplusplus
}
#endif

#endif /* IDFT64_RVV_H */

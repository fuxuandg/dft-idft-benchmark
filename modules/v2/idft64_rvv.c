/* ============================================================================
 * idft64_rvv.c —— 64 点逆 DFT，纯手写 RISC-V 向量（RVV）
 * ============================================================================
 *
 * 【这是什么】
 *   一个**完全自包含**的 64 点 IDFT。零 OAI 依赖、零 SIMDe 依赖、
 *   零运行时初始化。唯一的外部依赖是 <riscv_vector.h>（编译器自带的 RVV 头）。
 *
 *   对照：同一个算法在 OAI 里叫 idft64()，写在 8700 行的 oai_dfts.c 里，
 *   依赖 SIMDe 宏、静态旋转因子表、以及 dfts_autoinit() 启动时填的大表。
 *   这份文件做的事**完全一样**、结果**逐位相同**，但只有 3 个函数。
 *
 * ----------------------------------------------------------------------------
 * 【★ 调用位置（唯一入口）】
 *
 *       void idft64_rvv(int16_t *x, int16_t *y, unsigned char scale);
 *
 *   x      : 输入，128 个 int16 = 64 个复数，re/im 交错 [re0,im0,re1,im1,...]
 *   y      : 输出，同上格式（可以和 x 相同吗？—— 不可以，内部先读后写）
 *   scale  : 1 = 结果再右移 3 位（OAI 的常规用法）；0 = 不移位
 *
 *   签名与 OAI 原版 idft64() **完全一致**，可以直接替换。
 *   main.c 里就是这么调的（见 main.c 的 run_case()）。
 *
 * ----------------------------------------------------------------------------
 * 【★ 算法过程实际进行的位置】
 *
 *   idft64_rvv()                        ← 总控，5 个步骤
 *     ├─ 步骤 1 输入重排      8 × permutevar8x32     （本文件，见 "STEP 1"）
 *     ├─ 步骤 2 拆包          8 × unpack             （本文件，见 "STEP 2"）
 *     ├─ 步骤 3 两级 16 点 IDFT → rvv_idft16()       （本文件，见其定义）
 *     ├─ 步骤 4 两级基-4 合并   → rvv_ibfly4()        （本文件，见其定义）
 *     └─ 步骤 5 定标输出      >>3 + 写回             （本文件，见 "STEP 5"）
 *
 *   底层算子（vsadd / vrgather / vwmul 等）全部在 rvv_ops.h 里，
 *   每个算子都注明了"对应哪条 AVX2 指令"，便于对照原版。
 * ==========================================================================*/

#include "rvv_ops.h"

/* ==========================================================================
 * 一、旋转因子（定点 Q15：32767 代表 1.0）
 *
 * **不需要任何运行时初始化**——这些是编译期常量。
 * （OAI 原版要调 dfts_autoinit() 在启动时填一大堆表；这里全部省掉。）
 *
 * 【数值怎么来的】
 *   全部是 round(32767 * cos/sin(2*pi*k/N))。例如 64 点的第 1 个系数
 *       W64^1 = exp(-j*2pi/64)
 *       cos(2pi/64) = 0.995185  -> 32767*0.995185 = 32609.2 -> 32609
 *       -sin(2pi/64) = -0.098017 -> -3211.9              -> -3212
 *   与 oai_dfts.c 里那几张表**逐值相同**（已用 Tier 1 逐位比对确认）。
 *
 * 【为什么是这种"每 8 个值重复一遍"的布局】
 *   这是 AVX2 留下的痕迹：256 位向量 = 两个 128 位 lane，原版让两个 lane
 *   装**相同**的数据，所以每条向量的后 8 个 int16 是前 8 个的拷贝。
 *   RVV 这边保留同样的布局，是为了让"一条 vle16 直接喂给算子、中间不重排"，
 *   从而做到 bit-exact 且不引入额外指令。
 * ==========================================================================*/

/* ±j 的符号掩码，交给 sign_epi16 使用：-1 → 乘 j，+1 → 乘 -j。
 * （用于 radix-4 蝶形里那两个"乘 ±j"的支路，一条 vsign 就够，
 *   不需要真的做复数乘法。） */
static const int16_t TW_J_MUL[16] = {
  1, -1, 1, -1, 1, -1, 1, -1,
  1, -1, 1, -1, 1, -1, 1, -1,
};

/* 16 点 IDFT 用的旋转因子，实部。3 条向量 = 3 个蝶形级。
 * 每条向量 = 8 个有效值 + 同样 8 个值的拷贝（两个 lane 相同）。 */
static const int16_t TW16_RE[48] = {
  /* 级 1 */ 32767, 0, 30272, -12540, 23169, -23170, 12539, -30273,
             32767, 0, 30272, -12540, 23169, -23170, 12539, -30273,
  /* 级 2 */ 32767, 0, 23169, -23170, 0, -32767, -23170, -23170,
             32767, 0, 23169, -23170, 0, -32767, -23170, -23170,
  /* 级 3 */ 32767, 0, 12539, -30273, -23170, -23170, -30273, 12539,
             32767, 0, 12539, -30273, -23170, -23170, -30273, 12539,
};

/* 16 点 IDFT 用的旋转因子，虚部。布局同上。 */
static const int16_t TW16_IM[48] = {
  /* 级 1 */ 0, 32767, 12540, 30272, 23170, 23169, 30273, 12539,
             0, 32767, 12540, 30272, 23170, 23169, 30273, 12539,
  /* 级 2 */ 0, 32767, 23170, 23169, 32767, 0, 23170, -23170,
             0, 32767, 23170, 23169, 32767, 0, 23170, -23170,
  /* 级 3 */ 0, 32767, 30273, 12539, 23170, -23170, -12539, -30273,
             0, 32767, 30273, 12539, 23170, -23170, -12539, -30273,
};

/* 64 点最后一级合并用的旋转因子，第一组（6 条向量）。
 * ★ 原版这里有个坑：变量名 tw64a_256 实际指向的是 tw64（不是 tw64a），
 *   所以只带这一张就够；tw64a / tw64b 是**死表**，本文件已删除。 */
static const int16_t TW64_A[96] = {
  32767, 0, 32609, -3212, 32137, -6393, 31356, -9512,
  30272, -12540, 28897, -15447, 27244, -18205, 25329, -20788,
  23169, -23170, 20787, -25330, 18204, -27245, 15446, -28898,
  12539, -30273, 9511, -31357, 6392, -32138, 3211, -32610,
  32767, 0, 32137, -6393, 30272, -12540, 27244, -18205,
  23169, -23170, 18204, -27245, 12539, -30273, 6392, -32138,
  0, -32767, -6393, -32138, -12540, -30273, -18205, -27245,
  -23170, -23170, -27245, -18205, -30273, -12540, -32138, -6393,
  32767, 0, 31356, -9512, 27244, -18205, 20787, -25330,
  12539, -30273, 3211, -32610, -6393, -32138, -15447, -28898,
  -23170, -23170, -28898, -15447, -32138, -6393, -32610, 3211,
  -30273, 12539, -25330, 20787, -18205, 27244, -9512, 31356,
};

/* 64 点最后一级合并用的旋转因子，第二组（6 条向量）。
 * ★ 原版变量名 tw64b_256 实际指向 tw64c —— 名字同样具有误导性。 */
static const int16_t TW64_C[96] = {
  0, 32767, 3212, 32609, 6393, 32137, 9512, 31356,
  12540, 30272, 15447, 28897, 18205, 27244, 20788, 25329,
  23170, 23169, 25330, 20787, 27245, 18204, 28898, 15446,
  30273, 12539, 31357, 9511, 32138, 6392, 32610, 3211,
  0, 32767, 6393, 32137, 12540, 30272, 18205, 27244,
  23170, 23169, 27245, 18204, 30273, 12539, 32138, 6392,
  32767, 0, 32138, -6393, 30273, -12540, 27245, -18205,
  23170, -23170, 18205, -27245, 12540, -30273, 6393, -32138,
  0, 32767, 9512, 31356, 18205, 27244, 25330, 20787,
  30273, 12539, 32610, 3211, 32138, -6393, 28898, -15447,
  23170, -23170, 15447, -28898, 6393, -32138, -3211, -32610,
  -12539, -30273, -20787, -25330, -27244, -18205, -31356, -9512,
};

/* ==========================================================================
 * 二、便捷包装：让下面的算法读起来像 AVX2 原版
 * ==========================================================================*/

static inline vint16m1_t vld(const int16_t *p, size_t vl) { return rvv_load16(p, vl); }
static inline void vst(int16_t *p, vint16m1_t v, size_t vl) { rvv_store16(p, v, vl); }

/* 实部/虚部互换（原版用 shuffle_epi8 实现，16 位视角就是交换相邻 int16，
 * 也就是每个复数乘上 j）。 */
static inline vint16m1_t cswap(vint16m1_t a, size_t vl) { return rvv_shuffle_epi8_cplxswap(a, vl); }

/* 原来的 sign(a,[-1,+1]) 再交换相邻元素，结果恒为 [im,-re]。
 * 固定相邻交换后乘 [+1,-1] 完全等价，却不再需要两次比较、取负和
 * 两次 merge。int16 的 -32768 乘 -1 仍按低 16 位回绕，语义一致。 */
static inline vint16m1_t cmul_j(vint16m1_t a, vint16m1_t jmul, size_t vl) {
  return __riscv_vmul_vv_i16m1(cswap(a, vl), jmul, vl);
}

/* Q15 定点复数乘法：C = A * B
 *   a / b 是 re,im 交错的 Q15；b2 是 b 的"实虚互换"版（预先算好省一条 shuffle）。
 *   内部用 32 位保精度：乘累加 → 右移 15 → 饱和窄化回 16 位。
 *   （对应 AVX2 的 madd_epi16 + srai_epi32 + packs_epi32 三件套。） */
static inline vint16m1_t packed_cmult2(vint16m1_t a, vint16m1_t b, vint16m1_t b2, size_t vl) {
  /* 一条 e16m1 向量也可零指令地视为 8 个 packed int32：
   * 每个 int32 的低/高 16 位正好是 re/im。用 32 位移位做符号扩展，
   * 避免旧 madd 路径为了拆 even/odd lane 进行的大量 gather。 */
  const size_t vl32 = __riscv_vsetvl_e32m1(8);
  vint32m1_t ap = __riscv_vreinterpret_v_i16m1_i32m1(a);
  vint32m1_t bp = __riscv_vreinterpret_v_i16m1_i32m1(b);
  vint32m1_t cp = __riscv_vreinterpret_v_i16m1_i32m1(b2);

  vint32m1_t ar = __riscv_vsra_vx_i32m1(__riscv_vsll_vx_i32m1(ap, 16, vl32), 16, vl32);
  vint32m1_t ai = __riscv_vsra_vx_i32m1(ap, 16, vl32);
  vint32m1_t br = __riscv_vsra_vx_i32m1(__riscv_vsll_vx_i32m1(bp, 16, vl32), 16, vl32);
  vint32m1_t bi = __riscv_vsra_vx_i32m1(bp, 16, vl32);
  vint32m1_t cr = __riscv_vsra_vx_i32m1(__riscv_vsll_vx_i32m1(cp, 16, vl32), 16, vl32);
  vint32m1_t ci = __riscv_vsra_vx_i32m1(cp, 16, vl32);

  vint32m1_t cre = __riscv_vmul_vv_i32m1(ar, br, vl32);
  cre = __riscv_vmacc_vv_i32m1(cre, ai, bi, vl32);
  vint32m1_t cim = __riscv_vmul_vv_i32m1(ar, cr, vl32);
  cim = __riscv_vmacc_vv_i32m1(cim, ai, ci, vl32);

  /* RDN 是不加舍入量的截断，等价于旧版算术右移 15；vnclip 同时
   * 完成有符号饱和。窄化后布局为 re0..re7,im0..im7，再固定交错。 */
  vint32m2_t ri = __riscv_vcreate_v_i32m1_i32m2(cre, cim);
  vint16m1_t planar = __riscv_vnclip_wx_i16m1(ri, 15, __RISCV_VXRM_RDN, vl);
  static const uint16_t interleave[16] = {
    0,8, 1,9, 2,10, 3,11, 4,12, 5,13, 6,14, 7,15
  };
  return __riscv_vrgather_vv_i16m1(planar, rvv_idx16(interleave, vl), vl);
}

/* ==========================================================================
 * 三、rvv_idft16 —— 16 点 IDFT（一次算 2 个并行块）
 *
 * 结构：两级 radix-4 蝶形，中间夹一组旋转因子乘法。
 *   第一级：4 个无旋转因子的基-4 蝶形
 *   重排：unpack ×2 轮（对应 AVX2 的 unpacklo/hi_epi32 + unpacklo/hi_epi64）
 *   第二级：3 个带旋转因子的复数乘 + 4 个基-4 蝶形
 *   收尾：按 128 位半段重新拼接（对应 4 条 insertf128/extracti128）
 *
 * ★ 单向数据流，中间值全部用独立变量 —— 因为 vint16m1_t 是 scalable 类型，
 *   **不能做指针算术**（这是 RVV intrinsic 的硬约束，见 rvv_ops.h 顶部说明）。
 * ==========================================================================*/
static void rvv_idft16(vint16m1_t x0, vint16m1_t x1, vint16m1_t x2, vint16m1_t x3,
                       vint16m1_t *o0, vint16m1_t *o1, vint16m1_t *o2, vint16m1_t *o3,
                       size_t vl) {
  const vint16m1_t jmul = vld(TW_J_MUL, vl);
  const vint16m1_t a0 = vld(TW16_RE +  0, vl), a1 = vld(TW16_RE + 16, vl), a2 = vld(TW16_RE + 32, vl);
  const vint16m1_t b0 = vld(TW16_IM +  0, vl), b1 = vld(TW16_IM + 16, vl), b2 = vld(TW16_IM + 32, vl);
  vint16m1_t x02t, x13t, t0, t1, t2, t3, f1, f3, y0, y1, y2, y3;

  /* ---- 第一级：4 个基-4 蝶形（无输入旋转因子）---- */
  x02t = rvv_adds_epi16(x0, x2, vl);
  x13t = rvv_adds_epi16(x1, x3, vl);
  t0   = rvv_adds_epi16(x02t, x13t, vl);
  t2   = rvv_subs_epi16(x02t, x13t, vl);
  f1   = cmul_j(x1, jmul, vl);
  f3   = cmul_j(x3, jmul, vl);
  x02t = rvv_subs_epi16(x0, x2, vl);
  x13t = rvv_subs_epi16(f1, f3, vl);
  t3   = rvv_adds_epi16(x02t, x13t, vl);
  t1   = rvv_subs_epi16(x02t, x13t, vl);

  /* ---- 重排：先按 32 位交错，再按 64 位交错 ---- */
  y0 = rvv_unpacklo_epi32(t0, t1, vl);
  y1 = rvv_unpackhi_epi32(t0, t1, vl);
  y2 = rvv_unpacklo_epi32(t2, t3, vl);
  y3 = rvv_unpackhi_epi32(t2, t3, vl);
  t0 = rvv_unpacklo_epi64(y0, y2, vl);
  t1 = rvv_unpackhi_epi64(y0, y2, vl);
  t2 = rvv_unpacklo_epi64(y1, y3, vl);
  t3 = rvv_unpackhi_epi64(y1, y3, vl);

  /* ---- 第二级：3 个带旋转因子的复数乘 ---- */
  t1 = packed_cmult2(t1, a0, b0, vl);
  t2 = packed_cmult2(t2, a1, b1, vl);
  t3 = packed_cmult2(t3, a2, b2, vl);

  /* ---- 第二级：4 个基-4 蝶形 ---- */
  x02t = rvv_adds_epi16(t0, t2, vl);
  x13t = rvv_adds_epi16(t1, t3, vl);
  y0   = rvv_adds_epi16(x02t, x13t, vl);
  y2   = rvv_subs_epi16(x02t, x13t, vl);
  f1   = cmul_j(t1, jmul, vl);
  f3   = cmul_j(t3, jmul, vl);
  x02t = rvv_subs_epi16(t0, t2, vl);
  x13t = rvv_subs_epi16(f1, f3, vl);
  y3   = rvv_adds_epi16(x02t, x13t, vl);
  y1   = rvv_subs_epi16(x02t, x13t, vl);

  /* ---- 收尾：把两对 128-bit 半段做 2x2 transpose ----
   *   o0=[y0.lo,y1.lo], o2=[y0.hi,y1.hi]
   *   o1=[y2.lo,y3.lo], o3=[y2.hi,y3.hi]
   * 旧版逐个 extract 后再走通用双源 gather/merge；固定半宽拼接直接用
   * slide，既省索引表，也显著缩短重排链。 */
  *o0 = __riscv_vslideup_vx_i16m1_tu(y0, y1, 8, vl);
  *o1 = __riscv_vslideup_vx_i16m1_tu(y2, y3, 8, vl);
  vint16m1_t y0h = __riscv_vslidedown_vx_i16m1(y0, 8, vl);
  vint16m1_t y1h = __riscv_vslidedown_vx_i16m1(y1, 8, vl);
  vint16m1_t y2h = __riscv_vslidedown_vx_i16m1(y2, 8, vl);
  vint16m1_t y3h = __riscv_vslidedown_vx_i16m1(y3, 8, vl);
  *o2 = __riscv_vslideup_vx_i16m1_tu(y0h, y1h, 8, vl);
  *o3 = __riscv_vslideup_vx_i16m1_tu(y2h, y3h, 8, vl);
}

/* ==========================================================================
 * 四、rvv_ibfly4 —— 逆向基-4 蝶形（带输入旋转因子）
 *
 * 数学上就是一次 radix-4 DFT：
 *   o0 = x0 + x1' + x2' + x3'
 *   o1 = x0 - j*x1' - x2' + j*x3'
 *   o2 = x0 - x1' + x2' - x3'
 *   o3 = x0 + j*x1' - x2' - j*x3'
 * 其中 x1'/x2'/x3' 是乘过旋转因子的输入。
 * ==========================================================================*/
static inline void rvv_ibfly4(vint16m1_t x0, vint16m1_t x1, vint16m1_t x2, vint16m1_t x3,
                              vint16m1_t *o0, vint16m1_t *o1, vint16m1_t *o2, vint16m1_t *o3,
                              vint16m1_t tw1, vint16m1_t tw2, vint16m1_t tw3,
                              vint16m1_t tw1b, vint16m1_t tw2b, vint16m1_t tw3b,
                              size_t vl) {
  const vint16m1_t jmul = vld(TW_J_MUL, vl);
  vint16m1_t x1t, x2t, x3t, x02t, x13t, f1, f3;

  x1t = packed_cmult2(x1, tw1, tw1b, vl);      /* ★ 旋转因子在这里乘 */
  x2t = packed_cmult2(x2, tw2, tw2b, vl);
  x3t = packed_cmult2(x3, tw3, tw3b, vl);

  x02t = rvv_adds_epi16(x0, x2t, vl);
  x13t = rvv_adds_epi16(x1t, x3t, vl);
  *o0  = rvv_adds_epi16(x02t, x13t, vl);
  *o2  = rvv_subs_epi16(x02t, x13t, vl);

  f1 = cmul_j(x1t, jmul, vl);
  f3 = cmul_j(x3t, jmul, vl);
  x02t = rvv_subs_epi16(x0, x2t, vl);
  x13t = rvv_subs_epi16(f1, f3, vl);
  *o3  = rvv_adds_epi16(x02t, x13t, vl);
  *o1  = rvv_subs_epi16(x02t, x13t, vl);
}

/* ==========================================================================
 * 五、idft64_rvv —— 唯一入口（调用位置）
 *
 * 64 = 4 x 4 x 4，所以是"三次基-4"的分解：
 *   先 2 个 16 点 IDFT（16 = 4x4），再做一次跨 16 点的基-4 合并。
 *
 * 数据布局：一条 vint16m1_t = 16 个 int16 = 8 个复数。
 *           64 个复数 = 128 个 int16 = **8 条向量**。
 * ==========================================================================*/
void idft64_rvv(int16_t *x, int16_t *y, unsigned char scale) {
  const size_t vl = __riscv_vsetvl_e16m1(RVV_VL16);   /* ★ 整个函数只配置这一次 */

  /* ---------------- STEP 1：输入重排（8 × permutevar8x32） ----------------
   * 按 32 位元素（= 1 个复数）重新排序，把"隔 8 个取一个"变成"连续取"，
   * 为后面的 16 点 IDFT 摆好位置。 */
  /* 这里的置换掩码对 idft64 永远不变。旧版先装载 AVX2 风格的
   * 32-bit mask，再在每次调用中用 pick_lo/&7/<<1/+off 推导 16-bit
   * gather 索引。直接存最终索引能删掉整段运行时索引计算。 */
  static const uint16_t perm16[16] = {
    0,1, 8,9, 4,5, 12,13, 2,3, 10,11, 6,7, 14,15
  };
  const vuint16m1_t pidx = __riscv_vle16_v_u16m1(perm16, vl);
  vint16m1_t l0 = __riscv_vrgather_vv_i16m1(vld(x +   0, vl), pidx, vl);
  vint16m1_t l1 = __riscv_vrgather_vv_i16m1(vld(x +  16, vl), pidx, vl);
  vint16m1_t l2 = __riscv_vrgather_vv_i16m1(vld(x +  32, vl), pidx, vl);
  vint16m1_t l3 = __riscv_vrgather_vv_i16m1(vld(x +  48, vl), pidx, vl);
  vint16m1_t l4 = __riscv_vrgather_vv_i16m1(vld(x +  64, vl), pidx, vl);
  vint16m1_t l5 = __riscv_vrgather_vv_i16m1(vld(x +  80, vl), pidx, vl);
  vint16m1_t l6 = __riscv_vrgather_vv_i16m1(vld(x +  96, vl), pidx, vl);
  vint16m1_t l7 = __riscv_vrgather_vv_i16m1(vld(x + 112, vl), pidx, vl);

  /* ---------------- STEP 2：拆包（8 × unpack） ----------------
   * 把 8 条向量两两配对，分成两组各 4 条，交给两次 16 点 IDFT。 */
  vint16m1_t t0 = rvv_unpacklo_epi64(l0, l1, vl), t4 = rvv_unpackhi_epi64(l0, l1, vl);
  vint16m1_t t1 = rvv_unpacklo_epi64(l2, l3, vl), t5 = rvv_unpackhi_epi64(l2, l3, vl);
  vint16m1_t t2 = rvv_unpacklo_epi64(l4, l5, vl), t6 = rvv_unpackhi_epi64(l4, l5, vl);
  vint16m1_t t3 = rvv_unpacklo_epi64(l6, l7, vl), t7 = rvv_unpackhi_epi64(l6, l7, vl);

  /* ---------------- STEP 3：两次 16 点 IDFT ----------------
   * 算法实际计算的地方在 rvv_idft16()。 */
  vint16m1_t y0, y1, y2, y3, y4, y5, y6, y7;
  rvv_idft16(t0, t1, t2, t3, &y0, &y1, &y2, &y3, vl);
  rvv_idft16(t4, t5, t6, t7, &y4, &y5, &y6, &y7, vl);

  /* ---------------- STEP 4：两次基-4 合并（带 64 点旋转因子） ----------------
   * 算法实际计算的地方在 rvv_ibfly4()。
   * ★ 注意分组：偶数索引一组、奇数索引一组，这是 64 点 DIT 分解的要求。 */
  const vint16m1_t A0 = vld(TW64_A +  0, vl), A1 = vld(TW64_A + 16, vl), A2 = vld(TW64_A + 32, vl),
                   A3 = vld(TW64_A + 48, vl), A4 = vld(TW64_A + 64, vl), A5 = vld(TW64_A + 80, vl);
  const vint16m1_t B0 = vld(TW64_C +  0, vl), B1 = vld(TW64_C + 16, vl), B2 = vld(TW64_C + 32, vl),
                   B3 = vld(TW64_C + 48, vl), B4 = vld(TW64_C + 64, vl), B5 = vld(TW64_C + 80, vl);
  vint16m1_t r0, r1, r2, r3, r4, r5, r6, r7;

  rvv_ibfly4(y0, y2, y4, y6, &r0, &r2, &r4, &r6, A0, A2, A4, B0, B2, B4, vl);
  rvv_ibfly4(y1, y3, y5, y7, &r1, &r3, &r5, &r7, A1, A3, A5, B1, B3, B5, vl);

  /* ---------------- STEP 5：定标 + 输出 ----------------
   * OAI 的约定：scale != 0 时结果要再右移 3 位（相当于除以 8）。
   * 这**不是**数学上的 1/N 归一化（64 点应该是 /64），而是 OAI 的定点增益安排：
   * 前两级已经隐式放大，最后 >>3 只是把总增益调回 2^-3。
   * 与之配套：输入幅度必须 <= 约 4095，否则中间级会饱和到 32767。 */
  if (scale > 0) {
    vst(y +   0, rvv_srai_epi16(r0, 3, vl), vl);
    vst(y +  16, rvv_srai_epi16(r1, 3, vl), vl);
    vst(y +  32, rvv_srai_epi16(r2, 3, vl), vl);
    vst(y +  48, rvv_srai_epi16(r3, 3, vl), vl);
    vst(y +  64, rvv_srai_epi16(r4, 3, vl), vl);
    vst(y +  80, rvv_srai_epi16(r5, 3, vl), vl);
    vst(y +  96, rvv_srai_epi16(r6, 3, vl), vl);
    vst(y + 112, rvv_srai_epi16(r7, 3, vl), vl);
  } else {
    vst(y +   0, r0, vl); vst(y +  16, r1, vl); vst(y +  32, r2, vl); vst(y +  48, r3, vl);
    vst(y +  64, r4, vl); vst(y +  80, r5, vl); vst(y +  96, r6, vl); vst(y + 112, r7, vl);
  }
}

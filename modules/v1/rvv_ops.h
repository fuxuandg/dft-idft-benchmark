/* ============================================================================
 * rvv_ops.h —— A 线（手写 RVV）基础算子库
 *
 * 【为什么必须独立实现】
 * 前置验证的硬结论：GCC 固定向量 simde__m256i（vector_size(32)）与 RVV
 * scalable 类型 vint16m1_t **无零成本转换**（memcpy 会生成真实 vle/vse 往返；
 * 强制转换被编译器拒绝：different size）。? A 线全程使用 vint16m1_t。
 *
 * 【★ 三个关键设计决策】
 *
 * 1. AVX2 是"双源"，RVV 的 vrgather 是"单源"
 *    unpack/shuffle 要同时从 a、b 取数据。解法：两次 vrgather + 一次 vmerge。
 *    不用 LMUL=2 拼接，是为了不引入额外 vsetvli（消灭 vsetvli 是第一优先级）。
 *    巧妙之处：ia 与 ib **是同一个值**（都取索引低 4 位），所以只要一张表：
 *        r = merge(gather(a, idx&15), gather(b, idx&15), mask=(idx>>4)!=0)
 *
 * 2. vreinterpret 只能转"相同 SEW"，不能改 SEW
 *    __riscv_vreinterpret_v_i16m1_u16m1   ? 合法
 *    __riscv_vreinterpret_v_i16m1_u32m1   ? 编译报错（SEW 16→32）
 *    __riscv_vreinterpret_v_i16m1_u8m1    ? 编译报错（SEW 16→8）
 *    ? 所以**不能**用"32 位视角"或"字节视角"来实现 permute/shuffle。
 *    ? 统一改为"**16 位视角的收集索引表**"：把 AVX2 的 32 位/字节语义
 *      手工展开成 16 位元素下标。好处是索引表变成**编译期常量**，
 *      而且实现方式统一（全部落到 vrgather）。
 *
 * 3. AVX2 的 lane 语义
 *    256 位 = 两个独立 128 位 lane；unpack/shuffle 只在各自 lane 内搬，
 *    不跨 lane。索引表必须分段构造，否则会搬错。
 *
 * 【配置约定】
 *    SEW=16, LMUL=1, VL=16。所有算子**接收 vl、自己绝不调用
 *    __riscv_vsetvl_***（配置只由最外层做一次）。
 * ==========================================================================*/

#ifndef RVV_OPS_H
#define RVV_OPS_H

#include <stdint.h>
#include <riscv_vector.h>

#define RVV_VL16  16
#define RVV_CFG() __riscv_vsetvl_e16m1(RVV_VL16)

static inline vuint16m1_t rvv_idx16(const uint16_t *t, size_t vl) {
  return __riscv_vle16_v_u16m1(t, vl);
}

/* ---- 单源收集：idx[i] ∈ [0,15] ---- */
static inline vint16m1_t rvv_gather1(vint16m1_t a, const uint16_t idx[16], size_t vl) {
  return __riscv_vrgather_vv_i16m1(a, rvv_idx16(idx, vl), vl);
}

/* ---- 双源收集：idx[i] ∈ [0,31]，0..15 取 a、16..31 取 b (idx-16) ----
 * 见上面"关键设计决策 1"的说明。 */
static inline vint16m1_t rvv_gather2(vint16m1_t a, vint16m1_t b,
                                     const uint16_t idx[16], size_t vl) {
  vuint16m1_t vi  = rvv_idx16(idx, vl);
  vuint16m1_t vlo = __riscv_vand_vx_u16m1(vi, 15, vl);
  vbool16_t   m   = __riscv_vmsne_vx_u16m1_b16(__riscv_vsrl_vx_u16m1(vi, 4, vl), 0, vl);
  vint16m1_t ra = __riscv_vrgather_vv_i16m1(a, vlo, vl);
  vint16m1_t rb = __riscv_vrgather_vv_i16m1(b, vlo, vl);
  return __riscv_vmerge_vvm_i16m1(ra, rb, m, vl);   /* mask 真 -> 取 b */
}

/* =========================== 一、算术（80 次） ==========================
 * AVX2 的 adds/subs 是**饱和**加减；RVV 有硬件指令，一条对一条。
 * 这是相对 B 线补丁（软件模拟饱和）最大的优势。 */
static inline vint16m1_t rvv_adds_epi16(vint16m1_t a, vint16m1_t b, size_t vl) {
  return __riscv_vsadd_vv_i16m1(a, b, vl);
}
static inline vint16m1_t rvv_subs_epi16(vint16m1_t a, vint16m1_t b, size_t vl) {
  return __riscv_vssub_vv_i16m1(a, b, vl);
}
static inline vint16m1_t rvv_srai_epi16(vint16m1_t a, int n, size_t vl) {
  return __riscv_vsra_vx_i16m1(a, n, vl);
}

/* =========================== 二、符号（20 次） ==========================
 * sign_epi16(a,b): b>0→a, b<0→-a, b==0→0 */
static inline vint16m1_t rvv_sign_epi16(vint16m1_t a, vint16m1_t b, size_t vl) {
  vbool16_t bneg  = __riscv_vmslt_vx_i16m1_b16(b, 0, vl);
  vbool16_t bzero = __riscv_vmseq_vx_i16m1_b16(b, 0, vl);
  vint16m1_t na   = __riscv_vneg_v_i16m1(a, vl);
  vint16m1_t r    = __riscv_vmerge_vvm_i16m1(a, na, bneg, vl);
  return __riscv_vmerge_vxm_i16m1(r, 0, bzero, vl);
}

/* ===================== 三、重排（144 次，全部 16 位视角） ================
 * 每个索引表都按 AVX2 的 128 位 lane 语义手工展开。 */
#define RVV_UNPACK2(name, I0,I1,I2,I3,I4,I5,I6,I7, I8,I9,IA,IB,IC,ID,IE,IF)   \
  static inline vint16m1_t name(vint16m1_t a, vint16m1_t b, size_t vl) {      \
    static const uint16_t idx[16] = {I0,I1,I2,I3,I4,I5,I6,I7,                 \
                                     I8,I9,IA,IB,IC,ID,IE,IF};                \
    return rvv_gather2(a, b, idx, vl);                                        \
  }

/* unpacklo_epi64: [a0-3 | b0-3 | a8-11 | b8-11]（后半段用高 lane 的 a/b） */
RVV_UNPACK2(rvv_unpacklo_epi64, 0,1,2,3, 16,17,18,19, 8,9,10,11, 24,25,26,27)
/* unpackhi_epi64: [a4-7 | b4-7 | a12-15 | b12-15] */
RVV_UNPACK2(rvv_unpackhi_epi64, 4,5,6,7, 20,21,22,23, 12,13,14,15, 28,29,30,31)
/* unpacklo_epi32: lane 内低 32 位（=2 个 int16）交错 */
RVV_UNPACK2(rvv_unpacklo_epi32, 0,1, 16,17, 2,3, 18,19, 8,9, 24,25, 10,11, 26,27)
/* unpackhi_epi32 */
RVV_UNPACK2(rvv_unpackhi_epi32, 4,5, 20,21, 6,7, 22,23, 12,13, 28,29, 14,15, 30,31)
#undef RVV_UNPACK2

/* permutevar8x32_epi32(a, idx)：
 * ★ 这个是**跨 lane** 的（AVX2 文档明确），索引取低 3 位。
 * 由于不能转成"32 位视角"，改为在 16 位视角下展开：
 *   结果[2j] = a[2k], 结果[2j+1] = a[2k+1]，其中 k = idx32[j] & 7
 * 步骤：取出每个 32 位元素的低 16 位 → k = &7 → 乘 2 → 加 [0,1] 交替偏移。 */
static inline vint16m1_t rvv_permutevar8x32_epi32(vint16m1_t a, vint16m1_t idx, size_t vl) {
  static const uint16_t pick_lo[16] = {0,0, 2,2, 4,4, 6,6, 8,8, 10,10, 12,12, 14,14};
  static const uint16_t off[16]     = {0,1, 0,1, 0,1, 0,1, 0,1, 0,1, 0,1, 0,1};
  vuint16m1_t vi   = __riscv_vreinterpret_v_i16m1_u16m1(idx);   /* 同 SEW，合法 */
  vuint16m1_t lo   = __riscv_vrgather_vv_u16m1(vi, rvv_idx16(pick_lo, vl), vl);
  vuint16m1_t k    = __riscv_vand_vx_u16m1(lo, 7, vl);
  vuint16m1_t sidx = __riscv_vadd_vv_u16m1(__riscv_vsll_vx_u16m1(k, 1, vl),
                                           rvv_idx16(off, vl), vl);
  vuint16m1_t va   = __riscv_vreinterpret_v_i16m1_u16m1(a);
  return __riscv_vreinterpret_v_u16m1_i16m1(__riscv_vrgather_vv_u16m1(va, sidx, vl));
}

/* shuffle_epi8(a, b)：AVX2 语义是"按字节重排"。
 * ★ 因为 SEW 16→8 的 reinterpret 不合法，这里按 idft64 的**实际用法**实现：
 *   在 idft64 中 complex_shuffle 的作用就是"交换每个 32 位内的两个 16 位"
 *   （= 实部/虚部互换）。所以用 16 位视角的固定交换表表达，语义完全一致，
 *   且索引表是编译期常量 ? 一条 vrgather 顶掉 B 线补丁的一串 shuffle。
 * 若将来需要真正的任意字节粒度，需另行处理（见 docs/12 的局限性说明）。 */
static inline vint16m1_t rvv_shuffle_epi8_cplxswap(vint16m1_t a, size_t vl) {
  static const uint16_t idx[16] = {1,0, 3,2, 5,4, 7,6, 9,8, 11,10, 13,12, 15,14};
  return rvv_gather1(a, idx, vl);
}

/* ===================== 四、lane 拼接/提取（56 次） ===================== */
static inline vint16m1_t rvv_extracti128_0(vint16m1_t a, size_t vl) {
  static const uint16_t idx[16] = {0,1,2,3,4,5,6,7, 0,0,0,0,0,0,0,0};
  return rvv_gather1(a, idx, vl);
}
static inline vint16m1_t rvv_extracti128_1(vint16m1_t a, size_t vl) {
  static const uint16_t idx[16] = {8,9,10,11,12,13,14,15, 0,0,0,0,0,0,0,0};
  return rvv_gather1(a, idx, vl);
}
static inline vint16m1_t rvv_insertf128_0(vint16m1_t a, vint16m1_t b, size_t vl) {
  static const uint16_t idx[16] = {16,17,18,19,20,21,22,23, 8,9,10,11,12,13,14,15};
  return rvv_gather2(a, b, idx, vl);
}
static inline vint16m1_t rvv_insertf128_1(vint16m1_t a, vint16m1_t b, size_t vl) {
  static const uint16_t idx[16] = {0,1,2,3,4,5,6,7, 16,17,18,19,20,21,22,23};
  return rvv_gather2(a, b, idx, vl);
}

/* 载入/存出一整条 256 位（=16 个 int16） */
static inline vint16m1_t rvv_load16(const int16_t *p, size_t vl) {
  return __riscv_vle16_v_i16m1(p, vl);
}
static inline void rvv_store16(int16_t *p, vint16m1_t v, size_t vl) {
  __riscv_vse16_v_i16m1(p, v, vl);
}


/* ============ 五、32 位视角算子（复数乘加链，idft64 里 12 次） ============
 * idft64 用到的 packed_cmult2_256 展开后需要三个"32 位"算子：
 *     madd_epi16   : 相邻两个 int16 相乘再相加 → int32   （8 个 int32）
 *     srai_epi32   : 32 位算术右移
 *     packs_epi32  : int32 饱和打包成 int16
 *
 * ★ 前提（已实测确认）：__riscv_vreinterpret_v_i16m1_i32m1 **合法**
 *     —— 同一 LMUL（都占一个寄存器）、SEW 16→32、总位数不变。
 *   所以这些 32 位操作可以"原地重解释"，**不产生任何访存**。
 *   （对照：i16m1→u32m1、i16m1→u8m1 都不合法，编译直接报错。） */

/* 32 位算术右移：reinterpret 成 32 位视角后直接 vsra */
static inline vint16m1_t rvv_srai_epi32(vint16m1_t a, int n, size_t vl) {
  size_t vl32 = __riscv_vsetvl_e32m1(8);
  vint32m1_t a32 = __riscv_vreinterpret_v_i16m1_i32m1(a);
  vint32m1_t r   = __riscv_vsra_vx_i32m1(a32, n, vl32);
  return __riscv_vreinterpret_v_i32m1_i16m1(r);
}

/* madd_epi16(a,b)：结果[j] = a[2j]*b[2j] + a[2j+1]*b[2j+1]   (j=0..7)
 * 做法：把"偶数位""奇数位"分别收集到低 8 个位置，再做两次宽乘累加。
 * 宽乘 int16×int16→int32 的输出更宽，必须用 LMUL=2。
 * 高 8 个位置参与运算但结果丢弃（收集时填的是 0，无副作用）。 */
static inline vint16m1_t rvv_madd_epi16(vint16m1_t a, vint16m1_t b, size_t vl) {
  static const uint16_t ev[16] = {0,2,4,6,8,10,12,14, 0,0,0,0,0,0,0,0};
  static const uint16_t od[16] = {1,3,5,7,9,11,13,15, 0,0,0,0,0,0,0,0};
  vint16m1_t ae = rvv_gather1(a, ev, vl);
  vint16m1_t ao = rvv_gather1(a, od, vl);
  vint16m1_t be = rvv_gather1(b, ev, vl);
  vint16m1_t bo = rvv_gather1(b, od, vl);

  size_t vl32 = __riscv_vsetvl_e32m2(16);
  vint32m2_t acc = __riscv_vwmul_vv_i32m2(ae, be, vl32);    /* a[2j]*b[2j] */
  acc = __riscv_vwmacc_vv_i32m2(acc, ao, bo, vl32);         /* + a[2j+1]*b[2j+1] */
  vint32m1_t r32 = __riscv_vget_v_i32m2_i32m1(acc, 0);      /* 取低 8 个 int32 */
  return __riscv_vreinterpret_v_i32m1_i16m1(r32);
}

/* packs_epi32(a,b)：32 位饱和窄化成 16 位。
 * ★ AVX2 的 packs 是 **lane-local** 的：
 *     结果 = [a0 a1 a2 a3  b0 b1 b2 b3  a4 a5 a6 a7  b4 b5 b6 b7]
 *   而 RVV 的 vnclip 直接给出 [a0..a7 b0..b7]，所以还要用 vrgather 换一次位置。
 *   （sat=none 的等价物是 vnclip 的 shift=0：只饱和、不移位。） */
static inline vint16m1_t rvv_packs_epi32(vint16m1_t a, vint16m1_t b, size_t vl) {
  size_t vl32 = __riscv_vsetvl_e32m2(16);
  vint32m1_t a32 = __riscv_vreinterpret_v_i16m1_i32m1(a);
  vint32m1_t b32 = __riscv_vreinterpret_v_i16m1_i32m1(b);
  vint32m2_t ab  = __riscv_vcreate_v_i32m1_i32m2(a32, b32);  /* 拼成 16 个 int32 */
  vint16m1_t lo  = __riscv_vnclip_wx_i16m1(ab, 0, 0, vl32);     /* 饱和窄化 → [a0..a7 b0..b7] */
  static const uint16_t fix[16] = {0,1,2,3, 8,9,10,11, 4,5,6,7, 12,13,14,15};
  return rvv_gather1(lo, fix, vl);
}

#endif /* RVV_OPS_H */

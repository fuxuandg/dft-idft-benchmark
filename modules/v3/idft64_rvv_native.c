/*
 * Native RVV 64-point inverse DFT.
 *
 * This is deliberately not a transliteration of the AVX2 implementation.
 * It uses a radix-4 DIF dataflow and keeps real/imaginary components in
 * separate vectors.  The two inter-stage permutations are expressed as
 * strided stores into small, aligned scratch arrays.  All arithmetic remains
 * fixed-point Q15; saturating butterflies make the implementation robust for
 * the original int16_t interface, although the changed saturation/rounding
 * order is not expected to be bit-exact on every input.
 */

#include <riscv_vector.h>
#include <stddef.h>
#include <stdint.h>

#include "idft64_rvv.h"

#define ALIGN64 __attribute__((aligned(64)))

/* exp(+j*2*pi*p*n/64), n=0..15, p=1..3. */
static const int16_t W64_RE[3][16] ALIGN64 = {
  {32767,32609,32137,31356,30272,28897,27244,25329,
   23169,20787,18204,15446,12539, 9511, 6392, 3211},
  {32767,32137,30272,27244,23169,18204,12539, 6392,
       0,-6393,-12540,-18205,-23170,-27245,-30273,-32138},
  {32767,31356,27244,20787,12539, 3211,-6393,-15447,
   -23170,-28898,-32138,-32610,-30273,-25330,-18205,-9512},
};

static const int16_t W64_IM[3][16] ALIGN64 = {
  {    0, 3212, 6393, 9512,12540,15447,18205,20788,
   23170,25330,27245,28898,30273,31357,32138,32610},
  {    0, 6393,12540,18205,23170,27245,30273,32138,
   32767,32138,30273,27245,23170,18205,12540, 6393},
  {    0, 9512,18205,25330,30273,32610,32138,28898,
   23170,15447, 6393,-3211,-12539,-20787,-27244,-31356},
};

/* exp(+j*2*pi*p*j/16), j=0..3.  Each j is repeated for the
 * four first-stage radix-4 branches carried by adjacent lanes. */
static const int16_t W16_RE[3][16] ALIGN64 = {
  {32767,32767,32767,32767, 30272,30272,30272,30272,
   23169,23169,23169,23169, 12539,12539,12539,12539},
  {32767,32767,32767,32767, 23169,23169,23169,23169,
       0,    0,    0,    0,-23170,-23170,-23170,-23170},
  {32767,32767,32767,32767, 12539,12539,12539,12539,
  -23170,-23170,-23170,-23170,-30273,-30273,-30273,-30273},
};

static const int16_t W16_IM[3][16] ALIGN64 = {
  {0,0,0,0, 12540,12540,12540,12540,
   23170,23170,23170,23170, 30273,30273,30273,30273},
  {0,0,0,0, 23170,23170,23170,23170,
   32767,32767,32767,32767, 23170,23170,23170,23170},
  {0,0,0,0, 30273,30273,30273,30273,
   23170,23170,23170,23170,-12539,-12539,-12539,-12539},
};

static const uint16_t TRANSPOSE4[16] ALIGN64 = {
  0,4,8,12, 1,5,9,13, 2,6,10,14, 3,7,11,15
};

static inline vint16m1_t sat_add(vint16m1_t a, vint16m1_t b, size_t vl) {
  return __riscv_vsadd_vv_i16m1(a, b, vl);
}

static inline vint16m1_t sat_sub(vint16m1_t a, vint16m1_t b, size_t vl) {
  return __riscv_vssub_vv_i16m1(a, b, vl);
}

static inline void cmul_q15(vint16m1_t ar, vint16m1_t ai,
                            vint16m1_t wr, vint16m1_t wi,
                            vint16m1_t *out_r, vint16m1_t *out_i, size_t vl) {
  vint32m2_t rr = __riscv_vwmul_vv_i32m2(ar, wr, vl);
  rr = __riscv_vwmacc_vv_i32m2(rr, ai, __riscv_vneg_v_i16m1(wi, vl), vl);
  vint32m2_t ri = __riscv_vwmul_vv_i32m2(ar, wi, vl);
  ri = __riscv_vwmacc_vv_i32m2(ri, ai, wr, vl);
  *out_r = __riscv_vnclip_wx_i16m1(rr, 15, __RISCV_VXRM_RDN, vl);
  *out_i = __riscv_vnclip_wx_i16m1(ri, 15, __RISCV_VXRM_RDN, vl);
}

/* Inverse radix-4 butterfly: [a,b,c,d] -> frequency branches [0,1,2,3]. */
static inline void ibfly4(
    vint16m1_t ar, vint16m1_t ai, vint16m1_t br, vint16m1_t bi,
    vint16m1_t cr, vint16m1_t ci, vint16m1_t dr, vint16m1_t di,
    vint16m1_t *y0r, vint16m1_t *y0i, vint16m1_t *y1r, vint16m1_t *y1i,
    vint16m1_t *y2r, vint16m1_t *y2i, vint16m1_t *y3r, vint16m1_t *y3i,
    size_t vl) {
  vint16m1_t ac_r = sat_add(ar, cr, vl);
  vint16m1_t ac_i = sat_add(ai, ci, vl);
  vint16m1_t bd_r = sat_add(br, dr, vl);
  vint16m1_t bd_i = sat_add(bi, di, vl);
  vint16m1_t da_r = sat_sub(ar, cr, vl);
  vint16m1_t da_i = sat_sub(ai, ci, vl);
  vint16m1_t db_r = sat_sub(br, dr, vl);
  vint16m1_t db_i = sat_sub(bi, di, vl);

  *y0r = sat_add(ac_r, bd_r, vl);
  *y0i = sat_add(ac_i, bd_i, vl);
  *y2r = sat_sub(ac_r, bd_r, vl);
  *y2i = sat_sub(ac_i, bd_i, vl);

  /* da +/- j*db, where j*(r+j*i) = -i+j*r. */
  *y1r = sat_sub(da_r, db_i, vl);
  *y1i = sat_add(da_i, db_r, vl);
  *y3r = sat_add(da_r, db_i, vl);
  *y3i = sat_sub(da_i, db_r, vl);
}

static inline void apply_twiddles(vint16m1_t *y1r, vint16m1_t *y1i,
                                  vint16m1_t *y2r, vint16m1_t *y2i,
                                  vint16m1_t *y3r, vint16m1_t *y3i,
                                  const int16_t wr[3][16],
                                  const int16_t wi[3][16], size_t vl) {
  cmul_q15(*y1r, *y1i, __riscv_vle16_v_i16m1(wr[0], vl),
                        __riscv_vle16_v_i16m1(wi[0], vl), y1r, y1i, vl);
  cmul_q15(*y2r, *y2i, __riscv_vle16_v_i16m1(wr[1], vl),
                        __riscv_vle16_v_i16m1(wi[1], vl), y2r, y2i, vl);
  cmul_q15(*y3r, *y3i, __riscv_vle16_v_i16m1(wr[2], vl),
                        __riscv_vle16_v_i16m1(wi[2], vl), y3r, y3i, vl);
}

/* Store four branch vectors transposed: dst[4*n+r] = yr[n]. */
static inline void store4_transposed(int16_t *dst, vint16m1_t y0,
                                     vint16m1_t y1, vint16m1_t y2,
                                     vint16m1_t y3, size_t vl) {
  const ptrdiff_t stride = 4 * (ptrdiff_t)sizeof(int16_t);
  __riscv_vsse16_v_i16m1(dst + 0, stride, y0, vl);
  __riscv_vsse16_v_i16m1(dst + 1, stride, y1, vl);
  __riscv_vsse16_v_i16m1(dst + 2, stride, y2, vl);
  __riscv_vsse16_v_i16m1(dst + 3, stride, y3, vl);
}

void idft64_rvv(int16_t *x, int16_t *y, unsigned char scale) {
  const size_t vl = __riscv_vsetvl_e16m1(16);
  int16_t t1r[64] ALIGN64, t1i[64] ALIGN64;
  int16_t t2r[64] ALIGN64, t2i[64] ALIGN64;

  /* Stage 1: four contiguous 16-complex quarters. */
  vint16m1x2_t x0=__riscv_vlseg2e16_v_i16m1x2(x+0,vl);
  vint16m1x2_t x1=__riscv_vlseg2e16_v_i16m1x2(x+32,vl);
  vint16m1x2_t x2=__riscv_vlseg2e16_v_i16m1x2(x+64,vl);
  vint16m1x2_t x3=__riscv_vlseg2e16_v_i16m1x2(x+96,vl);
  vint16m1_t ar=__riscv_vget_v_i16m1x2_i16m1(x0,0), ai=__riscv_vget_v_i16m1x2_i16m1(x0,1);
  vint16m1_t br=__riscv_vget_v_i16m1x2_i16m1(x1,0), bi=__riscv_vget_v_i16m1x2_i16m1(x1,1);
  vint16m1_t cr=__riscv_vget_v_i16m1x2_i16m1(x2,0), ci=__riscv_vget_v_i16m1x2_i16m1(x2,1);
  vint16m1_t dr=__riscv_vget_v_i16m1x2_i16m1(x3,0), di=__riscv_vget_v_i16m1x2_i16m1(x3,1);
  vint16m1_t s10r,s10i,s11r,s11i,s12r,s12i,s13r,s13i;
  ibfly4(ar,ai,br,bi,cr,ci,dr,di,
         &s10r,&s10i,&s11r,&s11i,&s12r,&s12i,&s13r,&s13i,vl);
  apply_twiddles(&s11r,&s11i,&s12r,&s12i,&s13r,&s13i,W64_RE,W64_IM,vl);
  store4_transposed(t1r,s10r,s11r,s12r,s13r,vl);
  store4_transposed(t1i,s10i,s11i,s12i,s13i,vl);

  /* Stage 2: the first transpose makes all four inputs unit-stride. */
  ar=__riscv_vle16_v_i16m1(t1r+0,vl);  ai=__riscv_vle16_v_i16m1(t1i+0,vl);
  br=__riscv_vle16_v_i16m1(t1r+16,vl); bi=__riscv_vle16_v_i16m1(t1i+16,vl);
  cr=__riscv_vle16_v_i16m1(t1r+32,vl); ci=__riscv_vle16_v_i16m1(t1i+32,vl);
  dr=__riscv_vle16_v_i16m1(t1r+48,vl); di=__riscv_vle16_v_i16m1(t1i+48,vl);
  vint16m1_t s20r,s20i,s21r,s21i,s22r,s22i,s23r,s23i;
  ibfly4(ar,ai,br,bi,cr,ci,dr,di,
         &s20r,&s20i,&s21r,&s21i,&s22r,&s22i,&s23r,&s23i,vl);
  apply_twiddles(&s21r,&s21i,&s22r,&s22i,&s23r,&s23i,W16_RE,W16_IM,vl);
  store4_transposed(t2r,s20r,s21r,s22r,s23r,vl);
  store4_transposed(t2i,s20i,s21i,s22i,s23i,vl);

  /* Stage 3: no non-trivial twiddles remain. */
  ar=__riscv_vle16_v_i16m1(t2r+0,vl);  ai=__riscv_vle16_v_i16m1(t2i+0,vl);
  br=__riscv_vle16_v_i16m1(t2r+16,vl); bi=__riscv_vle16_v_i16m1(t2i+16,vl);
  cr=__riscv_vle16_v_i16m1(t2r+32,vl); ci=__riscv_vle16_v_i16m1(t2i+32,vl);
  dr=__riscv_vle16_v_i16m1(t2r+48,vl); di=__riscv_vle16_v_i16m1(t2i+48,vl);
  vint16m1_t o0r,o0i,o1r,o1i,o2r,o2i,o3r,o3i;
  ibfly4(ar,ai,br,bi,cr,ci,dr,di,
         &o0r,&o0i,&o1r,&o1i,&o2r,&o2i,&o3r,&o3i,vl);

  if(scale){
    o0r=__riscv_vsra_vx_i16m1(o0r,3,vl); o0i=__riscv_vsra_vx_i16m1(o0i,3,vl);
    o1r=__riscv_vsra_vx_i16m1(o1r,3,vl); o1i=__riscv_vsra_vx_i16m1(o1i,3,vl);
    o2r=__riscv_vsra_vx_i16m1(o2r,3,vl); o2i=__riscv_vsra_vx_i16m1(o2i,3,vl);
    o3r=__riscv_vsra_vx_i16m1(o3r,3,vl); o3i=__riscv_vsra_vx_i16m1(o3i,3,vl);
  }
  const vuint16m1_t tidx=__riscv_vle16_v_u16m1(TRANSPOSE4,vl);
  o0r=__riscv_vrgather_vv_i16m1(o0r,tidx,vl); o0i=__riscv_vrgather_vv_i16m1(o0i,tidx,vl);
  o1r=__riscv_vrgather_vv_i16m1(o1r,tidx,vl); o1i=__riscv_vrgather_vv_i16m1(o1i,tidx,vl);
  o2r=__riscv_vrgather_vv_i16m1(o2r,tidx,vl); o2i=__riscv_vrgather_vv_i16m1(o2i,tidx,vl);
  o3r=__riscv_vrgather_vv_i16m1(o3r,tidx,vl); o3i=__riscv_vrgather_vv_i16m1(o3i,tidx,vl);
  vint16m1x2_t q=x0;
  q=__riscv_vset_v_i16m1_i16m1x2(q,0,o0r); q=__riscv_vset_v_i16m1_i16m1x2(q,1,o0i);
  __riscv_vsseg2e16_v_i16m1x2(y+0,q,vl);
  q=__riscv_vset_v_i16m1_i16m1x2(q,0,o1r); q=__riscv_vset_v_i16m1_i16m1x2(q,1,o1i);
  __riscv_vsseg2e16_v_i16m1x2(y+32,q,vl);
  q=__riscv_vset_v_i16m1_i16m1x2(q,0,o2r); q=__riscv_vset_v_i16m1_i16m1x2(q,1,o2i);
  __riscv_vsseg2e16_v_i16m1x2(y+64,q,vl);
  q=__riscv_vset_v_i16m1_i16m1x2(q,0,o3r); q=__riscv_vset_v_i16m1_i16m1x2(q,1,o3i);
  __riscv_vsseg2e16_v_i16m1x2(y+96,q,vl);
}

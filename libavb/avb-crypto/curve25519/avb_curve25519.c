// TODO what copyright header should I use?

/**
 * This file is created from curve25519 related code in the boringssl.
 * Only EdDSA related functions and only
 * OPENSSL_SMALL, OPENSSL_32_BIT, OPENSSL_NO_ASM and
 * FIAT_25519_NO_ASM code is copied.
 */
#include "avb_curve25519.h"

#include <assert.h>
#include <stdint.h>

#include "../../avb_ops.h"
#include "../../avb_sha.h"
#include "../../avb_util.h"

// Following macros are used by the curve25519_32.h file. So if they are not
// defined in stdint.h then define them here.
#ifndef UINT8_C
#define UINT8_C(c) c
#endif
#ifndef UINT16_C
#define UINT16_C(c) c
#endif
#ifndef UINT32_C
#define UINT32_C(c) c##U
#endif
#ifndef UINT64_C
#define UINT64_C(c) c##UL
#endif
#ifndef __INT64_C
#define __INT64_C(c) c##L
#endif
#ifndef __UINT64_C
#define __UINT64_C(c) c##UL
#endif

#ifndef INT8_C
#define INT8_C(c) c
#endif
#ifndef INT16_C
#define INT16_C(c) c
#endif
#ifndef INT32_C
#define INT32_C(c) c
#endif
#ifndef INT64_C
#define INT64_C(c) c##L
#endif
#ifndef UINT8_C
#define UINT8_C(c) c
#endif
#ifndef UINT16_C
#define UINT16_C(c) c
#endif
#ifndef UINT32_C
#define UINT32_C(c) c##U
#endif
#ifndef UINT64_C
#define UINT64_C(c) c##UL
#endif

#include "curve25519_32.h"
#include "curve25519_tables_32.h"

#define SHA512_DIGEST_LENGTH AVB_SHA512_DIGEST_SIZE
#define SHA512_CTX AvbSHA512Ctx
static void SHA512(const uint8_t* data,
                   size_t len,
                   uint8_t out[AVB_SHA512_DIGEST_SIZE]) {
  AvbSHA512Ctx ctx;
  avb_sha512_init(&ctx);
  avb_sha512_update(&ctx, data, len);
  memcpy(out, avb_sha512_final(&ctx), AVB_SHA512_DIGEST_SIZE);
}

// Low-level intrinsic operations
static uint64_t load_3(const uint8_t* in) {
  uint64_t result;
  result = (uint64_t)in[0];
  result |= ((uint64_t)in[1]) << 8;
  result |= ((uint64_t)in[2]) << 16;
  return result;
}

static uint64_t load_4(const uint8_t* in) {
  uint64_t result;
  result = (uint64_t)in[0];
  result |= ((uint64_t)in[1]) << 8;
  result |= ((uint64_t)in[2]) << 16;
  result |= ((uint64_t)in[3]) << 24;
  return result;
}

// Field operations - 32 bit
typedef uint32_t fe_limb_t;
#define FE_NUM_LIMBS 10

// assert_fe asserts that |f| satisfies bounds:
//
//  [[0x0 ~> 0x4666666], [0x0 ~> 0x2333333],
//   [0x0 ~> 0x4666666], [0x0 ~> 0x2333333],
//   [0x0 ~> 0x4666666], [0x0 ~> 0x2333333],
//   [0x0 ~> 0x4666666], [0x0 ~> 0x2333333],
//   [0x0 ~> 0x4666666], [0x0 ~> 0x2333333]]
//
// See comments in curve25519_32.h for which functions use these bounds for
// inputs or outputs.
#define assert_fe(f)                                                     \
  do {                                                                   \
    for (unsigned _assert_fe_i = 0; _assert_fe_i < 10; _assert_fe_i++) { \
      assert(f[_assert_fe_i] <=                                          \
             ((_assert_fe_i & 1) ? 0x2333333u : 0x4666666u));            \
    }                                                                    \
  } while (0)

// assert_fe_loose asserts that |f| satisfies bounds:
//
//  [[0x0 ~> 0xd333332], [0x0 ~> 0x6999999],
//   [0x0 ~> 0xd333332], [0x0 ~> 0x6999999],
//   [0x0 ~> 0xd333332], [0x0 ~> 0x6999999],
//   [0x0 ~> 0xd333332], [0x0 ~> 0x6999999],
//   [0x0 ~> 0xd333332], [0x0 ~> 0x6999999]]
//
// See comments in curve25519_32.h for which functions use these bounds for
// inputs or outputs.
#define assert_fe_loose(f)                                               \
  do {                                                                   \
    for (unsigned _assert_fe_i = 0; _assert_fe_i < 10; _assert_fe_i++) { \
      assert(f[_assert_fe_i] <=                                          \
             ((_assert_fe_i & 1) ? 0x6999999u : 0xd333332u));            \
    }                                                                    \
  } while (0)

static_assert(sizeof(fe) == sizeof(fe_limb_t) * FE_NUM_LIMBS,
              "fe_limb_t[FE_NUM_LIMBS] is inconsistent with fe");

static void fe_frombytes_strict(fe* h, const uint8_t s[32]) {
  // |fiat_25519_from_bytes| requires the top-most bit be clear.
  assert((s[31] & 0x80) == 0);
  fiat_25519_from_bytes(h->v, s);
  assert_fe(h->v);
}

static void fe_frombytes(fe* h, const uint8_t s[32]) {
  uint8_t s_copy[32];
  memcpy(s_copy, s, 32);
  s_copy[31] &= 0x7f;
  fe_frombytes_strict(h, s_copy);
}

static void fe_tobytes(uint8_t s[32], const fe* f) {
  assert_fe(f->v);
  fiat_25519_to_bytes(s, f->v);
}

// h = 0
static void fe_0(fe* h) {
  memset(h, 0, sizeof(fe));
}

static void fe_loose_0(fe_loose* h) {
  memset(h, 0, sizeof(fe_loose));
}

// h = 1
static void fe_1(fe* h) {
  memset(h, 0, sizeof(fe));
  h->v[0] = 1;
}

static void fe_loose_1(fe_loose* h) {
  memset(h, 0, sizeof(fe_loose));
  h->v[0] = 1;
}

// h = f + g
// Can overlap h with f or g.
static void fe_add(fe_loose* h, const fe* f, const fe* g) {
  assert_fe(f->v);
  assert_fe(g->v);
  fiat_25519_add(h->v, f->v, g->v);
  assert_fe_loose(h->v);
}

// h = f - g
// Can overlap h with f or g.
static void fe_sub(fe_loose* h, const fe* f, const fe* g) {
  assert_fe(f->v);
  assert_fe(g->v);
  fiat_25519_sub(h->v, f->v, g->v);
  assert_fe_loose(h->v);
}

static void fe_carry(fe* h, const fe_loose* f) {
  assert_fe_loose(f->v);
  fiat_25519_carry(h->v, f->v);
  assert_fe(h->v);
}

static void fe_mul_impl(fe_limb_t out[FE_NUM_LIMBS],
                        const fe_limb_t in1[FE_NUM_LIMBS],
                        const fe_limb_t in2[FE_NUM_LIMBS]) {
  assert_fe_loose(in1);
  assert_fe_loose(in2);
  fiat_25519_carry_mul(out, in1, in2);
  assert_fe(out);
}

static void fe_mul_ltt(fe_loose* h, const fe* f, const fe* g) {
  fe_mul_impl(h->v, f->v, g->v);
}

static void fe_mul_llt(fe_loose* h, const fe_loose* f, const fe* g) {
  fe_mul_impl(h->v, f->v, g->v);
}

static void fe_mul_ttt(fe* h, const fe* f, const fe* g) {
  fe_mul_impl(h->v, f->v, g->v);
}

static void fe_mul_tlt(fe* h, const fe_loose* f, const fe* g) {
  fe_mul_impl(h->v, f->v, g->v);
}

static void fe_mul_ttl(fe* h, const fe* f, const fe_loose* g) {
  fe_mul_impl(h->v, f->v, g->v);
}

static void fe_mul_tll(fe* h, const fe_loose* f, const fe_loose* g) {
  fe_mul_impl(h->v, f->v, g->v);
}

static void fe_sq_tl(fe* h, const fe_loose* f) {
  assert_fe_loose(f->v);
  fiat_25519_carry_square(h->v, f->v);
  assert_fe(h->v);
}

static void fe_sq_tt(fe* h, const fe* f) {
  assert_fe_loose(f->v);
  fiat_25519_carry_square(h->v, f->v);
  assert_fe(h->v);
}

// Replace (f,g) with (g,f) if b == 1;
// replace (f,g) with (f,g) if b == 0.
//
// Preconditions: b in {0,1}.
static void fe_cswap(fe* f, fe* g, fe_limb_t b) {
  b = 0 - b;
  for (unsigned i = 0; i < FE_NUM_LIMBS; i++) {
    fe_limb_t x = f->v[i] ^ g->v[i];
    x &= b;
    f->v[i] ^= x;
    g->v[i] ^= x;
  }
}

static void fe_mul121666(fe* h, const fe_loose* f) {
  assert_fe_loose(f->v);
  fiat_25519_carry_scmul_121666(h->v, f->v);
  assert_fe(h->v);
}

// h = -f
static void fe_neg(fe_loose* h, const fe* f) {
  assert_fe(f->v);
  fiat_25519_opp(h->v, f->v);
  assert_fe_loose(h->v);
}

// Replace (f,g) with (g,g) if b == 1;
// replace (f,g) with (f,g) if b == 0.
//
// Preconditions: b in {0,1}.
static void fe_cmov(fe_loose* f, const fe_loose* g, fe_limb_t b) {
  // Silence an unused function warning. |fiat_25519_selectznz| isn't quite the
  // calling convention the rest of this code wants, so implement it by hand.
  //
  // TODO(davidben): Switch to fiat's calling convention, or ask fiat to emit a
  // different one.
  //(void)fiat_25519_selectznz;

  b = 0 - b;
  for (unsigned i = 0; i < FE_NUM_LIMBS; i++) {
    fe_limb_t x = f->v[i] ^ g->v[i];
    x &= b;
    f->v[i] ^= x;
  }
}

// h = f
static void fe_copy(fe* h, const fe* f) {
  memmove(h, f, sizeof(fe));
}

static void fe_copy_lt(fe_loose* h, const fe* f) {
  static_assert(sizeof(fe_loose) == sizeof(fe), "fe and fe_loose mismatch");
  memmove(h, f, sizeof(fe));
}

static void fe_loose_invert(fe* out, const fe_loose* z) {
  fe t0;
  fe t1;
  fe t2;
  fe t3;
  int i;

  fe_sq_tl(&t0, z);
  fe_sq_tt(&t1, &t0);
  for (i = 1; i < 2; ++i) {
    fe_sq_tt(&t1, &t1);
  }
  fe_mul_tlt(&t1, z, &t1);
  fe_mul_ttt(&t0, &t0, &t1);
  fe_sq_tt(&t2, &t0);
  fe_mul_ttt(&t1, &t1, &t2);
  fe_sq_tt(&t2, &t1);
  for (i = 1; i < 5; ++i) {
    fe_sq_tt(&t2, &t2);
  }
  fe_mul_ttt(&t1, &t2, &t1);
  fe_sq_tt(&t2, &t1);
  for (i = 1; i < 10; ++i) {
    fe_sq_tt(&t2, &t2);
  }
  fe_mul_ttt(&t2, &t2, &t1);
  fe_sq_tt(&t3, &t2);
  for (i = 1; i < 20; ++i) {
    fe_sq_tt(&t3, &t3);
  }
  fe_mul_ttt(&t2, &t3, &t2);
  fe_sq_tt(&t2, &t2);
  for (i = 1; i < 10; ++i) {
    fe_sq_tt(&t2, &t2);
  }
  fe_mul_ttt(&t1, &t2, &t1);
  fe_sq_tt(&t2, &t1);
  for (i = 1; i < 50; ++i) {
    fe_sq_tt(&t2, &t2);
  }
  fe_mul_ttt(&t2, &t2, &t1);
  fe_sq_tt(&t3, &t2);
  for (i = 1; i < 100; ++i) {
    fe_sq_tt(&t3, &t3);
  }
  fe_mul_ttt(&t2, &t3, &t2);
  fe_sq_tt(&t2, &t2);
  for (i = 1; i < 50; ++i) {
    fe_sq_tt(&t2, &t2);
  }
  fe_mul_ttt(&t1, &t2, &t1);
  fe_sq_tt(&t1, &t1);
  for (i = 1; i < 5; ++i) {
    fe_sq_tt(&t1, &t1);
  }
  fe_mul_ttt(out, &t1, &t0);
}

static void fe_invert(fe* out, const fe* z) {
  fe_loose l;
  fe_copy_lt(&l, z);
  fe_loose_invert(out, &l);
}

// return 0 if f == 0
// return 1 if f != 0
static int fe_isnonzero(const fe_loose* f) {
  fe tight;
  fe_carry(&tight, f);
  uint8_t s[32];
  fe_tobytes(s, &tight);

  static const uint8_t zero[32] = {0};
  return memcmp(s, zero, sizeof(zero)) != 0;
}

// return 1 if f is in {1,3,5,...,q-2}
// return 0 if f is in {0,2,4,...,q-1}
static int fe_isnegative(const fe* f) {
  uint8_t s[32];
  fe_tobytes(s, f);
  return s[0] & 1;
}

static void fe_sq2_tt(fe* h, const fe* f) {
  // h = f^2
  fe_sq_tt(h, f);

  // h = h + h
  fe_loose tmp;
  fe_add(&tmp, h, h);
  fe_carry(h, &tmp);
}

static void fe_pow22523(fe* out, const fe* z) {
  fe t0;
  fe t1;
  fe t2;
  int i;

  fe_sq_tt(&t0, z);
  fe_sq_tt(&t1, &t0);
  for (i = 1; i < 2; ++i) {
    fe_sq_tt(&t1, &t1);
  }
  fe_mul_ttt(&t1, z, &t1);
  fe_mul_ttt(&t0, &t0, &t1);
  fe_sq_tt(&t0, &t0);
  fe_mul_ttt(&t0, &t1, &t0);
  fe_sq_tt(&t1, &t0);
  for (i = 1; i < 5; ++i) {
    fe_sq_tt(&t1, &t1);
  }
  fe_mul_ttt(&t0, &t1, &t0);
  fe_sq_tt(&t1, &t0);
  for (i = 1; i < 10; ++i) {
    fe_sq_tt(&t1, &t1);
  }
  fe_mul_ttt(&t1, &t1, &t0);
  fe_sq_tt(&t2, &t1);
  for (i = 1; i < 20; ++i) {
    fe_sq_tt(&t2, &t2);
  }
  fe_mul_ttt(&t1, &t2, &t1);
  fe_sq_tt(&t1, &t1);
  for (i = 1; i < 10; ++i) {
    fe_sq_tt(&t1, &t1);
  }
  fe_mul_ttt(&t0, &t1, &t0);
  fe_sq_tt(&t1, &t0);
  for (i = 1; i < 50; ++i) {
    fe_sq_tt(&t1, &t1);
  }
  fe_mul_ttt(&t1, &t1, &t0);
  fe_sq_tt(&t2, &t1);
  for (i = 1; i < 100; ++i) {
    fe_sq_tt(&t2, &t2);
  }
  fe_mul_ttt(&t1, &t2, &t1);
  fe_sq_tt(&t1, &t1);
  for (i = 1; i < 50; ++i) {
    fe_sq_tt(&t1, &t1);
  }
  fe_mul_ttt(&t0, &t1, &t0);
  fe_sq_tt(&t0, &t0);
  for (i = 1; i < 2; ++i) {
    fe_sq_tt(&t0, &t0);
  }
  fe_mul_ttt(out, &t0, z);
}

// Group operations.

void x25519_ge_tobytes(uint8_t s[32], const ge_p2* h) {
  fe recip;
  fe x;
  fe y;

  fe_invert(&recip, &h->Z);
  fe_mul_ttt(&x, &h->X, &recip);
  fe_mul_ttt(&y, &h->Y, &recip);
  fe_tobytes(s, &y);
  s[31] ^= fe_isnegative(&x) << 7;
}

static void ge_p3_tobytes(uint8_t s[32], const ge_p3* h) {
  fe recip;
  fe x;
  fe y;

  fe_invert(&recip, &h->Z);
  fe_mul_ttt(&x, &h->X, &recip);
  fe_mul_ttt(&y, &h->Y, &recip);
  fe_tobytes(s, &y);
  s[31] ^= fe_isnegative(&x) << 7;
}

int x25519_ge_frombytes_vartime(ge_p3* h, const uint8_t s[32]) {
  fe u;
  fe_loose v;
  fe w;
  fe vxx;
  fe_loose check;

  fe_frombytes(&h->Y, s);
  fe_1(&h->Z);
  fe_sq_tt(&w, &h->Y);
  fe_mul_ttt(&vxx, &w, &d);
  fe_sub(&v, &w, &h->Z);  // u = y^2-1
  fe_carry(&u, &v);
  fe_add(&v, &vxx, &h->Z);  // v = dy^2+1

  fe_mul_ttl(&w, &u, &v);        // w = u*v
  fe_pow22523(&h->X, &w);        // x = w^((q-5)/8)
  fe_mul_ttt(&h->X, &h->X, &u);  // x = u*w^((q-5)/8)

  fe_sq_tt(&vxx, &h->X);
  fe_mul_ttl(&vxx, &vxx, &v);
  fe_sub(&check, &vxx, &u);
  if (fe_isnonzero(&check)) {
    fe_add(&check, &vxx, &u);
    if (fe_isnonzero(&check)) {
      return 0;
    }
    fe_mul_ttt(&h->X, &h->X, &sqrtm1);
  }

  if (fe_isnegative(&h->X) != (s[31] >> 7)) {
    fe_loose t;
    fe_neg(&t, &h->X);
    fe_carry(&h->X, &t);
  }

  fe_mul_ttt(&h->T, &h->X, &h->Y);
  return 1;
}

static void ge_p2_0(ge_p2* h) {
  fe_0(&h->X);
  fe_1(&h->Y);
  fe_1(&h->Z);
}

static void ge_p3_0(ge_p3* h) {
  fe_0(&h->X);
  fe_1(&h->Y);
  fe_1(&h->Z);
  fe_0(&h->T);
}

static void ge_cached_0(ge_cached* h) {
  fe_loose_1(&h->YplusX);
  fe_loose_1(&h->YminusX);
  fe_loose_1(&h->Z);
  fe_loose_0(&h->T2d);
}

static void ge_precomp_0(ge_precomp* h) {
  fe_loose_1(&h->yplusx);
  fe_loose_1(&h->yminusx);
  fe_loose_0(&h->xy2d);
}

// r = p
static void ge_p3_to_p2(ge_p2* r, const ge_p3* p) {
  fe_copy(&r->X, &p->X);
  fe_copy(&r->Y, &p->Y);
  fe_copy(&r->Z, &p->Z);
}

// r = p
void x25519_ge_p3_to_cached(ge_cached* r, const ge_p3* p) {
  fe_add(&r->YplusX, &p->Y, &p->X);
  fe_sub(&r->YminusX, &p->Y, &p->X);
  fe_copy_lt(&r->Z, &p->Z);
  fe_mul_ltt(&r->T2d, &p->T, &d2);
}

// r = p
void x25519_ge_p1p1_to_p2(ge_p2* r, const ge_p1p1* p) {
  fe_mul_tll(&r->X, &p->X, &p->T);
  fe_mul_tll(&r->Y, &p->Y, &p->Z);
  fe_mul_tll(&r->Z, &p->Z, &p->T);
}

// r = p
void x25519_ge_p1p1_to_p3(ge_p3* r, const ge_p1p1* p) {
  fe_mul_tll(&r->X, &p->X, &p->T);
  fe_mul_tll(&r->Y, &p->Y, &p->Z);
  fe_mul_tll(&r->Z, &p->Z, &p->T);
  fe_mul_tll(&r->T, &p->X, &p->Y);
}

// r = p
static void ge_p1p1_to_cached(ge_cached* r, const ge_p1p1* p) {
  ge_p3 t;
  x25519_ge_p1p1_to_p3(&t, p);
  x25519_ge_p3_to_cached(r, &t);
}

// r = 2 * p
static void ge_p2_dbl(ge_p1p1* r, const ge_p2* p) {
  fe trX, trZ, trT;
  fe t0;

  fe_sq_tt(&trX, &p->X);
  fe_sq_tt(&trZ, &p->Y);
  fe_sq2_tt(&trT, &p->Z);
  fe_add(&r->Y, &p->X, &p->Y);
  fe_sq_tl(&t0, &r->Y);

  fe_add(&r->Y, &trZ, &trX);
  fe_sub(&r->Z, &trZ, &trX);
  fe_carry(&trZ, &r->Y);
  fe_sub(&r->X, &t0, &trZ);
  fe_carry(&trZ, &r->Z);
  fe_sub(&r->T, &trT, &trZ);
}

// r = 2 * p
static void ge_p3_dbl(ge_p1p1* r, const ge_p3* p) {
  ge_p2 q;
  ge_p3_to_p2(&q, p);
  ge_p2_dbl(r, &q);
}

// r = p + q
static void ge_madd(ge_p1p1* r, const ge_p3* p, const ge_precomp* q) {
  fe trY, trZ, trT;

  fe_add(&r->X, &p->Y, &p->X);
  fe_sub(&r->Y, &p->Y, &p->X);
  fe_mul_tll(&trZ, &r->X, &q->yplusx);
  fe_mul_tll(&trY, &r->Y, &q->yminusx);
  fe_mul_tlt(&trT, &q->xy2d, &p->T);
  fe_add(&r->T, &p->Z, &p->Z);
  fe_sub(&r->X, &trZ, &trY);
  fe_add(&r->Y, &trZ, &trY);
  fe_carry(&trZ, &r->T);
  fe_add(&r->Z, &trZ, &trT);
  fe_sub(&r->T, &trZ, &trT);
}

// r = p - q
static void ge_msub(ge_p1p1* r, const ge_p3* p, const ge_precomp* q) {
  fe trY, trZ, trT;

  fe_add(&r->X, &p->Y, &p->X);
  fe_sub(&r->Y, &p->Y, &p->X);
  fe_mul_tll(&trZ, &r->X, &q->yminusx);
  fe_mul_tll(&trY, &r->Y, &q->yplusx);
  fe_mul_tlt(&trT, &q->xy2d, &p->T);
  fe_add(&r->T, &p->Z, &p->Z);
  fe_sub(&r->X, &trZ, &trY);
  fe_add(&r->Y, &trZ, &trY);
  fe_carry(&trZ, &r->T);
  fe_sub(&r->Z, &trZ, &trT);
  fe_add(&r->T, &trZ, &trT);
}

// r = p + q
void x25519_ge_add(ge_p1p1* r, const ge_p3* p, const ge_cached* q) {
  fe trX, trY, trZ, trT;

  fe_add(&r->X, &p->Y, &p->X);
  fe_sub(&r->Y, &p->Y, &p->X);
  fe_mul_tll(&trZ, &r->X, &q->YplusX);
  fe_mul_tll(&trY, &r->Y, &q->YminusX);
  fe_mul_tlt(&trT, &q->T2d, &p->T);
  fe_mul_ttl(&trX, &p->Z, &q->Z);
  fe_add(&r->T, &trX, &trX);
  fe_sub(&r->X, &trZ, &trY);
  fe_add(&r->Y, &trZ, &trY);
  fe_carry(&trZ, &r->T);
  fe_add(&r->Z, &trZ, &trT);
  fe_sub(&r->T, &trZ, &trT);
}

// r = p - q
void x25519_ge_sub(ge_p1p1* r, const ge_p3* p, const ge_cached* q) {
  fe trX, trY, trZ, trT;

  fe_add(&r->X, &p->Y, &p->X);
  fe_sub(&r->Y, &p->Y, &p->X);
  fe_mul_tll(&trZ, &r->X, &q->YminusX);
  fe_mul_tll(&trY, &r->Y, &q->YplusX);
  fe_mul_tlt(&trT, &q->T2d, &p->T);
  fe_mul_ttl(&trX, &p->Z, &q->Z);
  fe_add(&r->T, &trX, &trX);
  fe_sub(&r->X, &trZ, &trY);
  fe_add(&r->Y, &trZ, &trY);
  fe_carry(&trZ, &r->T);
  fe_sub(&r->Z, &trZ, &trT);
  fe_add(&r->T, &trZ, &trT);
}

static void cmov(ge_precomp* t, const ge_precomp* u, uint8_t b) {
  fe_cmov(&t->yplusx, &u->yplusx, b);
  fe_cmov(&t->yminusx, &u->yminusx, b);
  fe_cmov(&t->xy2d, &u->xy2d, b);
}

void x25519_ge_scalarmult_small_precomp(
    ge_p3* h, const uint8_t a[32], const uint8_t precomp_table[15 * 2 * 32]) {
  // precomp_table is first expanded into matching |ge_precomp|
  // elements.
  ge_precomp multiples[15];

  unsigned i;
  for (i = 0; i < 15; i++) {
    // The precomputed table is assumed to already clear the top bit, so
    // |fe_frombytes_strict| may be used directly.
    const uint8_t* bytes = &precomp_table[i * (2 * 32)];
    fe x, y;
    fe_frombytes_strict(&x, bytes);
    fe_frombytes_strict(&y, bytes + 32);

    ge_precomp* out = &multiples[i];
    fe_add(&out->yplusx, &y, &x);
    fe_sub(&out->yminusx, &y, &x);
    fe_mul_ltt(&out->xy2d, &x, &y);
    fe_mul_llt(&out->xy2d, &out->xy2d, &d2);
  }

  // See the comment above |k25519SmallPrecomp| about the structure of the
  // precomputed elements. This loop does 64 additions and 64 doublings to
  // calculate the result.
  ge_p3_0(h);

  for (i = 63; i < 64; i--) {
    unsigned j;
    signed char index = 0;

    for (j = 0; j < 4; j++) {
      const uint8_t bit = 1 & (a[(8 * j) + (i / 8)] >> (i & 7));
      index |= (bit << j);
    }

    ge_precomp e;
    ge_precomp_0(&e);

    for (j = 1; j < 16; j++) {
      cmov(&e, &multiples[j - 1], 1 & constant_time_eq_w(index, j));
    }

    ge_cached cached;
    ge_p1p1 r;
    x25519_ge_p3_to_cached(&cached, h);
    x25519_ge_add(&r, h, &cached);
    x25519_ge_p1p1_to_p3(h, &r);

    ge_madd(&r, h, &e);
    x25519_ge_p1p1_to_p3(h, &r);
  }
}

void x25519_ge_scalarmult_base(ge_p3* h, const uint8_t a[32]) {
  x25519_ge_scalarmult_small_precomp(h, a, k25519SmallPrecomp);
}

static void cmov_cached(ge_cached* t, ge_cached* u, uint8_t b) {
  fe_cmov(&t->YplusX, &u->YplusX, b);
  fe_cmov(&t->YminusX, &u->YminusX, b);
  fe_cmov(&t->Z, &u->Z, b);
  fe_cmov(&t->T2d, &u->T2d, b);
}

// r = scalar * A.
// where a = a[0]+256*a[1]+...+256^31 a[31].
void x25519_ge_scalarmult(ge_p2* r, const uint8_t* scalar, const ge_p3* A) {
  ge_p2 Ai_p2[8];
  ge_cached Ai[16];
  ge_p1p1 t;

  ge_cached_0(&Ai[0]);
  x25519_ge_p3_to_cached(&Ai[1], A);
  ge_p3_to_p2(&Ai_p2[1], A);

  unsigned i;
  for (i = 2; i < 16; i += 2) {
    ge_p2_dbl(&t, &Ai_p2[i / 2]);
    ge_p1p1_to_cached(&Ai[i], &t);
    if (i < 8) {
      x25519_ge_p1p1_to_p2(&Ai_p2[i], &t);
    }
    x25519_ge_add(&t, A, &Ai[i]);
    ge_p1p1_to_cached(&Ai[i + 1], &t);
    if (i < 7) {
      x25519_ge_p1p1_to_p2(&Ai_p2[i + 1], &t);
    }
  }

  ge_p2_0(r);
  ge_p3 u;

  for (i = 0; i < 256; i += 4) {
    ge_p2_dbl(&t, r);
    x25519_ge_p1p1_to_p2(r, &t);
    ge_p2_dbl(&t, r);
    x25519_ge_p1p1_to_p2(r, &t);
    ge_p2_dbl(&t, r);
    x25519_ge_p1p1_to_p2(r, &t);
    ge_p2_dbl(&t, r);
    x25519_ge_p1p1_to_p3(&u, &t);

    uint8_t index = scalar[31 - i / 8];
    index >>= 4 - (i & 4);
    index &= 0xf;

    unsigned j;
    ge_cached selected;
    ge_cached_0(&selected);
    for (j = 0; j < 16; j++) {
      cmov_cached(&selected, &Ai[j], 1 & constant_time_eq_w(index, j));
    }

    x25519_ge_add(&t, &u, &selected);
    x25519_ge_p1p1_to_p2(r, &t);
  }
}

static void slide(signed char* r, const uint8_t* a) {
  int i;
  int b;
  int k;

  for (i = 0; i < 256; ++i) {
    r[i] = 1 & (a[i >> 3] >> (i & 7));
  }

  for (i = 0; i < 256; ++i) {
    if (r[i]) {
      for (b = 1; b <= 6 && i + b < 256; ++b) {
        if (r[i + b]) {
          if (r[i] + (r[i + b] << b) <= 15) {
            r[i] += r[i + b] << b;
            r[i + b] = 0;
          } else if (r[i] - (r[i + b] << b) >= -15) {
            r[i] -= r[i + b] << b;
            for (k = i + b; k < 256; ++k) {
              if (!r[k]) {
                r[k] = 1;
                break;
              }
              r[k] = 0;
            }
          } else {
            break;
          }
        }
      }
    }
  }
}

// r = a * A + b * B
// where a = a[0]+256*a[1]+...+256^31 a[31].
// and b = b[0]+256*b[1]+...+256^31 b[31].
// B is the Ed25519 base point (x,4/5) with x positive.
static void ge_double_scalarmult_vartime(ge_p2* r,
                                         const uint8_t* a,
                                         const ge_p3* A,
                                         const uint8_t* b) {
  signed char aslide[256];
  signed char bslide[256];
  ge_cached Ai[8];  // A,3A,5A,7A,9A,11A,13A,15A
  ge_p1p1 t;
  ge_p3 u;
  ge_p3 A2;
  int i;

  slide(aslide, a);
  slide(bslide, b);

  x25519_ge_p3_to_cached(&Ai[0], A);
  ge_p3_dbl(&t, A);
  x25519_ge_p1p1_to_p3(&A2, &t);
  x25519_ge_add(&t, &A2, &Ai[0]);
  x25519_ge_p1p1_to_p3(&u, &t);
  x25519_ge_p3_to_cached(&Ai[1], &u);
  x25519_ge_add(&t, &A2, &Ai[1]);
  x25519_ge_p1p1_to_p3(&u, &t);
  x25519_ge_p3_to_cached(&Ai[2], &u);
  x25519_ge_add(&t, &A2, &Ai[2]);
  x25519_ge_p1p1_to_p3(&u, &t);
  x25519_ge_p3_to_cached(&Ai[3], &u);
  x25519_ge_add(&t, &A2, &Ai[3]);
  x25519_ge_p1p1_to_p3(&u, &t);
  x25519_ge_p3_to_cached(&Ai[4], &u);
  x25519_ge_add(&t, &A2, &Ai[4]);
  x25519_ge_p1p1_to_p3(&u, &t);
  x25519_ge_p3_to_cached(&Ai[5], &u);
  x25519_ge_add(&t, &A2, &Ai[5]);
  x25519_ge_p1p1_to_p3(&u, &t);
  x25519_ge_p3_to_cached(&Ai[6], &u);
  x25519_ge_add(&t, &A2, &Ai[6]);
  x25519_ge_p1p1_to_p3(&u, &t);
  x25519_ge_p3_to_cached(&Ai[7], &u);

  ge_p2_0(r);

  for (i = 255; i >= 0; --i) {
    if (aslide[i] || bslide[i]) {
      break;
    }
  }

  for (; i >= 0; --i) {
    ge_p2_dbl(&t, r);

    if (aslide[i] > 0) {
      x25519_ge_p1p1_to_p3(&u, &t);
      x25519_ge_add(&t, &u, &Ai[aslide[i] / 2]);
    } else if (aslide[i] < 0) {
      x25519_ge_p1p1_to_p3(&u, &t);
      x25519_ge_sub(&t, &u, &Ai[(-aslide[i]) / 2]);
    }

    if (bslide[i] > 0) {
      x25519_ge_p1p1_to_p3(&u, &t);
      ge_madd(&t, &u, &Bi[bslide[i] / 2]);
    } else if (bslide[i] < 0) {
      x25519_ge_p1p1_to_p3(&u, &t);
      ge_msub(&t, &u, &Bi[(-bslide[i]) / 2]);
    }

    x25519_ge_p1p1_to_p2(r, &t);
  }
}

// int64_lshift21 returns |a << 21| but is defined when shifting bits into the
// sign bit. This works around a language flaw in C.
static inline int64_t int64_lshift21(int64_t a) {
  return (int64_t)((uint64_t)a << 21);
}

// The set of scalars is \Z/l
// where l = 2^252 + 27742317777372353535851937790883648493.

// Input:
//   s[0]+256*s[1]+...+256^63*s[63] = s
//
// Output:
//   s[0]+256*s[1]+...+256^31*s[31] = s mod l
//   where l = 2^252 + 27742317777372353535851937790883648493.
//   Overwrites s in place.
void x25519_sc_reduce(uint8_t s[64]) {
  int64_t s0 = 2097151 & load_3(s);
  int64_t s1 = 2097151 & (load_4(s + 2) >> 5);
  int64_t s2 = 2097151 & (load_3(s + 5) >> 2);
  int64_t s3 = 2097151 & (load_4(s + 7) >> 7);
  int64_t s4 = 2097151 & (load_4(s + 10) >> 4);
  int64_t s5 = 2097151 & (load_3(s + 13) >> 1);
  int64_t s6 = 2097151 & (load_4(s + 15) >> 6);
  int64_t s7 = 2097151 & (load_3(s + 18) >> 3);
  int64_t s8 = 2097151 & load_3(s + 21);
  int64_t s9 = 2097151 & (load_4(s + 23) >> 5);
  int64_t s10 = 2097151 & (load_3(s + 26) >> 2);
  int64_t s11 = 2097151 & (load_4(s + 28) >> 7);
  int64_t s12 = 2097151 & (load_4(s + 31) >> 4);
  int64_t s13 = 2097151 & (load_3(s + 34) >> 1);
  int64_t s14 = 2097151 & (load_4(s + 36) >> 6);
  int64_t s15 = 2097151 & (load_3(s + 39) >> 3);
  int64_t s16 = 2097151 & load_3(s + 42);
  int64_t s17 = 2097151 & (load_4(s + 44) >> 5);
  int64_t s18 = 2097151 & (load_3(s + 47) >> 2);
  int64_t s19 = 2097151 & (load_4(s + 49) >> 7);
  int64_t s20 = 2097151 & (load_4(s + 52) >> 4);
  int64_t s21 = 2097151 & (load_3(s + 55) >> 1);
  int64_t s22 = 2097151 & (load_4(s + 57) >> 6);
  int64_t s23 = (load_4(s + 60) >> 3);
  int64_t carry0;
  int64_t carry1;
  int64_t carry2;
  int64_t carry3;
  int64_t carry4;
  int64_t carry5;
  int64_t carry6;
  int64_t carry7;
  int64_t carry8;
  int64_t carry9;
  int64_t carry10;
  int64_t carry11;
  int64_t carry12;
  int64_t carry13;
  int64_t carry14;
  int64_t carry15;
  int64_t carry16;

  s11 += s23 * 666643;
  s12 += s23 * 470296;
  s13 += s23 * 654183;
  s14 -= s23 * 997805;
  s15 += s23 * 136657;
  s16 -= s23 * 683901;
  s23 = 0;

  s10 += s22 * 666643;
  s11 += s22 * 470296;
  s12 += s22 * 654183;
  s13 -= s22 * 997805;
  s14 += s22 * 136657;
  s15 -= s22 * 683901;
  s22 = 0;

  s9 += s21 * 666643;
  s10 += s21 * 470296;
  s11 += s21 * 654183;
  s12 -= s21 * 997805;
  s13 += s21 * 136657;
  s14 -= s21 * 683901;
  s21 = 0;

  s8 += s20 * 666643;
  s9 += s20 * 470296;
  s10 += s20 * 654183;
  s11 -= s20 * 997805;
  s12 += s20 * 136657;
  s13 -= s20 * 683901;
  s20 = 0;

  s7 += s19 * 666643;
  s8 += s19 * 470296;
  s9 += s19 * 654183;
  s10 -= s19 * 997805;
  s11 += s19 * 136657;
  s12 -= s19 * 683901;
  s19 = 0;

  s6 += s18 * 666643;
  s7 += s18 * 470296;
  s8 += s18 * 654183;
  s9 -= s18 * 997805;
  s10 += s18 * 136657;
  s11 -= s18 * 683901;
  s18 = 0;

  carry6 = (s6 + (1 << 20)) >> 21;
  s7 += carry6;
  s6 -= int64_lshift21(carry6);
  carry8 = (s8 + (1 << 20)) >> 21;
  s9 += carry8;
  s8 -= int64_lshift21(carry8);
  carry10 = (s10 + (1 << 20)) >> 21;
  s11 += carry10;
  s10 -= int64_lshift21(carry10);
  carry12 = (s12 + (1 << 20)) >> 21;
  s13 += carry12;
  s12 -= int64_lshift21(carry12);
  carry14 = (s14 + (1 << 20)) >> 21;
  s15 += carry14;
  s14 -= int64_lshift21(carry14);
  carry16 = (s16 + (1 << 20)) >> 21;
  s17 += carry16;
  s16 -= int64_lshift21(carry16);

  carry7 = (s7 + (1 << 20)) >> 21;
  s8 += carry7;
  s7 -= int64_lshift21(carry7);
  carry9 = (s9 + (1 << 20)) >> 21;
  s10 += carry9;
  s9 -= int64_lshift21(carry9);
  carry11 = (s11 + (1 << 20)) >> 21;
  s12 += carry11;
  s11 -= int64_lshift21(carry11);
  carry13 = (s13 + (1 << 20)) >> 21;
  s14 += carry13;
  s13 -= int64_lshift21(carry13);
  carry15 = (s15 + (1 << 20)) >> 21;
  s16 += carry15;
  s15 -= int64_lshift21(carry15);

  s5 += s17 * 666643;
  s6 += s17 * 470296;
  s7 += s17 * 654183;
  s8 -= s17 * 997805;
  s9 += s17 * 136657;
  s10 -= s17 * 683901;
  s17 = 0;

  s4 += s16 * 666643;
  s5 += s16 * 470296;
  s6 += s16 * 654183;
  s7 -= s16 * 997805;
  s8 += s16 * 136657;
  s9 -= s16 * 683901;
  s16 = 0;

  s3 += s15 * 666643;
  s4 += s15 * 470296;
  s5 += s15 * 654183;
  s6 -= s15 * 997805;
  s7 += s15 * 136657;
  s8 -= s15 * 683901;
  s15 = 0;

  s2 += s14 * 666643;
  s3 += s14 * 470296;
  s4 += s14 * 654183;
  s5 -= s14 * 997805;
  s6 += s14 * 136657;
  s7 -= s14 * 683901;
  s14 = 0;

  s1 += s13 * 666643;
  s2 += s13 * 470296;
  s3 += s13 * 654183;
  s4 -= s13 * 997805;
  s5 += s13 * 136657;
  s6 -= s13 * 683901;
  s13 = 0;

  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;

  carry0 = (s0 + (1 << 20)) >> 21;
  s1 += carry0;
  s0 -= int64_lshift21(carry0);
  carry2 = (s2 + (1 << 20)) >> 21;
  s3 += carry2;
  s2 -= int64_lshift21(carry2);
  carry4 = (s4 + (1 << 20)) >> 21;
  s5 += carry4;
  s4 -= int64_lshift21(carry4);
  carry6 = (s6 + (1 << 20)) >> 21;
  s7 += carry6;
  s6 -= int64_lshift21(carry6);
  carry8 = (s8 + (1 << 20)) >> 21;
  s9 += carry8;
  s8 -= int64_lshift21(carry8);
  carry10 = (s10 + (1 << 20)) >> 21;
  s11 += carry10;
  s10 -= int64_lshift21(carry10);

  carry1 = (s1 + (1 << 20)) >> 21;
  s2 += carry1;
  s1 -= int64_lshift21(carry1);
  carry3 = (s3 + (1 << 20)) >> 21;
  s4 += carry3;
  s3 -= int64_lshift21(carry3);
  carry5 = (s5 + (1 << 20)) >> 21;
  s6 += carry5;
  s5 -= int64_lshift21(carry5);
  carry7 = (s7 + (1 << 20)) >> 21;
  s8 += carry7;
  s7 -= int64_lshift21(carry7);
  carry9 = (s9 + (1 << 20)) >> 21;
  s10 += carry9;
  s9 -= int64_lshift21(carry9);
  carry11 = (s11 + (1 << 20)) >> 21;
  s12 += carry11;
  s11 -= int64_lshift21(carry11);

  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;

  carry0 = s0 >> 21;
  s1 += carry0;
  s0 -= int64_lshift21(carry0);
  carry1 = s1 >> 21;
  s2 += carry1;
  s1 -= int64_lshift21(carry1);
  carry2 = s2 >> 21;
  s3 += carry2;
  s2 -= int64_lshift21(carry2);
  carry3 = s3 >> 21;
  s4 += carry3;
  s3 -= int64_lshift21(carry3);
  carry4 = s4 >> 21;
  s5 += carry4;
  s4 -= int64_lshift21(carry4);
  carry5 = s5 >> 21;
  s6 += carry5;
  s5 -= int64_lshift21(carry5);
  carry6 = s6 >> 21;
  s7 += carry6;
  s6 -= int64_lshift21(carry6);
  carry7 = s7 >> 21;
  s8 += carry7;
  s7 -= int64_lshift21(carry7);
  carry8 = s8 >> 21;
  s9 += carry8;
  s8 -= int64_lshift21(carry8);
  carry9 = s9 >> 21;
  s10 += carry9;
  s9 -= int64_lshift21(carry9);
  carry10 = s10 >> 21;
  s11 += carry10;
  s10 -= int64_lshift21(carry10);
  carry11 = s11 >> 21;
  s12 += carry11;
  s11 -= int64_lshift21(carry11);

  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;

  carry0 = s0 >> 21;
  s1 += carry0;
  s0 -= int64_lshift21(carry0);
  carry1 = s1 >> 21;
  s2 += carry1;
  s1 -= int64_lshift21(carry1);
  carry2 = s2 >> 21;
  s3 += carry2;
  s2 -= int64_lshift21(carry2);
  carry3 = s3 >> 21;
  s4 += carry3;
  s3 -= int64_lshift21(carry3);
  carry4 = s4 >> 21;
  s5 += carry4;
  s4 -= int64_lshift21(carry4);
  carry5 = s5 >> 21;
  s6 += carry5;
  s5 -= int64_lshift21(carry5);
  carry6 = s6 >> 21;
  s7 += carry6;
  s6 -= int64_lshift21(carry6);
  carry7 = s7 >> 21;
  s8 += carry7;
  s7 -= int64_lshift21(carry7);
  carry8 = s8 >> 21;
  s9 += carry8;
  s8 -= int64_lshift21(carry8);
  carry9 = s9 >> 21;
  s10 += carry9;
  s9 -= int64_lshift21(carry9);
  carry10 = s10 >> 21;
  s11 += carry10;
  s10 -= int64_lshift21(carry10);

  s[0] = s0 >> 0;
  s[1] = s0 >> 8;
  s[2] = (s0 >> 16) | (s1 << 5);
  s[3] = s1 >> 3;
  s[4] = s1 >> 11;
  s[5] = (s1 >> 19) | (s2 << 2);
  s[6] = s2 >> 6;
  s[7] = (s2 >> 14) | (s3 << 7);
  s[8] = s3 >> 1;
  s[9] = s3 >> 9;
  s[10] = (s3 >> 17) | (s4 << 4);
  s[11] = s4 >> 4;
  s[12] = s4 >> 12;
  s[13] = (s4 >> 20) | (s5 << 1);
  s[14] = s5 >> 7;
  s[15] = (s5 >> 15) | (s6 << 6);
  s[16] = s6 >> 2;
  s[17] = s6 >> 10;
  s[18] = (s6 >> 18) | (s7 << 3);
  s[19] = s7 >> 5;
  s[20] = s7 >> 13;
  s[21] = s8 >> 0;
  s[22] = s8 >> 8;
  s[23] = (s8 >> 16) | (s9 << 5);
  s[24] = s9 >> 3;
  s[25] = s9 >> 11;
  s[26] = (s9 >> 19) | (s10 << 2);
  s[27] = s10 >> 6;
  s[28] = (s10 >> 14) | (s11 << 7);
  s[29] = s11 >> 1;
  s[30] = s11 >> 9;
  s[31] = s11 >> 17;
}

// Input:
//   a[0]+256*a[1]+...+256^31*a[31] = a
//   b[0]+256*b[1]+...+256^31*b[31] = b
//   c[0]+256*c[1]+...+256^31*c[31] = c
//
// Output:
//   s[0]+256*s[1]+...+256^31*s[31] = (ab+c) mod l
//   where l = 2^252 + 27742317777372353535851937790883648493.
static void sc_muladd(uint8_t* s,
                      const uint8_t* a,
                      const uint8_t* b,
                      const uint8_t* c) {
  int64_t a0 = 2097151 & load_3(a);
  int64_t a1 = 2097151 & (load_4(a + 2) >> 5);
  int64_t a2 = 2097151 & (load_3(a + 5) >> 2);
  int64_t a3 = 2097151 & (load_4(a + 7) >> 7);
  int64_t a4 = 2097151 & (load_4(a + 10) >> 4);
  int64_t a5 = 2097151 & (load_3(a + 13) >> 1);
  int64_t a6 = 2097151 & (load_4(a + 15) >> 6);
  int64_t a7 = 2097151 & (load_3(a + 18) >> 3);
  int64_t a8 = 2097151 & load_3(a + 21);
  int64_t a9 = 2097151 & (load_4(a + 23) >> 5);
  int64_t a10 = 2097151 & (load_3(a + 26) >> 2);
  int64_t a11 = (load_4(a + 28) >> 7);
  int64_t b0 = 2097151 & load_3(b);
  int64_t b1 = 2097151 & (load_4(b + 2) >> 5);
  int64_t b2 = 2097151 & (load_3(b + 5) >> 2);
  int64_t b3 = 2097151 & (load_4(b + 7) >> 7);
  int64_t b4 = 2097151 & (load_4(b + 10) >> 4);
  int64_t b5 = 2097151 & (load_3(b + 13) >> 1);
  int64_t b6 = 2097151 & (load_4(b + 15) >> 6);
  int64_t b7 = 2097151 & (load_3(b + 18) >> 3);
  int64_t b8 = 2097151 & load_3(b + 21);
  int64_t b9 = 2097151 & (load_4(b + 23) >> 5);
  int64_t b10 = 2097151 & (load_3(b + 26) >> 2);
  int64_t b11 = (load_4(b + 28) >> 7);
  int64_t c0 = 2097151 & load_3(c);
  int64_t c1 = 2097151 & (load_4(c + 2) >> 5);
  int64_t c2 = 2097151 & (load_3(c + 5) >> 2);
  int64_t c3 = 2097151 & (load_4(c + 7) >> 7);
  int64_t c4 = 2097151 & (load_4(c + 10) >> 4);
  int64_t c5 = 2097151 & (load_3(c + 13) >> 1);
  int64_t c6 = 2097151 & (load_4(c + 15) >> 6);
  int64_t c7 = 2097151 & (load_3(c + 18) >> 3);
  int64_t c8 = 2097151 & load_3(c + 21);
  int64_t c9 = 2097151 & (load_4(c + 23) >> 5);
  int64_t c10 = 2097151 & (load_3(c + 26) >> 2);
  int64_t c11 = (load_4(c + 28) >> 7);
  int64_t s0;
  int64_t s1;
  int64_t s2;
  int64_t s3;
  int64_t s4;
  int64_t s5;
  int64_t s6;
  int64_t s7;
  int64_t s8;
  int64_t s9;
  int64_t s10;
  int64_t s11;
  int64_t s12;
  int64_t s13;
  int64_t s14;
  int64_t s15;
  int64_t s16;
  int64_t s17;
  int64_t s18;
  int64_t s19;
  int64_t s20;
  int64_t s21;
  int64_t s22;
  int64_t s23;
  int64_t carry0;
  int64_t carry1;
  int64_t carry2;
  int64_t carry3;
  int64_t carry4;
  int64_t carry5;
  int64_t carry6;
  int64_t carry7;
  int64_t carry8;
  int64_t carry9;
  int64_t carry10;
  int64_t carry11;
  int64_t carry12;
  int64_t carry13;
  int64_t carry14;
  int64_t carry15;
  int64_t carry16;
  int64_t carry17;
  int64_t carry18;
  int64_t carry19;
  int64_t carry20;
  int64_t carry21;
  int64_t carry22;

  s0 = c0 + a0 * b0;
  s1 = c1 + a0 * b1 + a1 * b0;
  s2 = c2 + a0 * b2 + a1 * b1 + a2 * b0;
  s3 = c3 + a0 * b3 + a1 * b2 + a2 * b1 + a3 * b0;
  s4 = c4 + a0 * b4 + a1 * b3 + a2 * b2 + a3 * b1 + a4 * b0;
  s5 = c5 + a0 * b5 + a1 * b4 + a2 * b3 + a3 * b2 + a4 * b1 + a5 * b0;
  s6 = c6 + a0 * b6 + a1 * b5 + a2 * b4 + a3 * b3 + a4 * b2 + a5 * b1 + a6 * b0;
  s7 = c7 + a0 * b7 + a1 * b6 + a2 * b5 + a3 * b4 + a4 * b3 + a5 * b2 +
       a6 * b1 + a7 * b0;
  s8 = c8 + a0 * b8 + a1 * b7 + a2 * b6 + a3 * b5 + a4 * b4 + a5 * b3 +
       a6 * b2 + a7 * b1 + a8 * b0;
  s9 = c9 + a0 * b9 + a1 * b8 + a2 * b7 + a3 * b6 + a4 * b5 + a5 * b4 +
       a6 * b3 + a7 * b2 + a8 * b1 + a9 * b0;
  s10 = c10 + a0 * b10 + a1 * b9 + a2 * b8 + a3 * b7 + a4 * b6 + a5 * b5 +
        a6 * b4 + a7 * b3 + a8 * b2 + a9 * b1 + a10 * b0;
  s11 = c11 + a0 * b11 + a1 * b10 + a2 * b9 + a3 * b8 + a4 * b7 + a5 * b6 +
        a6 * b5 + a7 * b4 + a8 * b3 + a9 * b2 + a10 * b1 + a11 * b0;
  s12 = a1 * b11 + a2 * b10 + a3 * b9 + a4 * b8 + a5 * b7 + a6 * b6 + a7 * b5 +
        a8 * b4 + a9 * b3 + a10 * b2 + a11 * b1;
  s13 = a2 * b11 + a3 * b10 + a4 * b9 + a5 * b8 + a6 * b7 + a7 * b6 + a8 * b5 +
        a9 * b4 + a10 * b3 + a11 * b2;
  s14 = a3 * b11 + a4 * b10 + a5 * b9 + a6 * b8 + a7 * b7 + a8 * b6 + a9 * b5 +
        a10 * b4 + a11 * b3;
  s15 = a4 * b11 + a5 * b10 + a6 * b9 + a7 * b8 + a8 * b7 + a9 * b6 + a10 * b5 +
        a11 * b4;
  s16 = a5 * b11 + a6 * b10 + a7 * b9 + a8 * b8 + a9 * b7 + a10 * b6 + a11 * b5;
  s17 = a6 * b11 + a7 * b10 + a8 * b9 + a9 * b8 + a10 * b7 + a11 * b6;
  s18 = a7 * b11 + a8 * b10 + a9 * b9 + a10 * b8 + a11 * b7;
  s19 = a8 * b11 + a9 * b10 + a10 * b9 + a11 * b8;
  s20 = a9 * b11 + a10 * b10 + a11 * b9;
  s21 = a10 * b11 + a11 * b10;
  s22 = a11 * b11;
  s23 = 0;

  carry0 = (s0 + (1 << 20)) >> 21;
  s1 += carry0;
  s0 -= int64_lshift21(carry0);
  carry2 = (s2 + (1 << 20)) >> 21;
  s3 += carry2;
  s2 -= int64_lshift21(carry2);
  carry4 = (s4 + (1 << 20)) >> 21;
  s5 += carry4;
  s4 -= int64_lshift21(carry4);
  carry6 = (s6 + (1 << 20)) >> 21;
  s7 += carry6;
  s6 -= int64_lshift21(carry6);
  carry8 = (s8 + (1 << 20)) >> 21;
  s9 += carry8;
  s8 -= int64_lshift21(carry8);
  carry10 = (s10 + (1 << 20)) >> 21;
  s11 += carry10;
  s10 -= int64_lshift21(carry10);
  carry12 = (s12 + (1 << 20)) >> 21;
  s13 += carry12;
  s12 -= int64_lshift21(carry12);
  carry14 = (s14 + (1 << 20)) >> 21;
  s15 += carry14;
  s14 -= int64_lshift21(carry14);
  carry16 = (s16 + (1 << 20)) >> 21;
  s17 += carry16;
  s16 -= int64_lshift21(carry16);
  carry18 = (s18 + (1 << 20)) >> 21;
  s19 += carry18;
  s18 -= int64_lshift21(carry18);
  carry20 = (s20 + (1 << 20)) >> 21;
  s21 += carry20;
  s20 -= int64_lshift21(carry20);
  carry22 = (s22 + (1 << 20)) >> 21;
  s23 += carry22;
  s22 -= int64_lshift21(carry22);

  carry1 = (s1 + (1 << 20)) >> 21;
  s2 += carry1;
  s1 -= int64_lshift21(carry1);
  carry3 = (s3 + (1 << 20)) >> 21;
  s4 += carry3;
  s3 -= int64_lshift21(carry3);
  carry5 = (s5 + (1 << 20)) >> 21;
  s6 += carry5;
  s5 -= int64_lshift21(carry5);
  carry7 = (s7 + (1 << 20)) >> 21;
  s8 += carry7;
  s7 -= int64_lshift21(carry7);
  carry9 = (s9 + (1 << 20)) >> 21;
  s10 += carry9;
  s9 -= int64_lshift21(carry9);
  carry11 = (s11 + (1 << 20)) >> 21;
  s12 += carry11;
  s11 -= int64_lshift21(carry11);
  carry13 = (s13 + (1 << 20)) >> 21;
  s14 += carry13;
  s13 -= int64_lshift21(carry13);
  carry15 = (s15 + (1 << 20)) >> 21;
  s16 += carry15;
  s15 -= int64_lshift21(carry15);
  carry17 = (s17 + (1 << 20)) >> 21;
  s18 += carry17;
  s17 -= int64_lshift21(carry17);
  carry19 = (s19 + (1 << 20)) >> 21;
  s20 += carry19;
  s19 -= int64_lshift21(carry19);
  carry21 = (s21 + (1 << 20)) >> 21;
  s22 += carry21;
  s21 -= int64_lshift21(carry21);

  s11 += s23 * 666643;
  s12 += s23 * 470296;
  s13 += s23 * 654183;
  s14 -= s23 * 997805;
  s15 += s23 * 136657;
  s16 -= s23 * 683901;
  s23 = 0;

  s10 += s22 * 666643;
  s11 += s22 * 470296;
  s12 += s22 * 654183;
  s13 -= s22 * 997805;
  s14 += s22 * 136657;
  s15 -= s22 * 683901;
  s22 = 0;

  s9 += s21 * 666643;
  s10 += s21 * 470296;
  s11 += s21 * 654183;
  s12 -= s21 * 997805;
  s13 += s21 * 136657;
  s14 -= s21 * 683901;
  s21 = 0;

  s8 += s20 * 666643;
  s9 += s20 * 470296;
  s10 += s20 * 654183;
  s11 -= s20 * 997805;
  s12 += s20 * 136657;
  s13 -= s20 * 683901;
  s20 = 0;

  s7 += s19 * 666643;
  s8 += s19 * 470296;
  s9 += s19 * 654183;
  s10 -= s19 * 997805;
  s11 += s19 * 136657;
  s12 -= s19 * 683901;
  s19 = 0;

  s6 += s18 * 666643;
  s7 += s18 * 470296;
  s8 += s18 * 654183;
  s9 -= s18 * 997805;
  s10 += s18 * 136657;
  s11 -= s18 * 683901;
  s18 = 0;

  carry6 = (s6 + (1 << 20)) >> 21;
  s7 += carry6;
  s6 -= int64_lshift21(carry6);
  carry8 = (s8 + (1 << 20)) >> 21;
  s9 += carry8;
  s8 -= int64_lshift21(carry8);
  carry10 = (s10 + (1 << 20)) >> 21;
  s11 += carry10;
  s10 -= int64_lshift21(carry10);
  carry12 = (s12 + (1 << 20)) >> 21;
  s13 += carry12;
  s12 -= int64_lshift21(carry12);
  carry14 = (s14 + (1 << 20)) >> 21;
  s15 += carry14;
  s14 -= int64_lshift21(carry14);
  carry16 = (s16 + (1 << 20)) >> 21;
  s17 += carry16;
  s16 -= int64_lshift21(carry16);

  carry7 = (s7 + (1 << 20)) >> 21;
  s8 += carry7;
  s7 -= int64_lshift21(carry7);
  carry9 = (s9 + (1 << 20)) >> 21;
  s10 += carry9;
  s9 -= int64_lshift21(carry9);
  carry11 = (s11 + (1 << 20)) >> 21;
  s12 += carry11;
  s11 -= int64_lshift21(carry11);
  carry13 = (s13 + (1 << 20)) >> 21;
  s14 += carry13;
  s13 -= int64_lshift21(carry13);
  carry15 = (s15 + (1 << 20)) >> 21;
  s16 += carry15;
  s15 -= int64_lshift21(carry15);

  s5 += s17 * 666643;
  s6 += s17 * 470296;
  s7 += s17 * 654183;
  s8 -= s17 * 997805;
  s9 += s17 * 136657;
  s10 -= s17 * 683901;
  s17 = 0;

  s4 += s16 * 666643;
  s5 += s16 * 470296;
  s6 += s16 * 654183;
  s7 -= s16 * 997805;
  s8 += s16 * 136657;
  s9 -= s16 * 683901;
  s16 = 0;

  s3 += s15 * 666643;
  s4 += s15 * 470296;
  s5 += s15 * 654183;
  s6 -= s15 * 997805;
  s7 += s15 * 136657;
  s8 -= s15 * 683901;
  s15 = 0;

  s2 += s14 * 666643;
  s3 += s14 * 470296;
  s4 += s14 * 654183;
  s5 -= s14 * 997805;
  s6 += s14 * 136657;
  s7 -= s14 * 683901;
  s14 = 0;

  s1 += s13 * 666643;
  s2 += s13 * 470296;
  s3 += s13 * 654183;
  s4 -= s13 * 997805;
  s5 += s13 * 136657;
  s6 -= s13 * 683901;
  s13 = 0;

  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;

  carry0 = (s0 + (1 << 20)) >> 21;
  s1 += carry0;
  s0 -= int64_lshift21(carry0);
  carry2 = (s2 + (1 << 20)) >> 21;
  s3 += carry2;
  s2 -= int64_lshift21(carry2);
  carry4 = (s4 + (1 << 20)) >> 21;
  s5 += carry4;
  s4 -= int64_lshift21(carry4);
  carry6 = (s6 + (1 << 20)) >> 21;
  s7 += carry6;
  s6 -= int64_lshift21(carry6);
  carry8 = (s8 + (1 << 20)) >> 21;
  s9 += carry8;
  s8 -= int64_lshift21(carry8);
  carry10 = (s10 + (1 << 20)) >> 21;
  s11 += carry10;
  s10 -= int64_lshift21(carry10);

  carry1 = (s1 + (1 << 20)) >> 21;
  s2 += carry1;
  s1 -= int64_lshift21(carry1);
  carry3 = (s3 + (1 << 20)) >> 21;
  s4 += carry3;
  s3 -= int64_lshift21(carry3);
  carry5 = (s5 + (1 << 20)) >> 21;
  s6 += carry5;
  s5 -= int64_lshift21(carry5);
  carry7 = (s7 + (1 << 20)) >> 21;
  s8 += carry7;
  s7 -= int64_lshift21(carry7);
  carry9 = (s9 + (1 << 20)) >> 21;
  s10 += carry9;
  s9 -= int64_lshift21(carry9);
  carry11 = (s11 + (1 << 20)) >> 21;
  s12 += carry11;
  s11 -= int64_lshift21(carry11);

  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;

  carry0 = s0 >> 21;
  s1 += carry0;
  s0 -= int64_lshift21(carry0);
  carry1 = s1 >> 21;
  s2 += carry1;
  s1 -= int64_lshift21(carry1);
  carry2 = s2 >> 21;
  s3 += carry2;
  s2 -= int64_lshift21(carry2);
  carry3 = s3 >> 21;
  s4 += carry3;
  s3 -= int64_lshift21(carry3);
  carry4 = s4 >> 21;
  s5 += carry4;
  s4 -= int64_lshift21(carry4);
  carry5 = s5 >> 21;
  s6 += carry5;
  s5 -= int64_lshift21(carry5);
  carry6 = s6 >> 21;
  s7 += carry6;
  s6 -= int64_lshift21(carry6);
  carry7 = s7 >> 21;
  s8 += carry7;
  s7 -= int64_lshift21(carry7);
  carry8 = s8 >> 21;
  s9 += carry8;
  s8 -= int64_lshift21(carry8);
  carry9 = s9 >> 21;
  s10 += carry9;
  s9 -= int64_lshift21(carry9);
  carry10 = s10 >> 21;
  s11 += carry10;
  s10 -= int64_lshift21(carry10);
  carry11 = s11 >> 21;
  s12 += carry11;
  s11 -= int64_lshift21(carry11);

  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;

  carry0 = s0 >> 21;
  s1 += carry0;
  s0 -= int64_lshift21(carry0);
  carry1 = s1 >> 21;
  s2 += carry1;
  s1 -= int64_lshift21(carry1);
  carry2 = s2 >> 21;
  s3 += carry2;
  s2 -= int64_lshift21(carry2);
  carry3 = s3 >> 21;
  s4 += carry3;
  s3 -= int64_lshift21(carry3);
  carry4 = s4 >> 21;
  s5 += carry4;
  s4 -= int64_lshift21(carry4);
  carry5 = s5 >> 21;
  s6 += carry5;
  s5 -= int64_lshift21(carry5);
  carry6 = s6 >> 21;
  s7 += carry6;
  s6 -= int64_lshift21(carry6);
  carry7 = s7 >> 21;
  s8 += carry7;
  s7 -= int64_lshift21(carry7);
  carry8 = s8 >> 21;
  s9 += carry8;
  s8 -= int64_lshift21(carry8);
  carry9 = s9 >> 21;
  s10 += carry9;
  s9 -= int64_lshift21(carry9);
  carry10 = s10 >> 21;
  s11 += carry10;
  s10 -= int64_lshift21(carry10);

  s[0] = s0 >> 0;
  s[1] = s0 >> 8;
  s[2] = (s0 >> 16) | (s1 << 5);
  s[3] = s1 >> 3;
  s[4] = s1 >> 11;
  s[5] = (s1 >> 19) | (s2 << 2);
  s[6] = s2 >> 6;
  s[7] = (s2 >> 14) | (s3 << 7);
  s[8] = s3 >> 1;
  s[9] = s3 >> 9;
  s[10] = (s3 >> 17) | (s4 << 4);
  s[11] = s4 >> 4;
  s[12] = s4 >> 12;
  s[13] = (s4 >> 20) | (s5 << 1);
  s[14] = s5 >> 7;
  s[15] = (s5 >> 15) | (s6 << 6);
  s[16] = s6 >> 2;
  s[17] = s6 >> 10;
  s[18] = (s6 >> 18) | (s7 << 3);
  s[19] = s7 >> 5;
  s[20] = s7 >> 13;
  s[21] = s8 >> 0;
  s[22] = s8 >> 8;
  s[23] = (s8 >> 16) | (s9 << 5);
  s[24] = s9 >> 3;
  s[25] = s9 >> 11;
  s[26] = (s9 >> 19) | (s10 << 2);
  s[27] = s10 >> 6;
  s[28] = (s10 >> 14) | (s11 << 7);
  s[29] = s11 >> 1;
  s[30] = s11 >> 9;
  s[31] = s11 >> 17;
}

int ED25519_sign(uint8_t out_sig[64],
                 const uint8_t* message,
                 size_t message_len,
                 const uint8_t private_key[64]) {
  // NOTE: The documentation on this function says that it returns zero on
  // allocation failure. While that can't happen with the current
  // implementation, we want to reserve the ability to allocate in this
  // implementation in the future.

  uint8_t az[SHA512_DIGEST_LENGTH];
  SHA512(private_key, 32, az);

  az[0] &= 248;
  az[31] &= 63;
  az[31] |= 64;

  SHA512_CTX hash_ctx;

  avb_sha512_init(&hash_ctx);
  avb_sha512_update(&hash_ctx, az + 32, 32);
  avb_sha512_update(&hash_ctx, message, message_len);
  uint8_t nonce[SHA512_DIGEST_LENGTH];
  memcpy(nonce, avb_sha512_final(&hash_ctx), SHA512_DIGEST_LENGTH);

  x25519_sc_reduce(nonce);
  ge_p3 R;
  x25519_ge_scalarmult_base(&R, nonce);
  ge_p3_tobytes(out_sig, &R);

  avb_sha512_init(&hash_ctx);
  avb_sha512_update(&hash_ctx, out_sig, 32);
  avb_sha512_update(&hash_ctx, private_key + 32, 32);
  avb_sha512_update(&hash_ctx, message, message_len);
  uint8_t hram[SHA512_DIGEST_LENGTH];
  memcpy(hram, avb_sha512_final(&hash_ctx), SHA512_DIGEST_LENGTH);

  x25519_sc_reduce(hram);
  sc_muladd(out_sig + 32, hram, az, nonce);

  return 1;
}

void ED25519_keypair_from_seed(uint8_t out_public_key[32],
                               uint8_t out_private_key[64],
                               const uint8_t seed[32]) {
  uint8_t az[SHA512_DIGEST_LENGTH];
  SHA512(seed, 32, az);

  az[0] &= 248;
  az[31] &= 127;
  az[31] |= 64;

  ge_p3 A;
  x25519_ge_scalarmult_base(&A, az);
  ge_p3_tobytes(out_public_key, &A);

  memcpy(out_private_key, seed, 32);
  memcpy(out_private_key + 32, out_public_key, 32);
}

static void x25519_scalar_mult_generic(uint8_t out[32],
                                       const uint8_t scalar[32],
                                       const uint8_t point[32]) {
  fe x1, x2, z2, x3, z3, tmp0, tmp1;
  fe_loose x2l, z2l, x3l, tmp0l, tmp1l;

  uint8_t e[32];
  memcpy(e, scalar, 32);
  e[0] &= 248;
  e[31] &= 127;
  e[31] |= 64;

  // The following implementation was transcribed to Coq and proven to
  // correspond to unary scalar multiplication in affine coordinates given that
  // x1 != 0 is the x coordinate of some point on the curve. It was also checked
  // in Coq that doing a ladderstep with x1 = x3 = 0 gives z2' = z3' = 0, and z2
  // = z3 = 0 gives z2' = z3' = 0. The statement was quantified over the
  // underlying field, so it applies to Curve25519 itself and the quadratic
  // twist of Curve25519. It was not proven in Coq that prime-field arithmetic
  // correctly simulates extension-field arithmetic on prime-field values.
  // The decoding of the byte array representation of e was not considered.
  // Specification of Montgomery curves in affine coordinates:
  // <https://github.com/mit-plv/fiat-crypto/blob/2456d821825521f7e03e65882cc3521795b0320f/src/Spec/MontgomeryCurve.v#L27>
  // Proof that these form a group that is isomorphic to a Weierstrass curve:
  // <https://github.com/mit-plv/fiat-crypto/blob/2456d821825521f7e03e65882cc3521795b0320f/src/Curves/Montgomery/AffineProofs.v#L35>
  // Coq transcription and correctness proof of the loop (where scalarbits=255):
  // <https://github.com/mit-plv/fiat-crypto/blob/2456d821825521f7e03e65882cc3521795b0320f/src/Curves/Montgomery/XZ.v#L118>
  // <https://github.com/mit-plv/fiat-crypto/blob/2456d821825521f7e03e65882cc3521795b0320f/src/Curves/Montgomery/XZProofs.v#L278>
  // preconditions: 0 <= e < 2^255 (not necessarily e < order), fe_invert(0) = 0
  fe_frombytes(&x1, point);
  fe_1(&x2);
  fe_0(&z2);
  fe_copy(&x3, &x1);
  fe_1(&z3);

  unsigned swap = 0;
  int pos;
  for (pos = 254; pos >= 0; --pos) {
    // loop invariant as of right before the test, for the case where x1 != 0:
    //   pos >= -1; if z2 = 0 then x2 is nonzero; if z3 = 0 then x3 is nonzero
    //   let r := e >> (pos+1) in the following equalities of projective points:
    //   to_xz (r*P)     === if swap then (x3, z3) else (x2, z2)
    //   to_xz ((r+1)*P) === if swap then (x2, z2) else (x3, z3)
    //   x1 is the nonzero x coordinate of the nonzero point (r*P-(r+1)*P)
    unsigned b = 1 & (e[pos / 8] >> (pos & 7));
    swap ^= b;
    fe_cswap(&x2, &x3, swap);
    fe_cswap(&z2, &z3, swap);
    swap = b;
    // Coq transcription of ladderstep formula (called from transcribed loop):
    // <https://github.com/mit-plv/fiat-crypto/blob/2456d821825521f7e03e65882cc3521795b0320f/src/Curves/Montgomery/XZ.v#L89>
    // <https://github.com/mit-plv/fiat-crypto/blob/2456d821825521f7e03e65882cc3521795b0320f/src/Curves/Montgomery/XZProofs.v#L131>
    // x1 != 0
    // <https://github.com/mit-plv/fiat-crypto/blob/2456d821825521f7e03e65882cc3521795b0320f/src/Curves/Montgomery/XZProofs.v#L217>
    // x1  = 0
    // <https://github.com/mit-plv/fiat-crypto/blob/2456d821825521f7e03e65882cc3521795b0320f/src/Curves/Montgomery/XZProofs.v#L147>
    fe_sub(&tmp0l, &x3, &z3);
    fe_sub(&tmp1l, &x2, &z2);
    fe_add(&x2l, &x2, &z2);
    fe_add(&z2l, &x3, &z3);
    fe_mul_tll(&z3, &tmp0l, &x2l);
    fe_mul_tll(&z2, &z2l, &tmp1l);
    fe_sq_tl(&tmp0, &tmp1l);
    fe_sq_tl(&tmp1, &x2l);
    fe_add(&x3l, &z3, &z2);
    fe_sub(&z2l, &z3, &z2);
    fe_mul_ttt(&x2, &tmp1, &tmp0);
    fe_sub(&tmp1l, &tmp1, &tmp0);
    fe_sq_tl(&z2, &z2l);
    fe_mul121666(&z3, &tmp1l);
    fe_sq_tl(&x3, &x3l);
    fe_add(&tmp0l, &tmp0, &z3);
    fe_mul_ttt(&z3, &x1, &z2);
    fe_mul_tll(&z2, &tmp1l, &tmp0l);
  }
  // here pos=-1, so r=e, so to_xz (e*P) === if swap then (x3, z3) else (x2, z2)
  fe_cswap(&x2, &x3, swap);
  fe_cswap(&z2, &z3, swap);

  fe_invert(&z2, &z2);
  fe_mul_ttt(&x2, &x2, &z2);
  fe_tobytes(out, &x2);
}

static void x25519_scalar_mult(uint8_t out[32],
                               const uint8_t scalar[32],
                               const uint8_t point[32]) {
  x25519_scalar_mult_generic(out, scalar, point);
}

// ED25519_sign sets |out_sig| to be a signature of |message_len| bytes from
// |message| using |private_key|. It returns one on success or zero on
// allocation failure.
int avb_ED25519_sign(uint8_t out_sig[64],
                     const uint8_t* message,
                     size_t message_len,
                     const uint8_t private_key[64]) {
  return ED25519_sign(out_sig, message, message_len, private_key);
}

// ED25519_keypair_from_seed calculates a public and private key from an
// Ed25519 “seed”. Seed values are not exposed by this API (although they
// happen to be the first 32 bytes of a private key) so this function is for
// interoperating with systems that may store just a seed instead of a full
// private key.
void avb_ED25519_keypair_from_seed(uint8_t out_public_key[32],
                                   uint8_t out_private_key[64],
                                   const uint8_t seed[32]) {
  ED25519_keypair_from_seed(out_public_key, out_private_key, seed);
}

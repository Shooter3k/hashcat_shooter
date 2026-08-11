/**
 * GPU implementation of libxcrypt gost-yescrypt for the j9T setting.
 *
 * This follows yescrypt-ref.c's YESCRYPT_RW path.  Each scalar work-item owns
 * one 16 MiB V array in d_extra{0..3}; the smaller B and S-box state lives in
 * tmps.  The state machine lets hashcat split the 5,548 SMix steps into
 * watchdog-friendly loop kernels.
 */

#ifdef KERNEL_STATIC
#include M2S(INCLUDE_PATH/inc_vendor.h)
#include M2S(INCLUDE_PATH/inc_types.h)
#include M2S(INCLUDE_PATH/inc_platform.cl)
#include M2S(INCLUDE_PATH/inc_common.cl)
#include M2S(INCLUDE_PATH/inc_hash_sha256.cl)
#include M2S(INCLUDE_PATH/inc_hash_streebog256.cl)
#endif

#define COMPARE_S M2S(INCLUDE_PATH/inc_comp_single.cl)
#define COMPARE_M M2S(INCLUDE_PATH/inc_comp_multi.cl)

#define GY_PRE_N          64u
#define GY_PRE_MIX        22u
#define GY_MAIN_N         4096u
#define GY_MAIN_MIX       1366u
#define GY_R              32u
#define GY_STATE_WORDS    1024u
#define GY_S_WORDS        3072u
#define GY_V_WORDS        (GY_MAIN_N * GY_STATE_WORDS)

#define GY_PHASE_PRE_FILL   0u
#define GY_PHASE_PRE_MIX    1u
#define GY_PHASE_MAIN_FILL  2u
#define GY_PHASE_MAIN_MIX   3u
#define GY_PHASE_DONE       4u

typedef struct gy_esalt
{
  u32 setting_buf[24];
  u32 setting_len;
} gy_esalt_t;

typedef struct gy_tmp
{
  u32 b[1024];
  u32 s[3072];
  u32 key[16];
  u32 aux[16];
  u32 hk[16];
  u32 phase;
  u32 counter;
  u32 rotation;
  u32 w;
} gy_tmp_t;

DECLSPEC void gy_salsa20 (PRIVATE_AS u32 *b, const u32 rounds)
{
  u32 x[16];

  for (u32 i = 0; i < 16; i++) x[(i * 5) & 15] = b[i];

  for (u32 i = 0; i < rounds; i += 2)
  {
    x[ 4] ^= hc_rotl32_S (x[ 0] + x[12],  7); x[ 8] ^= hc_rotl32_S (x[ 4] + x[ 0],  9);
    x[12] ^= hc_rotl32_S (x[ 8] + x[ 4], 13); x[ 0] ^= hc_rotl32_S (x[12] + x[ 8], 18);
    x[ 9] ^= hc_rotl32_S (x[ 5] + x[ 1],  7); x[13] ^= hc_rotl32_S (x[ 9] + x[ 5],  9);
    x[ 1] ^= hc_rotl32_S (x[13] + x[ 9], 13); x[ 5] ^= hc_rotl32_S (x[ 1] + x[13], 18);
    x[14] ^= hc_rotl32_S (x[10] + x[ 6],  7); x[ 2] ^= hc_rotl32_S (x[14] + x[10],  9);
    x[ 6] ^= hc_rotl32_S (x[ 2] + x[14], 13); x[10] ^= hc_rotl32_S (x[ 6] + x[ 2], 18);
    x[ 3] ^= hc_rotl32_S (x[15] + x[11],  7); x[ 7] ^= hc_rotl32_S (x[ 3] + x[15],  9);
    x[11] ^= hc_rotl32_S (x[ 7] + x[ 3], 13); x[15] ^= hc_rotl32_S (x[11] + x[ 7], 18);

    x[ 1] ^= hc_rotl32_S (x[ 0] + x[ 3],  7); x[ 2] ^= hc_rotl32_S (x[ 1] + x[ 0],  9);
    x[ 3] ^= hc_rotl32_S (x[ 2] + x[ 1], 13); x[ 0] ^= hc_rotl32_S (x[ 3] + x[ 2], 18);
    x[ 6] ^= hc_rotl32_S (x[ 5] + x[ 4],  7); x[ 7] ^= hc_rotl32_S (x[ 6] + x[ 5],  9);
    x[ 4] ^= hc_rotl32_S (x[ 7] + x[ 6], 13); x[ 5] ^= hc_rotl32_S (x[ 4] + x[ 7], 18);
    x[11] ^= hc_rotl32_S (x[10] + x[ 9],  7); x[ 8] ^= hc_rotl32_S (x[11] + x[10],  9);
    x[ 9] ^= hc_rotl32_S (x[ 8] + x[11], 13); x[10] ^= hc_rotl32_S (x[ 9] + x[ 8], 18);
    x[12] ^= hc_rotl32_S (x[15] + x[14],  7); x[13] ^= hc_rotl32_S (x[12] + x[15],  9);
    x[14] ^= hc_rotl32_S (x[13] + x[12], 13); x[15] ^= hc_rotl32_S (x[14] + x[13], 18);
  }

  for (u32 i = 0; i < 16; i++) b[i] += x[(i * 5) & 15];
}

DECLSPEC void gy_blockmix_salsa8_r1 (PRIVATE_AS u32 *x, PRIVATE_AS u32 *y)
{
  u32 t[16];

  for (u32 j = 0; j < 16; j++) t[j] = x[16 + j];

  for (u32 i = 0; i < 2; i++)
  {
    for (u32 j = 0; j < 16; j++) t[j] ^= x[i * 16 + j];

    gy_salsa20 (t, 8);

    for (u32 j = 0; j < 16; j++) y[i * 16 + j] = t[j];
  }

  for (u32 j = 0; j < 16; j++) x[     j] = y[     j];
  for (u32 j = 0; j < 16; j++) x[16 + j] = y[16 + j];
}

DECLSPEC void gy_init_sboxes (GLOBAL_AS gy_tmp_t *tmp)
{
  u32 x[32];
  u32 y[32];

  for (u32 k = 0; k < 2; k++)
  {
    for (u32 i = 0; i < 16; i++) x[k * 16 + i] = tmp->b[k * 16 + ((i * 5) & 15)];
  }

  for (u32 n = 0; n < 96; n++)
  {
    for (u32 i = 0; i < 32; i++) tmp->s[n * 32 + i] = x[i];

    gy_blockmix_salsa8_r1 (x, y);
  }

  for (u32 k = 0; k < 2; k++)
  {
    for (u32 i = 0; i < 16; i++) tmp->b[k * 16 + ((i * 5) & 15)] = x[k * 16 + i];
  }

  tmp->rotation = 0;
  tmp->w = 0;
}

DECLSPEC void gy_shuffle (GLOBAL_AS u32 *b)
{
  u32 t[16];

  for (u32 k = 0; k < 64; k++)
  {
    for (u32 i = 0; i < 16; i++) t[i] = b[k * 16 + ((i * 5) & 15)];
    for (u32 i = 0; i < 16; i++) b[k * 16 + i] = t[i];
  }
}

DECLSPEC void gy_unshuffle (GLOBAL_AS u32 *b)
{
  u32 t[16];

  for (u32 k = 0; k < 64; k++)
  {
    for (u32 i = 0; i < 16; i++) t[i] = b[k * 16 + i];
    for (u32 i = 0; i < 16; i++) b[k * 16 + ((i * 5) & 15)] = t[i];
  }
}

DECLSPEC void gy_pwxform (PRIVATE_AS u32 *x, GLOBAL_AS u32 *s, PRIVATE_AS u32 *rotation, PRIVATE_AS u32 *wp)
{
  u32 s0;
  u32 s1;
  u32 s2;

  if (*rotation == 0) { s0 = 2048; s1 = 1024; s2 =    0; }
  else if (*rotation == 1) { s0 =    0; s1 = 2048; s2 = 1024; }
  else { s0 = 1024; s1 =    0; s2 = 2048; }

  u32 w = *wp;

  for (u32 i = 0; i < 6; i++)
  {
    for (u32 j = 0; j < 4; j++)
    {
      const u32 xl0 = x[j * 4 + 0];
      const u32 xh0 = x[j * 4 + 1];
      const u32 p0 = s0 + ((xl0 & 0x00000ff0) >> 2);
      const u32 p1 = s1 + ((xh0 & 0x00000ff0) >> 2);

      for (u32 k = 0; k < 2; k++)
      {
        const u32 pos = j * 4 + k * 2;
        const u64 sx0 = hl32_to_64_S (s[p0 + k * 2 + 1], s[p0 + k * 2 + 0]);
        const u64 sx1 = hl32_to_64_S (s[p1 + k * 2 + 1], s[p1 + k * 2 + 0]);
        u64 v = (u64) x[pos + 1] * x[pos + 0];

        v += sx0;
        v ^= sx1;

        x[pos + 0] = l32_from_64_S (v);
        x[pos + 1] = h32_from_64_S (v);

        if ((i != 0) && (i != 5))
        {
          s[s2 + w * 2 + 0] = x[pos + 0];
          s[s2 + w * 2 + 1] = x[pos + 1];
          w++;
        }
      }
    }
  }

  *rotation = (*rotation + 1) % 3;
  *wp = w & 511;
}

DECLSPEC void gy_blockmix_pwx (GLOBAL_AS u32 *b, GLOBAL_AS u32 *s, PRIVATE_AS u32 *rotation, PRIVATE_AS u32 *w)
{
  u32 x[16];

  for (u32 j = 0; j < 16; j++) x[j] = b[1008 + j];

  for (u32 i = 0; i < 64; i++)
  {
    for (u32 j = 0; j < 16; j++) x[j] ^= b[i * 16 + j];

    gy_pwxform (x, s, rotation, w);

    for (u32 j = 0; j < 16; j++) b[i * 16 + j] = x[j];
  }

  u32 t[16];

  for (u32 j = 0; j < 16; j++) t[j] = b[1008 + j];

  gy_salsa20 (t, 2);

  for (u32 j = 0; j < 16; j++) b[1008 + j] = t[j];
}

DECLSPEC u64 gy_integerify (GLOBAL_AS const u32 *b)
{
  return hl32_to_64_S (b[1008 + 13], b[1008 + 0]);
}

DECLSPEC u64 gy_p2floor (u64 x)
{
  u64 y;

  while ((y = x & (x - 1)) != 0) x = y;

  return x;
}

DECLSPEC u32 gy_wrap (const u64 x, const u32 i)
{
  const u64 n = gy_p2floor (i);

  return (u32) ((x & (n - 1)) + (i - n));
}

DECLSPEC GLOBAL_AS u32 *gy_memory (GLOBAL_AS void *v0, GLOBAL_AS void *v1, GLOBAL_AS void *v2, GLOBAL_AS void *v3, const u64 gid)
{
  const u64 group = gid / 4;

  GLOBAL_AS u32 *base;

  switch ((u32) (gid & 3))
  {
    case 0: base = (GLOBAL_AS u32 *) v0; break;
    case 1: base = (GLOBAL_AS u32 *) v1; break;
    case 2: base = (GLOBAL_AS u32 *) v2; break;
    default: base = (GLOBAL_AS u32 *) v3; break;
  }

  return base + group * (u64) GY_V_WORDS;
}

DECLSPEC void gy_pbkdf2_body_g (PRIVATE_AS const sha256_hmac_ctx_t *base, GLOBAL_AS u32 *out, const u32 out_len)
{
  for (u32 off = 0, block = 1; off < out_len; off += 8, block++)
  {
    sha256_hmac_ctx_t ctx = *base;
    u32 w0[4] = { block, 0, 0, 0 };
    u32 w1[4] = { 0, 0, 0, 0 };
    u32 w2[4] = { 0, 0, 0, 0 };
    u32 w3[4] = { 0, 0, 0, 0 };

    sha256_hmac_update_64 (&ctx, w0, w1, w2, w3, 4);
    sha256_hmac_final (&ctx);

    for (u32 i = 0; i < 8; i++) out[off + i] = hc_swap32_S (ctx.opad.h[i]);
  }
}

DECLSPEC void gy_pbkdf2_body_p (PRIVATE_AS const sha256_hmac_ctx_t *base, PRIVATE_AS u32 *out)
{
  sha256_hmac_ctx_t ctx = *base;
  u32 w0[4] = { 1, 0, 0, 0 };
  u32 w1[4] = { 0, 0, 0, 0 };
  u32 w2[4] = { 0, 0, 0, 0 };
  u32 w3[4] = { 0, 0, 0, 0 };

  sha256_hmac_update_64 (&ctx, w0, w1, w2, w3, 4);
  sha256_hmac_final (&ctx);

  for (u32 i = 0; i < 8; i++) out[i] = hc_swap32_S (ctx.opad.h[i]);
}

DECLSPEC void gy_yescrypt_key_pw (GLOBAL_AS const pw_t *pw, const u32 label_len, GLOBAL_AS u32 *key)
{
  const u32 label[16] = { 0x63736579, 0x74707972, 0x6572702d, 0x68736168, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  sha256_hmac_ctx_t ctx;

  sha256_hmac_init_swap (&ctx, label, label_len);
  sha256_hmac_update_global_swap (&ctx, pw->i, pw->pw_len);
  sha256_hmac_final (&ctx);

  for (u32 i = 0; i < 8; i++) key[i] = hc_swap32_S (ctx.opad.h[i]);
  for (u32 i = 8; i < 16; i++) key[i] = 0;
}

DECLSPEC void gy_yescrypt_key_private (PRIVATE_AS const u32 *pw, const u32 label_len, GLOBAL_AS u32 *key)
{
  const u32 label[16] = { 0x63736579, 0x74707972, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  sha256_hmac_ctx_t ctx;

  sha256_hmac_init_swap (&ctx, label, label_len);
  sha256_hmac_update_swap (&ctx, pw, 32);
  sha256_hmac_final (&ctx);

  for (u32 i = 0; i < 8; i++) key[i] = hc_swap32_S (ctx.opad.h[i]);
  for (u32 i = 8; i < 16; i++) key[i] = 0;
}

DECLSPEC void gy_setup_kdf (GLOBAL_AS gy_tmp_t *tmp, GLOBAL_AS const salt_t *salt)
{
  sha256_hmac_ctx_t ctx;

  sha256_hmac_init_global_swap (&ctx, tmp->key, 32);
  sha256_hmac_update_global_swap (&ctx, salt->salt_buf, salt->salt_len);
  gy_pbkdf2_body_g (&ctx, tmp->b, 1024);

  for (u32 i = 0; i < 8; i++) tmp->aux[i] = tmp->b[i];
  for (u32 i = 8; i < 16; i++) tmp->aux[i] = 0;

  gy_init_sboxes (tmp);

  sha256_hmac_init_global_swap (&ctx, tmp->b + 1008, 64);
  sha256_hmac_update_swap (&ctx, tmp->aux, 32);
  sha256_hmac_final (&ctx);

  for (u32 i = 0; i < 8; i++) tmp->aux[i] = hc_swap32_S (ctx.opad.h[i]);
  for (u32 i = 8; i < 16; i++) tmp->aux[i] = 0;

  gy_shuffle (tmp->b);
}

DECLSPEC void gy_finish_kdf (GLOBAL_AS gy_tmp_t *tmp, PRIVATE_AS u32 *dk)
{
  sha256_hmac_ctx_t ctx;

  gy_unshuffle (tmp->b);

  sha256_hmac_init_global_swap (&ctx, tmp->aux, 32);
  sha256_hmac_update_global_swap (&ctx, tmp->b, 4096);
  gy_pbkdf2_body_p (&ctx, dk);
}

DECLSPEC void gy_streebog_raw_pw (GLOBAL_AS const pw_t *pw, GLOBAL_AS u32 *out, SHM_TYPE u64a (*tbl)[256])
{
  streebog256_ctx_t ctx;

  streebog256_init (&ctx, tbl);
  streebog256_update_global_swap (&ctx, pw->i, pw->pw_len);
  streebog256_final (&ctx);

  out[0] = hc_swap32_S (h32_from_64_S (ctx.h[3])); out[1] = hc_swap32_S (l32_from_64_S (ctx.h[3]));
  out[2] = hc_swap32_S (h32_from_64_S (ctx.h[2])); out[3] = hc_swap32_S (l32_from_64_S (ctx.h[2]));
  out[4] = hc_swap32_S (h32_from_64_S (ctx.h[1])); out[5] = hc_swap32_S (l32_from_64_S (ctx.h[1]));
  out[6] = hc_swap32_S (h32_from_64_S (ctx.h[0])); out[7] = hc_swap32_S (l32_from_64_S (ctx.h[0]));
  for (u32 i = 8; i < 16; i++) out[i] = 0;
}

DECLSPEC void gy_streebog_hmac_out (PRIVATE_AS streebog256_hmac_ctx_t *ctx, PRIVATE_AS u32 *out)
{
  streebog256_hmac_final (ctx);

  out[0] = hc_swap32_S (h32_from_64_S (ctx->opad.h[3])); out[1] = hc_swap32_S (l32_from_64_S (ctx->opad.h[3]));
  out[2] = hc_swap32_S (h32_from_64_S (ctx->opad.h[2])); out[3] = hc_swap32_S (l32_from_64_S (ctx->opad.h[2]));
  out[4] = hc_swap32_S (h32_from_64_S (ctx->opad.h[1])); out[5] = hc_swap32_S (l32_from_64_S (ctx->opad.h[1]));
  out[6] = hc_swap32_S (h32_from_64_S (ctx->opad.h[0])); out[7] = hc_swap32_S (l32_from_64_S (ctx->opad.h[0]));
}

KERNEL_FQ KERNEL_FA void m29980_init (KERN_ATTR_TMPS_ESALT (gy_tmp_t, gy_esalt_t))
{
  const u64 gid = get_global_id (0);

  if (gid >= GID_CNT) return;

  GLOBAL_AS gy_tmp_t *tmp = &tmps[gid];

  CONSTANT_AS u64a (*tbl)[256] = sbob256_sl64;

  gy_streebog_raw_pw (&pws[gid], tmp->hk, tbl);

  gy_yescrypt_key_pw (&pws[gid], 16, tmp->key);
  gy_setup_kdf (tmp, &salt_bufs[SALT_POS_HOST]);

  tmp->phase = GY_PHASE_PRE_FILL;
  tmp->counter = 0;
}

KERNEL_FQ KERNEL_FA void m29980_loop (KERN_ATTR_TMPS_ESALT (gy_tmp_t, gy_esalt_t))
{
  const u64 gid = get_global_id (0);

  if (gid >= GID_CNT) return;

  GLOBAL_AS gy_tmp_t *tmp = &tmps[gid];
  GLOBAL_AS u32 *v = gy_memory (d_extra0_buf, d_extra1_buf, d_extra2_buf, d_extra3_buf, gid);

  u32 phase = tmp->phase;
  u32 counter = tmp->counter;
  u32 rotation = tmp->rotation;
  u32 w = tmp->w;

  for (u32 step = 0; step < LOOP_CNT; step++)
  {
    if ((phase == GY_PHASE_PRE_FILL) || (phase == GY_PHASE_MAIN_FILL))
    {
      GLOBAL_AS u32 *vi = v + (u64) counter * GY_STATE_WORDS;

      for (u32 i = 0; i < GY_STATE_WORDS; i++) vi[i] = tmp->b[i];

      if (counter > 1)
      {
        const u32 j = gy_wrap (gy_integerify (tmp->b), counter);
        GLOBAL_AS const u32 *vj = v + (u64) j * GY_STATE_WORDS;

        for (u32 i = 0; i < GY_STATE_WORDS; i++) tmp->b[i] ^= vj[i];
      }

      gy_blockmix_pwx (tmp->b, tmp->s, &rotation, &w);

      counter++;

      const u32 n = (phase == GY_PHASE_PRE_FILL) ? GY_PRE_N : GY_MAIN_N;

      if (counter == n)
      {
        phase++;
        counter = 0;
      }
    }
    else if ((phase == GY_PHASE_PRE_MIX) || (phase == GY_PHASE_MAIN_MIX))
    {
      const u32 n = (phase == GY_PHASE_PRE_MIX) ? GY_PRE_N : GY_MAIN_N;
      const u32 j = (u32) gy_integerify (tmp->b) & (n - 1);
      GLOBAL_AS u32 *vj = v + (u64) j * GY_STATE_WORDS;

      for (u32 i = 0; i < GY_STATE_WORDS; i++) tmp->b[i] ^= vj[i];
      for (u32 i = 0; i < GY_STATE_WORDS; i++) vj[i] = tmp->b[i];

      gy_blockmix_pwx (tmp->b, tmp->s, &rotation, &w);

      counter++;

      const u32 count = (phase == GY_PHASE_PRE_MIX) ? GY_PRE_MIX : GY_MAIN_MIX;

      if (counter == count)
      {
        if (phase == GY_PHASE_PRE_MIX)
        {
          u32 dk[16] = { 0 };

          gy_finish_kdf (tmp, dk);
          gy_yescrypt_key_private (dk, 8, tmp->key);
          gy_setup_kdf (tmp, &salt_bufs[SALT_POS_HOST]);

          phase = GY_PHASE_MAIN_FILL;
          counter = 0;
          rotation = tmp->rotation;
          w = tmp->w;
        }
        else
        {
          phase = GY_PHASE_DONE;
          counter = 0;
          break;
        }
      }
    }
    else
    {
      break;
    }
  }

  tmp->phase = phase;
  tmp->counter = counter;
  tmp->rotation = rotation;
  tmp->w = w;
}

KERNEL_FQ KERNEL_FA void m29980_comp (KERN_ATTR_TMPS_ESALT (gy_tmp_t, gy_esalt_t))
{
  const u64 gid = get_global_id (0);

  if (gid >= GID_CNT) return;

  GLOBAL_AS gy_tmp_t *tmp = &tmps[gid];

  u32 dk[16] = { 0 };

  gy_finish_kdf (tmp, dk);

  const u32 client_key[16] = { 0x65696c43, 0x4b20746e, 0x00007965, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  sha256_hmac_ctx_t shactx;

  sha256_hmac_init_swap (&shactx, dk, 32);
  sha256_hmac_update_swap (&shactx, client_key, 10);
  sha256_hmac_final (&shactx);

  u32 client_hash[16] = { 0 };

  for (u32 i = 0; i < 8; i++) client_hash[i] = hc_swap32_S (shactx.opad.h[i]);

  sha256_ctx_t sha;

  sha256_init (&sha);
  sha256_update_swap (&sha, client_hash, 32);
  sha256_final (&sha);

  u32 yescrypt[16] = { 0 };

  for (u32 i = 0; i < 8; i++) yescrypt[i] = hc_swap32_S (sha.h[i]);

  CONSTANT_AS u64a (*tbl)[256] = sbob256_sl64;
  GLOBAL_AS const gy_esalt_t *esalt = &esalt_bufs[DIGESTS_OFFSET_HOST];

  streebog256_hmac_ctx_t gctx;
  u32 interm[16] = { 0 };
  u32 hk[16];

  for (u32 i = 0; i < 16; i++) hk[i] = tmp->hk[i];

  streebog256_hmac_init_swap (&gctx, hk, 32, tbl);
  streebog256_hmac_update_global_swap (&gctx, esalt->setting_buf, esalt->setting_len);
  gy_streebog_hmac_out (&gctx, interm);

  streebog256_hmac_init_swap (&gctx, interm, 32, tbl);
  streebog256_hmac_update_swap (&gctx, yescrypt, 32);
  streebog256_hmac_final (&gctx);

  const u32 r0 = hc_swap32_S (h32_from_64_S (gctx.opad.h[3]));
  const u32 r1 = hc_swap32_S (l32_from_64_S (gctx.opad.h[3]));
  const u32 r2 = hc_swap32_S (h32_from_64_S (gctx.opad.h[2]));
  const u32 r3 = hc_swap32_S (l32_from_64_S (gctx.opad.h[2]));

  #define il_pos 0

  #ifdef KERNEL_STATIC
  #include COMPARE_M
  #endif
}

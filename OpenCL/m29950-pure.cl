#define PHPBB3_BCRYPT_PREHASH_MD5 0

/**
 * phpBB3 legacy rehash: bcrypt(phpass($pass)) and
 * bcrypt(phpass(md5($pass))).
 */

#ifdef KERNEL_STATIC
#include M2S(INCLUDE_PATH/inc_vendor.h)
#include M2S(INCLUDE_PATH/inc_types.h)
#include M2S(INCLUDE_PATH/inc_platform.cl)
#include M2S(INCLUDE_PATH/inc_common.cl)
#include M2S(INCLUDE_PATH/inc_hash_md5.cl)
#include M2S(INCLUDE_PATH/inc_cipher_blowfish.cl)
#endif

#define COMPARE_S M2S(INCLUDE_PATH/inc_comp_single.cl)
#define COMPARE_M M2S(INCLUDE_PATH/inc_comp_multi.cl)

typedef struct phpbb3_bcrypt_tmp
{
  u32 digest_buf[4];
  u32 md5_buf[8];

  u32 E[18];
  u32 P[18];

  u32 S0[256];
  u32 S1[256];
  u32 S2[256];
  u32 S3[256];

} phpbb3_bcrypt_tmp_t;

#ifndef PHPBB3_BCRYPT_PREHASH_MD5
#define PHPBB3_BCRYPT_PREHASH_MD5 0
#endif

DECLSPEC u32 phpbb3_itoa64 (const u32 v)
{
  if (v == 0) return '.';
  if (v == 1) return '/';
  if (v < 12) return '0' + v - 2;
  if (v < 38) return 'A' + v - 12;

  return 'a' + v - 38;
}

DECLSPEC u32 phpbb3_hex_lower (const u32 v)
{
  return (v < 10) ? '0' + v : 'a' + v - 10;
}

KERNEL_FQ KERNEL_FA void m29950_init (KERN_ATTR_TMPS (phpbb3_bcrypt_tmp_t))
{
  const u64 gid = get_global_id (0);

  if (gid >= GID_CNT) return;

  md5_ctx_t md5_ctx;

  md5_init (&md5_ctx);

  md5_update_global (&md5_ctx, salt_bufs[SALT_POS_HOST].salt_buf_pc, salt_bufs[SALT_POS_HOST].salt_len_pc);

  if (PHPBB3_BCRYPT_PREHASH_MD5 == 0)
  {
    md5_update_global (&md5_ctx, pws[gid].i, pws[gid].pw_len);
  }
  else
  {
    md5_ctx_t pw_ctx;

    md5_init (&pw_ctx);
    md5_update_global (&pw_ctx, pws[gid].i, pws[gid].pw_len);
    md5_final (&pw_ctx);

    // md5_update() always stages one complete 64-byte block, even when len is
    // shorter. Keep all 16 words addressable and zero-padded.

    u32 w[16] = { 0 };

    for (u32 i = 0; i < 16; i++)
    {
      const u32 b = (pw_ctx.h[i >> 2] >> ((i & 3) * 8)) & 0xff;
      const u32 p = i * 2;

      w[(p + 0) >> 2] |= phpbb3_hex_lower (b >> 4)       << (((p + 0) & 3) * 8);
      w[(p + 1) >> 2] |= phpbb3_hex_lower (b & 0x0f)     << (((p + 1) & 3) * 8);
    }

    for (u32 i = 0; i < 8; i++) tmps[gid].md5_buf[i] = w[i];

    md5_update (&md5_ctx, w, 32);
  }

  md5_final (&md5_ctx);

  tmps[gid].digest_buf[0] = md5_ctx.h[0];
  tmps[gid].digest_buf[1] = md5_ctx.h[1];
  tmps[gid].digest_buf[2] = md5_ctx.h[2];
  tmps[gid].digest_buf[3] = md5_ctx.h[3];
}

KERNEL_FQ KERNEL_FA void m29950_loop (KERN_ATTR_TMPS (phpbb3_bcrypt_tmp_t))
{
  const u64 gid = get_global_id (0);

  if (gid >= GID_CNT) return;

  if (PHPBB3_BCRYPT_PREHASH_MD5 != 0)
  {
    u32 w[8];

    w[0] = tmps[gid].md5_buf[0];
    w[1] = tmps[gid].md5_buf[1];
    w[2] = tmps[gid].md5_buf[2];
    w[3] = tmps[gid].md5_buf[3];
    w[4] = tmps[gid].md5_buf[4];
    w[5] = tmps[gid].md5_buf[5];
    w[6] = tmps[gid].md5_buf[6];
    w[7] = tmps[gid].md5_buf[7];

    u32 digest[4];

    digest[0] = tmps[gid].digest_buf[0];
    digest[1] = tmps[gid].digest_buf[1];
    digest[2] = tmps[gid].digest_buf[2];
    digest[3] = tmps[gid].digest_buf[3];

    md5_ctx_t md5_ctx;

    md5_init (&md5_ctx);

    md5_ctx.w0[0] = digest[0];
    md5_ctx.w0[1] = digest[1];
    md5_ctx.w0[2] = digest[2];
    md5_ctx.w0[3] = digest[3];
    md5_ctx.w1[0] = w[0];
    md5_ctx.w1[1] = w[1];
    md5_ctx.w1[2] = w[2];
    md5_ctx.w1[3] = w[3];
    md5_ctx.w2[0] = w[4];
    md5_ctx.w2[1] = w[5];
    md5_ctx.w2[2] = w[6];
    md5_ctx.w2[3] = w[7];
    md5_ctx.len = 48;

    md5_final (&md5_ctx);

    digest[0] = md5_ctx.h[0];
    digest[1] = md5_ctx.h[1];
    digest[2] = md5_ctx.h[2];
    digest[3] = md5_ctx.h[3];

    for (u32 i = 1; i < LOOP_CNT; i++)
    {
      md5_ctx.w0[0] = digest[0];
      md5_ctx.w0[1] = digest[1];
      md5_ctx.w0[2] = digest[2];
      md5_ctx.w0[3] = digest[3];

      digest[0] = MD5M_A;
      digest[1] = MD5M_B;
      digest[2] = MD5M_C;
      digest[3] = MD5M_D;

      md5_transform (md5_ctx.w0, md5_ctx.w1, md5_ctx.w2, md5_ctx.w3, digest);
    }

    tmps[gid].digest_buf[0] = digest[0];
    tmps[gid].digest_buf[1] = digest[1];
    tmps[gid].digest_buf[2] = digest[2];
    tmps[gid].digest_buf[3] = digest[3];

    return;
  }

  u32 w[64] = { 0 };
  const u32 pw_len = pws[gid].pw_len;

  for (u32 i = 0; i < pw_len; i += 4) w[i >> 2] = pws[gid].i[i >> 2];

  u32 digest[4];

  digest[0] = tmps[gid].digest_buf[0];
  digest[1] = tmps[gid].digest_buf[1];
  digest[2] = tmps[gid].digest_buf[2];
  digest[3] = tmps[gid].digest_buf[3];

  md5_ctx_t md5_ctx;

  md5_init (&md5_ctx);

  md5_ctx.w0[0] = digest[0];
  md5_ctx.w0[1] = digest[1];
  md5_ctx.w0[2] = digest[2];
  md5_ctx.w0[3] = digest[3];
  md5_ctx.len = 16;

  md5_update (&md5_ctx, w, pw_len);
  md5_final (&md5_ctx);

  digest[0] = md5_ctx.h[0];
  digest[1] = md5_ctx.h[1];
  digest[2] = md5_ctx.h[2];
  digest[3] = md5_ctx.h[3];

  if ((16 + pw_len + 1) >= 56)
  {
    for (u32 i = 1; i < LOOP_CNT; i++)
    {
      md5_init (&md5_ctx);

      md5_ctx.w0[0] = digest[0];
      md5_ctx.w0[1] = digest[1];
      md5_ctx.w0[2] = digest[2];
      md5_ctx.w0[3] = digest[3];
      md5_ctx.len = 16;

      md5_update (&md5_ctx, w, pw_len);
      md5_final (&md5_ctx);

      digest[0] = md5_ctx.h[0];
      digest[1] = md5_ctx.h[1];
      digest[2] = md5_ctx.h[2];
      digest[3] = md5_ctx.h[3];
    }
  }
  else
  {
    for (u32 i = 1; i < LOOP_CNT; i++)
    {
      md5_ctx.w0[0] = digest[0];
      md5_ctx.w0[1] = digest[1];
      md5_ctx.w0[2] = digest[2];
      md5_ctx.w0[3] = digest[3];

      digest[0] = MD5M_A;
      digest[1] = MD5M_B;
      digest[2] = MD5M_C;
      digest[3] = MD5M_D;

      md5_transform (md5_ctx.w0, md5_ctx.w1, md5_ctx.w2, md5_ctx.w3, digest);
    }
  }

  tmps[gid].digest_buf[0] = digest[0];
  tmps[gid].digest_buf[1] = digest[1];
  tmps[gid].digest_buf[2] = digest[2];
  tmps[gid].digest_buf[3] = digest[3];
}

KERNEL_FQ KERNEL_FA void m29950_init2 (KERN_ATTR_TMPS (phpbb3_bcrypt_tmp_t))
{
  const u64 gid = get_global_id (0);
  const u64 lid = get_local_id (0);

  if (gid >= GID_CNT) return;

  u8 encoded[22];
  u32 out = 0;
  u32 pos = 0;

  while (pos < 16)
  {
    u32 v = (tmps[gid].digest_buf[pos >> 2] >> ((pos & 3) * 8)) & 0xff;

    pos++;
    encoded[out++] = phpbb3_itoa64 (v & 0x3f);

    if (pos < 16) v |= ((tmps[gid].digest_buf[pos >> 2] >> ((pos & 3) * 8)) & 0xff) << 8;

    encoded[out++] = phpbb3_itoa64 ((v >> 6) & 0x3f);

    if (pos++ >= 16) break;
    if (pos < 16) v |= ((tmps[gid].digest_buf[pos >> 2] >> ((pos & 3) * 8)) & 0xff) << 16;

    encoded[out++] = phpbb3_itoa64 ((v >> 12) & 0x3f);

    if (pos++ >= 16) break;

    encoded[out++] = phpbb3_itoa64 ((v >> 18) & 0x3f);
  }

  u32 w[18] = { 0 };

  for (u32 i = 0; i < 22; i++) w[i >> 2] |= ((u32) encoded[i]) << ((i & 3) * 8);

  u32 E[18] = { 0 };

  expand_key (E, w, 22);

  for (u32 i = 0; i < 18; i++)
  {
    E[i] = hc_swap32_S (E[i]);
    tmps[gid].E[i] = E[i];
  }

  u32 salt_buf[4];

  salt_buf[0] = salt_bufs[SALT_POS_HOST].salt_buf[0];
  salt_buf[1] = salt_bufs[SALT_POS_HOST].salt_buf[1];
  salt_buf[2] = salt_bufs[SALT_POS_HOST].salt_buf[2];
  salt_buf[3] = salt_bufs[SALT_POS_HOST].salt_buf[3];

  #ifdef DYNAMIC_LOCAL
  // supplied by the backend
  #else
  LOCAL_VK u32 S0_all[FIXED_LOCAL_SIZE][256];
  LOCAL_VK u32 S1_all[FIXED_LOCAL_SIZE][256];
  LOCAL_VK u32 S2_all[FIXED_LOCAL_SIZE][256];
  LOCAL_VK u32 S3_all[FIXED_LOCAL_SIZE][256];
  #endif

  #ifdef BCRYPT_AVOID_BANK_CONFLICTS
  LOCAL_AS u32 *S0 = S + (FIXED_LOCAL_SIZE * 256 * 0);
  LOCAL_AS u32 *S1 = S + (FIXED_LOCAL_SIZE * 256 * 1);
  LOCAL_AS u32 *S2 = S + (FIXED_LOCAL_SIZE * 256 * 2);
  LOCAL_AS u32 *S3 = S + (FIXED_LOCAL_SIZE * 256 * 3);
  #else
  LOCAL_AS u32 *S0 = S0_all[lid];
  LOCAL_AS u32 *S1 = S1_all[lid];
  LOCAL_AS u32 *S2 = S2_all[lid];
  LOCAL_AS u32 *S3 = S3_all[lid];
  #endif

  u32 P[18];

  blowfish_set_key_salt (E, 18, salt_buf, P, S0, S1, S2, S3);

  for (u32 i = 0; i < 18; i++) tmps[gid].P[i] = P[i];

  for (u32 i = 0; i < 256; i++)
  {
    tmps[gid].S0[i] = GET_KEY32 (S0, i);
    tmps[gid].S1[i] = GET_KEY32 (S1, i);
    tmps[gid].S2[i] = GET_KEY32 (S2, i);
    tmps[gid].S3[i] = GET_KEY32 (S3, i);
  }
}

KERNEL_FQ KERNEL_FA void m29950_loop2 (KERN_ATTR_TMPS (phpbb3_bcrypt_tmp_t))
{
  const u64 gid = get_global_id (0);
  const u64 lid = get_local_id (0);

  if (gid >= GID_CNT) return;

  u32 E[18];
  u32 P[18];

  for (u32 i = 0; i < 18; i++)
  {
    E[i] = tmps[gid].E[i];
    P[i] = tmps[gid].P[i];
  }

  #ifdef DYNAMIC_LOCAL
  // supplied by the backend
  #else
  LOCAL_VK u32 S0_all[FIXED_LOCAL_SIZE][256];
  LOCAL_VK u32 S1_all[FIXED_LOCAL_SIZE][256];
  LOCAL_VK u32 S2_all[FIXED_LOCAL_SIZE][256];
  LOCAL_VK u32 S3_all[FIXED_LOCAL_SIZE][256];
  #endif

  #ifdef BCRYPT_AVOID_BANK_CONFLICTS
  LOCAL_AS u32 *S0 = S + (FIXED_LOCAL_SIZE * 256 * 0);
  LOCAL_AS u32 *S1 = S + (FIXED_LOCAL_SIZE * 256 * 1);
  LOCAL_AS u32 *S2 = S + (FIXED_LOCAL_SIZE * 256 * 2);
  LOCAL_AS u32 *S3 = S + (FIXED_LOCAL_SIZE * 256 * 3);
  #else
  LOCAL_AS u32 *S0 = S0_all[lid];
  LOCAL_AS u32 *S1 = S1_all[lid];
  LOCAL_AS u32 *S2 = S2_all[lid];
  LOCAL_AS u32 *S3 = S3_all[lid];
  #endif

  for (u32 i = 0; i < 256; i++)
  {
    SET_KEY32 (S0, i, tmps[gid].S0[i]);
    SET_KEY32 (S1, i, tmps[gid].S1[i]);
    SET_KEY32 (S2, i, tmps[gid].S2[i]);
    SET_KEY32 (S3, i, tmps[gid].S3[i]);
  }

  u32 salt_buf[4];

  salt_buf[0] = salt_bufs[SALT_POS_HOST].salt_buf[0];
  salt_buf[1] = salt_bufs[SALT_POS_HOST].salt_buf[1];
  salt_buf[2] = salt_bufs[SALT_POS_HOST].salt_buf[2];
  salt_buf[3] = salt_bufs[SALT_POS_HOST].salt_buf[3];

  for (u32 i = 0; i < LOOP_CNT; i++)
  {
    for (u32 j = 0; j < 18; j++) P[j] ^= E[j];

    blowfish_encrypt (P, S0, S1, S2, S3);

    P[ 0] ^= salt_buf[0]; P[ 1] ^= salt_buf[1]; P[ 2] ^= salt_buf[2]; P[ 3] ^= salt_buf[3];
    P[ 4] ^= salt_buf[0]; P[ 5] ^= salt_buf[1]; P[ 6] ^= salt_buf[2]; P[ 7] ^= salt_buf[3];
    P[ 8] ^= salt_buf[0]; P[ 9] ^= salt_buf[1]; P[10] ^= salt_buf[2]; P[11] ^= salt_buf[3];
    P[12] ^= salt_buf[0]; P[13] ^= salt_buf[1]; P[14] ^= salt_buf[2]; P[15] ^= salt_buf[3];
    P[16] ^= salt_buf[0]; P[17] ^= salt_buf[1];

    blowfish_encrypt (P, S0, S1, S2, S3);
  }

  for (u32 i = 0; i < 18; i++) tmps[gid].P[i] = P[i];

  for (u32 i = 0; i < 256; i++)
  {
    tmps[gid].S0[i] = GET_KEY32 (S0, i);
    tmps[gid].S1[i] = GET_KEY32 (S1, i);
    tmps[gid].S2[i] = GET_KEY32 (S2, i);
    tmps[gid].S3[i] = GET_KEY32 (S3, i);
  }
}

KERNEL_FQ KERNEL_FA void m29950_comp (KERN_ATTR_TMPS (phpbb3_bcrypt_tmp_t))
{
  const u64 gid = get_global_id (0);
  const u64 lid = get_local_id (0);

  if (gid >= GID_CNT) return;

  u32 P[18];

  for (u32 i = 0; i < 18; i++) P[i] = tmps[gid].P[i];

  #ifdef DYNAMIC_LOCAL
  // supplied by the backend
  #else
  LOCAL_VK u32 S0_all[FIXED_LOCAL_SIZE][256];
  LOCAL_VK u32 S1_all[FIXED_LOCAL_SIZE][256];
  LOCAL_VK u32 S2_all[FIXED_LOCAL_SIZE][256];
  LOCAL_VK u32 S3_all[FIXED_LOCAL_SIZE][256];
  #endif

  #ifdef BCRYPT_AVOID_BANK_CONFLICTS
  LOCAL_AS u32 *S0 = S + (FIXED_LOCAL_SIZE * 256 * 0);
  LOCAL_AS u32 *S1 = S + (FIXED_LOCAL_SIZE * 256 * 1);
  LOCAL_AS u32 *S2 = S + (FIXED_LOCAL_SIZE * 256 * 2);
  LOCAL_AS u32 *S3 = S + (FIXED_LOCAL_SIZE * 256 * 3);
  #else
  LOCAL_AS u32 *S0 = S0_all[lid];
  LOCAL_AS u32 *S1 = S1_all[lid];
  LOCAL_AS u32 *S2 = S2_all[lid];
  LOCAL_AS u32 *S3 = S3_all[lid];
  #endif

  for (u32 i = 0; i < 256; i++)
  {
    SET_KEY32 (S0, i, tmps[gid].S0[i]);
    SET_KEY32 (S1, i, tmps[gid].S1[i]);
    SET_KEY32 (S2, i, tmps[gid].S2[i]);
    SET_KEY32 (S3, i, tmps[gid].S3[i]);
  }

  u32 L0 = BCRYPTM_0;
  u32 R0 = BCRYPTM_1;

  for (u32 i = 0; i < 64; i++) BF_ENCRYPT (L0, R0);

  const u32 r0 = L0;
  const u32 r1 = R0;

  L0 = BCRYPTM_2;
  R0 = BCRYPTM_3;

  for (u32 i = 0; i < 64; i++) BF_ENCRYPT (L0, R0);

  const u32 r2 = L0;
  const u32 r3 = R0;

  #define il_pos 0

  #ifdef KERNEL_STATIC
  #include COMPARE_M
  #endif
}

/**
 * Private Hashcat kernel for the KoreLogic CMIYC 2026 memory-hard SHA-512 KDF.
 * One scalar work-item owns one 64 MiB job for the official memlog=20 format.
 *
 * All intermediate data is kept as u32 word arrays (LE native) and converted
 * to BE only when feeding sha512_update_swap.  Memory (global) uses u32[16]
 * per 64-byte block with native LE ordering.
 */

#ifdef KERNEL_STATIC
#include M2S(INCLUDE_PATH/inc_vendor.h)
#include M2S(INCLUDE_PATH/inc_types.h)
#include M2S(INCLUDE_PATH/inc_platform.cl)
#include M2S(INCLUDE_PATH/inc_common.cl)
#include M2S(INCLUDE_PATH/inc_hash_sha512.cl)
#endif

#define COMPARE_S M2S(INCLUDE_PATH/inc_comp_single.cl)
#define COMPARE_M M2S(INCLUDE_PATH/inc_comp_multi.cl)

typedef struct cmiyc_tmp
{
  u32 chain[16];  // 64 bytes as LE u32s
} cmiyc_tmp_t;

DECLSPEC void cmiyc_sha512_init_digest (PRIVATE_AS u64 *digest)
{
  digest[0] = SHA512M_A;
  digest[1] = SHA512M_B;
  digest[2] = SHA512M_C;
  digest[3] = SHA512M_D;
  digest[4] = SHA512M_E;
  digest[5] = SHA512M_F;
  digest[6] = SHA512M_G;
  digest[7] = SHA512M_H;
}

DECLSPEC void cmiyc_sha512_store (PRIVATE_AS const u64 *digest, PRIVATE_AS u32 *out)
{
  for (u32 i = 0; i < 8; i++)
  {
    out[i * 2 + 0] = hc_swap32_S (h32_from_64_S (digest[i]));
    out[i * 2 + 1] = hc_swap32_S (l32_from_64_S (digest[i]));
  }
}

DECLSPEC void cmiyc_sha512_64 (PRIVATE_AS const u32 *input, PRIVATE_AS u32 *out)
{
  u32 block[32] = { 0 };
  u64 digest[8];

  for (u32 i = 0; i < 16; i++) block[i] = hc_swap32_S (input[i]);

  block[16] = 0x80000000;
  block[31] = 64 * 8;

  cmiyc_sha512_init_digest (digest);
  sha512_transform (block + 0, block + 4, block + 8, block + 12, block + 16, block + 20, block + 24, block + 28, digest);
  cmiyc_sha512_store (digest, out);
}

/**
 * Pack a u64 value into a LE u32 word array at byte offset `off`.
 * Handles the case where `off` is u32-aligned (which it always is in our usage).
 */
DECLSPEC void cmiyc_pack_le64 (PRIVATE_AS u32 *w, const u32 word_off, const u64 value)
{
  w[word_off + 0] = (u32) (value      );
  w[word_off + 1] = (u32) (value >> 32);
}

/**
 * Read a LE u64 from global u32 memory at word offset.
 */
DECLSPEC u64 cmiyc_read_le64_global (GLOBAL_AS const u32 *mem, const u32 word_off)
{
  return (u64) mem[word_off] | ((u64) mem[word_off + 1] << 32);
}

/**
 * Get pointer to this work-item's memory region.
 */
DECLSPEC GLOBAL_AS u32 *cmiyc_memory (GLOBAL_AS u32 *d_extra0_buf, GLOBAL_AS u32 *d_extra1_buf, GLOBAL_AS u32 *d_extra2_buf, GLOBAL_AS u32 *d_extra3_buf, const u64 gid)
{
  const u64 gd4 = gid / 4;
  const u32 gm4 = gid % 4;

  GLOBAL_AS u32 *base;

  switch (gm4)
  {
    case 0: base = d_extra0_buf; break;
    case 1: base = d_extra1_buf; break;
    case 2: base = d_extra2_buf; break;
    default: base = d_extra3_buf; break;
  }

  return base + gd4 * (u64) CMIYC_STRIDE_BLOCKS * 16;
}

/**
 * HMAC-SHA512(key=salt, msg=password || LE32(rounds) || LE32(memlog))
 * Result stored in chain[0..15] as LE u32s.
 */
DECLSPEC void cmiyc_hmac_seed (GLOBAL_AS const pw_t *pw, GLOBAL_AS const salt_t *salt, PRIVATE_AS u32 *chain)
{
  // Key = salt bytes (16 bytes), stored in salt_buf as LE u32s
  u32 key[32] = { 0 };
  key[0] = salt->salt_buf[0];
  key[1] = salt->salt_buf[1];
  key[2] = salt->salt_buf[2];
  key[3] = salt->salt_buf[3];

  sha512_hmac_ctx_t ctx;
  sha512_hmac_init_swap (&ctx, key, 16);
  sha512_hmac_update_global_swap (&ctx, pw->i, pw->pw_len);

  // config = LE32(rounds) || LE32(memlog) = 8 bytes
  const u32 config[32] = { salt->scrypt_p, salt->scrypt_r };
  sha512_hmac_update_swap (&ctx, config, 8);
  sha512_hmac_final (&ctx);

  // ctx.opad.h[i] are BE u64 values representing the hash output.
  // We need to store as LE u32s matching the byte sequence.
  // h[0] = 0xAABBCCDDEEFF0011 means bytes [AA BB CC DD EE FF 00 11]
  // As LE u32s: word0 = 0xDDCCBBAA (bytes 0-3), word1 = 0x1100FFEE (bytes 4-7)
  for (u32 i = 0; i < 8; i++)
  {
    const u64 h = ctx.opad.h[i];
    chain[i * 2 + 0] = hc_swap32_S (h32_from_64_S (h));  // bytes [0..3] as LE u32
    chain[i * 2 + 1] = hc_swap32_S (l32_from_64_S (h));  // bytes [4..7] as LE u32
  }
}

/**
 * fill_hash: SHA-512(chain || "cmiyc-fill" || LE64(index))
 * chain and output are u32[16] in LE format.
 * Message is 82 bytes = 21 u32s (last partial).
 */
DECLSPEC void cmiyc_fill_hash (PRIVATE_AS const u32 *chain, const u64 index, PRIVATE_AS u32 *output)
{
  u32 block[32] = { 0 };
  u64 digest[8];

  for (u32 i = 0; i < 16; i++) block[i] = hc_swap32_S (chain[i]);

  block[16] = hc_swap32_S (0x79696d63);
  block[17] = hc_swap32_S (0x69662d63);
  block[18] = hc_swap32_S (0x00006c6c | ((u32) (index & 0xffff) << 16));
  block[19] = hc_swap32_S ((u32) (index >> 16));
  block[20] = hc_swap32_S ((u32) (index >> 48) | 0x00800000);
  block[31] = 82 * 8;

  cmiyc_sha512_init_digest (digest);
  sha512_transform (block + 0, block + 4, block + 8, block + 12, block + 16, block + 20, block + 24, block + 28, digest);
  cmiyc_sha512_store (digest, output);
}

/**
 * mix_hash: SHA-512(current || other || LE64(round) || LE64(index) || tag)
 * current/other are global u32[16] in LE format.
 * Message is 145 bytes = 37 u32s (last partial: 1 byte in word 36).
 */
DECLSPEC void cmiyc_mix_hash (GLOBAL_AS const u32 *current, GLOBAL_AS const u32 *other, const u64 round, const u64 index, const u32 tag, PRIVATE_AS u32 *output)
{
  u32 block[32];
  u64 digest[8];

  for (u32 i = 0; i < 16; i++) block[i] = hc_swap32_S (current[i]);
  for (u32 i = 0; i < 16; i++) block[16 + i] = hc_swap32_S (other[i]);

  cmiyc_sha512_init_digest (digest);
  sha512_transform (block + 0, block + 4, block + 8, block + 12, block + 16, block + 20, block + 24, block + 28, digest);

  for (u32 i = 0; i < 32; i++) block[i] = 0;

  block[0] = hc_swap32_S ((u32) round);
  block[1] = hc_swap32_S ((u32) (round >> 32));
  block[2] = hc_swap32_S ((u32) index);
  block[3] = hc_swap32_S ((u32) (index >> 32));
  block[4] = (tag << 24) | 0x00800000;
  block[31] = 145 * 8;

  sha512_transform (block + 0, block + 4, block + 8, block + 12, block + 16, block + 20, block + 24, block + 28, digest);
  cmiyc_sha512_store (digest, output);
}

KERNEL_FQ KERNEL_FA void m29960_init (KERN_ATTR_TMPS (cmiyc_tmp_t))
{
  const u64 gid = get_global_id (0);

  if (gid >= GID_CNT) return;

  u32 chain[16];

  cmiyc_hmac_seed (&pws[gid], &salt_bufs[SALT_POS_HOST], chain);

  for (u32 i = 0; i < 16; i++) tmps[gid].chain[i] = chain[i];
}

KERNEL_FQ KERNEL_FA void m29960_loop (KERN_ATTR_TMPS (cmiyc_tmp_t))
{
  const u64 gid = get_global_id (0);

  if (gid >= GID_CNT) return;

  GLOBAL_AS u32 *memory = cmiyc_memory ((GLOBAL_AS u32 *) d_extra0_buf, (GLOBAL_AS u32 *) d_extra1_buf, (GLOBAL_AS u32 *) d_extra2_buf, (GLOBAL_AS u32 *) d_extra3_buf, gid);

  const u64 blocks = (u64) salt_bufs[SALT_POS_HOST].scrypt_N * 2;

  u32 chain[16];

  for (u32 i = 0; i < 16; i++) chain[i] = tmps[gid].chain[i];

  for (u32 step = 0; step < LOOP_CNT; step++)
  {
    const u64 position = (u64) LOOP_POS + step;

    if (position < blocks)
    {
      u32 digest[16];

      cmiyc_fill_hash (chain, position, digest);

      GLOBAL_AS u32 *dst = memory + position * 16;

      for (u32 i = 0; i < 16; i++)
      {
        dst[i] = digest[i];
        chain[i] = digest[i];
      }
    }
    else
    {
      const u64 mix_position = position - blocks;
      const u64 round_span = blocks * 2;
      const u64 round = (u64) (mix_position / round_span) + 1;
      const u64 within = mix_position % round_span;

      u64 index;
      u64 other_index;
      u32 tag;

      if (within < blocks)
      {
        index = within;
        tag = 'A';

        GLOBAL_AS const u32 *cur = memory + index * 16;
        // other_index from bytes 0-7 of current block = LE u64 from words 0,1
        other_index = cmiyc_read_le64_global (cur, 0) & (blocks - 1);
      }
      else
      {
        index = blocks - 1 - (within - blocks);
        tag = 'B';

        GLOBAL_AS const u32 *cur = memory + index * 16;
        // other_index from bytes 8-15 of current block = LE u64 from words 2,3
        other_index = cmiyc_read_le64_global (cur, 2) & (blocks - 1);
      }

      GLOBAL_AS u32 *current = memory + index * 16;
      GLOBAL_AS const u32 *other = memory + other_index * 16;

      u32 digest[16];

      cmiyc_mix_hash (current, other, round, index, tag, digest);

      for (u32 i = 0; i < 16; i++) current[i] = digest[i];
    }
  }

  if ((u64) LOOP_POS < blocks)
  {
    for (u32 i = 0; i < 16; i++) tmps[gid].chain[i] = chain[i];
  }
}

KERNEL_FQ KERNEL_FA void m29960_comp (KERN_ATTR_TMPS (cmiyc_tmp_t))
{
  const u64 gid = get_global_id (0);

  if (gid >= GID_CNT) return;

  GLOBAL_AS const u32 *memory = cmiyc_memory ((GLOBAL_AS u32 *) d_extra0_buf, (GLOBAL_AS u32 *) d_extra1_buf, (GLOBAL_AS u32 *) d_extra2_buf, (GLOBAL_AS u32 *) d_extra3_buf, gid);

  const u64 blocks = (u64) salt_bufs[SALT_POS_HOST].scrypt_N * 2;

  u32 accumulator[32] = { 0 };

  for (u64 block = 0; block < blocks; block++)
  {
    GLOBAL_AS const u32 *src = memory + block * 16;

    for (u32 i = 0; i < 16; i++) accumulator[i] ^= src[i];
  }

  u32 digest[16];

  cmiyc_sha512_64 (accumulator, digest);

  const u32 r0 = digest[0];
  const u32 r1 = digest[1];
  const u32 r2 = digest[2];
  const u32 r3 = digest[3];

  #define il_pos 0

  #ifdef KERNEL_STATIC
  #include COMPARE_M
  #endif
}

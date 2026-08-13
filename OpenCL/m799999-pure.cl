/**
 * Result-comparison kernel for mdxfind expression-VM bridge modules.
 */

#ifdef KERNEL_STATIC
#include M2S(INCLUDE_PATH/inc_vendor.h)
#include M2S(INCLUDE_PATH/inc_types.h)
#include M2S(INCLUDE_PATH/inc_platform.cl)
#include M2S(INCLUDE_PATH/inc_common.cl)
#include M2S(INCLUDE_PATH/inc_hash_md4.cl)
#endif

#define COMPARE_S M2S(INCLUDE_PATH/inc_comp_single.cl)
#define COMPARE_M M2S(INCLUDE_PATH/inc_comp_multi.cl)

typedef struct mdxfind_tmp
{
  u32 pw_buf[64];
  u32 pw_len;

  u32 out_buf[32][64];
  u32 out_len[32];
  u32 out_cnt;

} mdxfind_tmp_t;

KERNEL_FQ KERNEL_FA void m799999_init (KERN_ATTR_TMPS (mdxfind_tmp_t))
{
  const u64 gid = get_global_id (0);

  if (gid >= GID_CNT) return;

  for (u32 idx = 0; idx < 64; idx++)
  {
    tmps[gid].pw_buf[idx] = pws[gid].i[idx];
  }

  tmps[gid].pw_len = pws[gid].pw_len;
  tmps[gid].out_cnt = 0;
}

KERNEL_FQ KERNEL_FA void m799999_loop (KERN_ATTR_TMPS (mdxfind_tmp_t))
{
}

KERNEL_FQ KERNEL_FA void m799999_comp (KERN_ATTR_TMPS (mdxfind_tmp_t))
{
  const u64 gid = get_global_id (0);

  if (gid >= GID_CNT) return;

  const u32 out_cnt = tmps[gid].out_cnt;

  for (u32 idx = 0; idx < out_cnt; idx++)
  {
    md4_ctx_t ctx;

    md4_init (&ctx);
    md4_update_global (&ctx, tmps[gid].out_buf[idx], tmps[gid].out_len[idx]);
    md4_final (&ctx);

    const u32 r0 = ctx.h[0];
    const u32 r1 = ctx.h[1];
    const u32 r2 = ctx.h[2];
    const u32 r3 = ctx.h[3];

    #define il_pos 0

    #ifdef KERNEL_STATIC
    #include COMPARE_M
    #endif
  }
}

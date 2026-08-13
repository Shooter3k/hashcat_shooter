/**
 * Shared hashcat module front-end for mdxfind expression-VM algorithms.
 *
 * Generated module_eN.c files define MDXFIND_MODE_ID and
 * MDXFIND_MODE_NAME before including this file.  The native bridge evaluates
 * mdxfind's compiled expression for each candidate.  The tiny OpenCL kernel
 * only performs hashcat's normal multi-hash lookup on the returned text.
 */

#include "common.h"
#include "types.h"
#include "modules.h"
#include "bitops.h"
#include "convert.h"
#include "shared.h"
#include "memory.h"
#include "emu_inc_hash_md4.h"
#include "mdxfind_modes.h"

#define MDXFIND_STRINGIFY_INNER(value) #value
#define MDXFIND_STRINGIFY(value) MDXFIND_STRINGIFY_INNER (value)

#define MDXFIND_TEXT_MAX 1024
#define MDXFIND_FIELD_MAX 256
#define MDXFIND_FIELD_COUNT 5

static const u32   ATTACK_EXEC    = ATTACK_EXEC_OUTSIDE_KERNEL;
static const u32   DGST_POS0      = 0;
static const u32   DGST_POS1      = 1;
static const u32   DGST_POS2      = 2;
static const u32   DGST_POS3      = 3;
static const u32   DGST_SIZE      = DGST_SIZE_4_4;
static const u32   HASH_CATEGORY  = HASH_CATEGORY_UNDEFINED;
static const char *HASH_NAME      = "mdxfind e" MDXFIND_STRINGIFY (MDXFIND_MODE_ID) " " MDXFIND_MODE_NAME;
static const u64   KERN_TYPE      = 799998;
static const u32   OPTI_TYPE      = OPTI_TYPE_ZERO_BYTE;
static const u64   OPTS_TYPE      = OPTS_TYPE_STOCK_MODULE
                                  | OPTS_TYPE_PT_GENERATE_LE
                                  | OPTS_TYPE_AUTODETECT_DISABLE
                                  | OPTS_TYPE_NATIVE_THREADS
                                  | OPTS_TYPE_MULTIHASH_DESPITE_ESALT
                                  | OPTS_TYPE_MP_MULTI_DISABLE
                                  | OPTS_TYPE_SELF_TEST_DISABLE;
static const u32   SALT_TYPE      = SALT_TYPE_EMBEDDED;
static const u64   BRIDGE_TYPE    = BRIDGE_TYPE_LAUNCH_LOOP;
static const char *BRIDGE_NAME    = "mdxfind";
static const char *ST_PASS        = "";
static const char *ST_HASH        = "00";

u32         module_attack_exec    (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return ATTACK_EXEC; }
u32         module_dgst_pos0      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_POS0; }
u32         module_dgst_pos1      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_POS1; }
u32         module_dgst_pos2      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_POS2; }
u32         module_dgst_pos3      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_POS3; }
u32         module_dgst_size      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_SIZE; }
u32         module_hash_category  (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return HASH_CATEGORY; }
const char *module_hash_name      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return HASH_NAME; }
static int  mdxfind_module_hash_mode (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return MDXFIND_HASH_MODE_FROM_ID (MDXFIND_MODE_ID); }
u64         module_kern_type      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return KERN_TYPE; }
u32         module_opti_type      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return OPTI_TYPE; }
u64         module_opts_type      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return OPTS_TYPE; }
u32         module_salt_type      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return SALT_TYPE; }
const char *module_st_hash        (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return ST_HASH; }
const char *module_st_pass        (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return ST_PASS; }
const char *module_bridge_name    (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return BRIDGE_NAME; }
u64         module_bridge_type    (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return BRIDGE_TYPE; }

typedef struct mdxfind_tmp
{
  u32 pw_buf[64];
  u32 pw_len;

  u32 out_buf[32][256];
  u32 out_len[32];
  u32 out_cnt;

} mdxfind_tmp_t;

typedef struct mdxfind_esalt
{
  u8 target_buf[MDXFIND_TEXT_MAX];
  u32 target_len;

  u8 field_buf[MDXFIND_FIELD_COUNT][MDXFIND_FIELD_MAX];
  u32 field_len[MDXFIND_FIELD_COUNT];
  u32 field_cnt;

  u32 suffix_pos;

} mdxfind_esalt_t;

u64 module_tmp_size (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return sizeof (mdxfind_tmp_t);
}

u64 module_esalt_size (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return sizeof (mdxfind_esalt_t);
}

int module_hash_decode (MAYBE_UNUSED const hashconfig_t *hashconfig, void *digest_buf, salt_t *salt, void *esalt_buf, MAYBE_UNUSED void *hook_salt_buf, MAYBE_UNUSED hashinfo_t *hash_info, const char *line_buf, const int line_len)
{
  if ((line_len <= 0) || (line_len >= MDXFIND_TEXT_MAX)) return PARSER_HASH_LENGTH;

  u32 *digest = (u32 *) digest_buf;

  mdxfind_esalt_t *esalt = (mdxfind_esalt_t *) esalt_buf;

  memset (esalt, 0, sizeof (*esalt));

  memcpy (esalt->target_buf, line_buf, line_len);

  esalt->target_buf[line_len] = 0;
  esalt->target_len = line_len;
  esalt->suffix_pos = line_len;

  u32 field_idx = 0;
  u32 field_pos = 0;
  bool in_fields = false;

  for (int pos = 0; pos < line_len; pos++)
  {
    const u8 c = (const u8) line_buf[pos];

    if (in_fields == false)
    {
      if (c == ':')
      {
        in_fields = true;
        esalt->suffix_pos = pos;
      }

      continue;
    }

    if ((c == ':') && (field_idx + 1 < MDXFIND_FIELD_COUNT))
    {
      esalt->field_len[field_idx] = field_pos;

      field_idx++;
      field_pos = 0;

      continue;
    }

    if (field_pos + 1 >= MDXFIND_FIELD_MAX) return PARSER_SALT_LENGTH;

    esalt->field_buf[field_idx][field_pos++] = c;
  }

  if (in_fields == true)
  {
    esalt->field_len[field_idx] = field_pos;
    esalt->field_cnt = field_idx + 1;
  }

  md4_ctx_t target_ctx;

  md4_init (&target_ctx);
  md4_update (&target_ctx, (const u32 *) esalt->target_buf, esalt->target_len);
  md4_final (&target_ctx);

  digest[0] = target_ctx.h[0];
  digest[1] = target_ctx.h[1];
  digest[2] = target_ctx.h[2];
  digest[3] = target_ctx.h[3];

  md4_ctx_t salt_ctx;

  md4_init (&salt_ctx);

  if (esalt->suffix_pos < esalt->target_len)
  {
    const u8 *suffix = esalt->target_buf + esalt->suffix_pos;
    const u32 suffix_len = esalt->target_len - esalt->suffix_pos;

    md4_update (&salt_ctx, (const u32 *) suffix, suffix_len);
  }

  md4_final (&salt_ctx);

  salt->salt_buf[0] = salt_ctx.h[0];
  salt->salt_buf[1] = salt_ctx.h[1];
  salt->salt_buf[2] = salt_ctx.h[2];
  salt->salt_buf[3] = salt_ctx.h[3];
  salt->salt_len = 16;
  salt->salt_iter = 1;

  return PARSER_OK;
}

int module_hash_encode (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const void *digest_buf, MAYBE_UNUSED const salt_t *salt, const void *esalt_buf, MAYBE_UNUSED const void *hook_salt_buf, MAYBE_UNUSED const hashinfo_t *hash_info, char *line_buf, const int line_size)
{
  const mdxfind_esalt_t *esalt = (const mdxfind_esalt_t *) esalt_buf;

  if ((int) esalt->target_len >= line_size) return -1;

  memcpy (line_buf, esalt->target_buf, esalt->target_len);

  line_buf[esalt->target_len] = 0;

  return esalt->target_len;
}

void module_init (module_ctx_t *module_ctx)
{
  module_ctx->module_context_size             = MODULE_CONTEXT_SIZE_CURRENT;
  module_ctx->module_interface_version        = MODULE_INTERFACE_VERSION_CURRENT;

  module_ctx->module_attack_exec              = module_attack_exec;
  module_ctx->module_benchmark_esalt          = MODULE_DEFAULT;
  module_ctx->module_benchmark_hook_salt      = MODULE_DEFAULT;
  module_ctx->module_benchmark_mask           = MODULE_DEFAULT;
  module_ctx->module_benchmark_charset        = MODULE_DEFAULT;
  module_ctx->module_benchmark_salt           = MODULE_DEFAULT;
  module_ctx->module_bridge_name              = module_bridge_name;
  module_ctx->module_bridge_type              = module_bridge_type;
  module_ctx->module_build_plain_postprocess  = MODULE_DEFAULT;
  module_ctx->module_deep_comp_kernel         = MODULE_DEFAULT;
  module_ctx->module_deprecated_notice        = MODULE_DEFAULT;
  module_ctx->module_dgst_pos0                = module_dgst_pos0;
  module_ctx->module_dgst_pos1                = module_dgst_pos1;
  module_ctx->module_dgst_pos2                = module_dgst_pos2;
  module_ctx->module_dgst_pos3                = module_dgst_pos3;
  module_ctx->module_dgst_size                = module_dgst_size;
  module_ctx->module_esalt_size               = module_esalt_size;
  module_ctx->module_extra_buffer_size        = MODULE_DEFAULT;
  module_ctx->module_extra_tmp_size           = MODULE_DEFAULT;
  module_ctx->module_extra_tuningdb_block     = MODULE_DEFAULT;
  module_ctx->module_forced_outfile_format    = MODULE_DEFAULT;
  module_ctx->module_hash_binary_count        = MODULE_DEFAULT;
  module_ctx->module_hash_binary_parse        = MODULE_DEFAULT;
  module_ctx->module_hash_binary_save         = MODULE_DEFAULT;
  module_ctx->module_hash_category            = module_hash_category;
  module_ctx->module_hash_decode              = module_hash_decode;
  module_ctx->module_hash_decode_postprocess  = MODULE_DEFAULT;
  module_ctx->module_hash_decode_potfile      = MODULE_DEFAULT;
  module_ctx->module_hash_decode_zero_hash    = MODULE_DEFAULT;
  module_ctx->module_hash_encode              = module_hash_encode;
  module_ctx->module_hash_encode_potfile      = MODULE_DEFAULT;
  module_ctx->module_hash_encode_status       = MODULE_DEFAULT;
  module_ctx->module_hash_init_selftest       = MODULE_DEFAULT;
  module_ctx->module_hash_mode                = mdxfind_module_hash_mode;
  module_ctx->module_hash_name                = module_hash_name;
  module_ctx->module_hashes_count_max         = MODULE_DEFAULT;
  module_ctx->module_hashes_count_min         = MODULE_DEFAULT;
  module_ctx->module_hlfmt_disable            = MODULE_DEFAULT;
  module_ctx->module_hook_extra_param_size    = MODULE_DEFAULT;
  module_ctx->module_hook_extra_param_init    = MODULE_DEFAULT;
  module_ctx->module_hook_extra_param_term    = MODULE_DEFAULT;
  module_ctx->module_hook12                   = MODULE_DEFAULT;
  module_ctx->module_hook23                   = MODULE_DEFAULT;
  module_ctx->module_hook_salt_size           = MODULE_DEFAULT;
  module_ctx->module_hook_size                = MODULE_DEFAULT;
  module_ctx->module_jit_build_options        = MODULE_DEFAULT;
  module_ctx->module_jit_cache_disable        = MODULE_DEFAULT;
  module_ctx->module_kern_type                = module_kern_type;
  module_ctx->module_kern_type_dynamic        = MODULE_DEFAULT;
  module_ctx->module_kernel_accel_max         = MODULE_DEFAULT;
  module_ctx->module_kernel_accel_min         = MODULE_DEFAULT;
  module_ctx->module_kernel_loops_max         = MODULE_DEFAULT;
  module_ctx->module_kernel_loops_min         = MODULE_DEFAULT;
  module_ctx->module_kernel_threads_max       = MODULE_DEFAULT;
  module_ctx->module_kernel_threads_min       = MODULE_DEFAULT;
  module_ctx->module_opti_type                = module_opti_type;
  module_ctx->module_opts_type                = module_opts_type;
  module_ctx->module_outfile_check_disable    = MODULE_DEFAULT;
  module_ctx->module_outfile_check_nocomp     = MODULE_DEFAULT;
  module_ctx->module_potfile_custom_check     = MODULE_DEFAULT;
  module_ctx->module_potfile_disable          = MODULE_DEFAULT;
  module_ctx->module_potfile_keep_all_hashes  = MODULE_DEFAULT;
  module_ctx->module_pw_max                   = MODULE_DEFAULT;
  module_ctx->module_pw_min                   = MODULE_DEFAULT;
  module_ctx->module_pwdump_column            = MODULE_DEFAULT;
  module_ctx->module_salt_max                 = MODULE_DEFAULT;
  module_ctx->module_salt_min                 = MODULE_DEFAULT;
  module_ctx->module_salt_type                = module_salt_type;
  module_ctx->module_separator                = MODULE_DEFAULT;
  module_ctx->module_st_hash                  = module_st_hash;
  module_ctx->module_st_pass                  = module_st_pass;
  module_ctx->module_tmp_size                 = module_tmp_size;
  module_ctx->module_unstable_warning         = MODULE_DEFAULT;
  module_ctx->module_warmup_disable           = MODULE_DEFAULT;
}

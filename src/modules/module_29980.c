/**
 * Private GPU module for libxcrypt gost-yescrypt ($gy$j9T$).
 *
 * The $gy$ construction is yescrypt followed by the Streebog-256 HMAC
 * wrapping implemented by libxcrypt.  This focused module supports the
 * current Linux login-password setting j9T (YESCRYPT_DEFAULTS, N=4096,
 * r=32, p=1, t=0, g=0, no ROM).
 */

#include <inttypes.h>
#include "common.h"
#include "types.h"
#include "modules.h"
#include "bitops.h"
#include "convert.h"
#include "shared.h"
#include "parser.h"
#include "memory.h"

static const u32   ATTACK_EXEC    = ATTACK_EXEC_OUTSIDE_KERNEL;
static const u32   DGST_POS0      = 0;
static const u32   DGST_POS1      = 1;
static const u32   DGST_POS2      = 2;
static const u32   DGST_POS3      = 3;
static const u32   DGST_SIZE      = DGST_SIZE_4_8;
static const u32   HASH_CATEGORY  = HASH_CATEGORY_OS;
static const char *HASH_NAME      = "gost-yescrypt ($gy$j9T$) [GPU/private]";
static const u64   KERN_TYPE      = 29980;
static const u32   OPTI_TYPE      = OPTI_TYPE_ZERO_BYTE | OPTI_TYPE_USES_BITS_64;
static const u64   OPTS_TYPE      = OPTS_TYPE_STOCK_MODULE
                                  | OPTS_TYPE_PT_GENERATE_LE
                                  | OPTS_TYPE_THREAD_MULTI_DISABLE
                                  | OPTS_TYPE_MP_MULTI_DISABLE;
static const u32   SALT_TYPE      = SALT_TYPE_EMBEDDED;
static const char *ST_PASS        = "fixed-but forgot to";
static const char *ST_HASH        = "$gy$j9T$uUiI.N7T3Tz6d3pvC.pnB0$If1AACG./CCB6LPJ/dZBgxzkRCBEvheOmpOcvxfYK36";

#define GY_N                  4096u
#define GY_R                  32u
#define GY_MEMORY_BYTES       (128ULL * GY_R * GY_N)
#define GY_TOTAL_LOOPS        5548u
#define GY_SETTING_MAX        96

typedef struct gy_esalt
{
  u32 setting_buf[GY_SETTING_MAX / 4];
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

u32         module_attack_exec    (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return ATTACK_EXEC;   }
u32         module_dgst_pos0      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_POS0;     }
u32         module_dgst_pos1      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_POS1;     }
u32         module_dgst_pos2      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_POS2;     }
u32         module_dgst_pos3      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_POS3;     }
u32         module_dgst_size      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_SIZE;     }
u32         module_hash_category  (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return HASH_CATEGORY; }
const char *module_hash_name      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return HASH_NAME;     }
u64         module_kern_type      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return KERN_TYPE;     }
u32         module_opti_type      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return OPTI_TYPE;     }
u64         module_opts_type      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return OPTS_TYPE;     }
u32         module_salt_type      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return SALT_TYPE;     }
const char *module_st_hash        (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return ST_HASH;       }
const char *module_st_pass        (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return ST_PASS;       }

u64 module_esalt_size (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return sizeof (gy_esalt_t);
}

u64 module_tmp_size (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return sizeof (gy_tmp_t);
}

u32 module_kernel_loops_min (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return 64;
}

u32 module_kernel_loops_max (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return 64;
}

u32 module_kernel_threads_max (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return 1;
}

const char *module_extra_tuningdb_block (MAYBE_UNUSED const hashconfig_t *hashconfig, const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra, const backend_ctx_t *backend_ctx, MAYBE_UNUSED const hashes_t *hashes, const u32 device_id, const u32 kernel_accel_user)
{
  hc_device_param_t *device_param = &backend_ctx->devices_param[device_id];

  const u64 fixed_mem = 256ULL * 1024 * 1024;
  const u64 available_mem = (device_param->device_available_mem > fixed_mem)
                          ? device_param->device_available_mem - fixed_mem
                          : GY_MEMORY_BYTES;
  const u64 allocation_mem = device_param->device_maxmem_alloc * 4;
  const u64 usable_mem = MIN (available_mem, allocation_mem);

  u32 accel = (u32) MIN ((u64) KERNEL_ACCEL_MAX, usable_mem / GY_MEMORY_BYTES);

  if (kernel_accel_user != 0) accel = MIN (accel, kernel_accel_user);
  if (accel == 0) accel = 1;

  char *device_name = hcstrdup (device_param->device_name);

  for (size_t i = 0; i < strlen (device_name); i++) if (device_name[i] == ' ') device_name[i] = '_';

  char *line = hcmalloc (4096);

  snprintf (line, 4096, "%s * %u 1 %u A\n", device_name, user_options->hash_mode, accel);

  hcfree (device_name);

  return line;
}

u64 module_extra_buffer_size (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra, MAYBE_UNUSED const hashes_t *hashes, const hc_device_param_t *device_param)
{
  return device_param->kernel_accel_max * GY_MEMORY_BYTES;
}

char *module_jit_build_options (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra, MAYBE_UNUSED const hashes_t *hashes, MAYBE_UNUSED const hc_device_param_t *device_param)
{
  return hcstrdup ("-D FIXED_LOCAL_SIZE=1 -D FORCE_DISABLE_SHM");
}

static int crypt64_value (const u8 c)
{
  static const char alphabet[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

  const char *p = strchr (alphabet, c);

  return p ? (int) (p - alphabet) : -1;
}

static int crypt64_decode (const u8 *src, const int src_len, u8 *dst, const int dst_size)
{
  int src_pos = 0;
  int dst_pos = 0;

  while (src_pos < src_len)
  {
    u32 value = 0;
    u32 bits = 0;

    while ((bits < 24) && (src_pos < src_len))
    {
      const int c = crypt64_value (src[src_pos++]);

      if (c < 0) return -1;

      value |= (u32) c << bits;
      bits += 6;
    }

    if (bits < 12) return -1;

    while (bits >= 8)
    {
      if (dst_pos >= dst_size) return -1;

      dst[dst_pos++] = (u8) value;
      value >>= 8;
      bits -= 8;
    }

    if (value != 0) return -1;
  }

  return dst_pos;
}

static int crypt64_encode (const u8 *src, const int src_len, u8 *dst)
{
  static const char alphabet[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

  int src_pos = 0;
  int dst_pos = 0;

  while (src_pos < src_len)
  {
    u32 value = 0;
    u32 bits = 0;

    while ((bits < 24) && (src_pos < src_len))
    {
      value |= (u32) src[src_pos++] << bits;
      bits += 8;
    }

    while (bits > 0)
    {
      dst[dst_pos++] = alphabet[value & 0x3f];
      value >>= 6;
      bits = (bits > 6) ? bits - 6 : 0;
    }
  }

  dst[dst_pos] = 0;

  return dst_pos;
}

int module_hash_decode (MAYBE_UNUSED const hashconfig_t *hashconfig, void *digest_buf, salt_t *salt, void *esalt_buf, MAYBE_UNUSED void *hook_salt_buf, MAYBE_UNUSED hashinfo_t *hash_info, const char *line_buf, const int line_len)
{
  u32 *digest = (u32 *) digest_buf;
  gy_esalt_t *esalt = (gy_esalt_t *) esalt_buf;

  hc_token_t token;

  memset (&token, 0, sizeof (token));

  token.token_cnt = 4;
  token.signatures_cnt = 1;
  token.signatures_buf[0] = "$gy$";

  token.len[0] = 4;
  token.attr[0] = TOKEN_ATTR_FIXED_LENGTH | TOKEN_ATTR_VERIFY_SIGNATURE;

  token.sep[1] = '$';
  token.len[1] = 3;
  token.attr[1] = TOKEN_ATTR_FIXED_LENGTH;

  token.sep[2] = '$';
  token.len_min[2] = 1;
  token.len_max[2] = 86;
  token.attr[2] = TOKEN_ATTR_VERIFY_LENGTH;

  token.len[3] = 43;
  token.attr[3] = TOKEN_ATTR_FIXED_LENGTH;

  const int rc = input_tokenizer ((const u8 *) line_buf, line_len, &token);

  if (rc != PARSER_OK) return rc;
  if (memcmp (token.buf[1], "j9T", 3) != 0) return PARSER_SALT_VALUE;

  u8 salt_raw[64] = { 0 };
  u8 digest_raw[32] = { 0 };

  const int salt_len = crypt64_decode (token.buf[2], token.len[2], salt_raw, sizeof (salt_raw));

  if (salt_len < 1) return PARSER_SALT_VALUE;
  if (crypt64_decode (token.buf[3], token.len[3], digest_raw, sizeof (digest_raw)) != 32) return PARSER_HASH_VALUE;

  memcpy (salt->salt_buf, salt_raw, salt_len);
  salt->salt_len = salt_len;
  salt->salt_iter = GY_TOTAL_LOOPS;
  salt->scrypt_N = GY_N;
  salt->scrypt_r = GY_R;
  salt->scrypt_p = 1;

  for (int i = 0; i < 8; i++)
  {
    digest[i] = ((u32) digest_raw[i * 4 + 0] <<  0)
              | ((u32) digest_raw[i * 4 + 1] <<  8)
              | ((u32) digest_raw[i * 4 + 2] << 16)
              | ((u32) digest_raw[i * 4 + 3] << 24);
  }

  esalt->setting_len = line_len - 44;

  if (esalt->setting_len > GY_SETTING_MAX) return PARSER_SALT_LENGTH;

  memcpy (esalt->setting_buf, line_buf, esalt->setting_len);

  return PARSER_OK;
}

int module_hash_encode (MAYBE_UNUSED const hashconfig_t *hashconfig, const void *digest_buf, MAYBE_UNUSED const salt_t *salt, const void *esalt_buf, MAYBE_UNUSED const void *hook_salt_buf, MAYBE_UNUSED const hashinfo_t *hash_info, char *line_buf, const int line_size)
{
  const u32 *digest = (const u32 *) digest_buf;
  const gy_esalt_t *esalt = (const gy_esalt_t *) esalt_buf;

  u8 digest_raw[32];
  u8 digest_b64[44];

  for (int i = 0; i < 8; i++)
  {
    digest_raw[i * 4 + 0] = digest[i] >>  0;
    digest_raw[i * 4 + 1] = digest[i] >>  8;
    digest_raw[i * 4 + 2] = digest[i] >> 16;
    digest_raw[i * 4 + 3] = digest[i] >> 24;
  }

  crypt64_encode (digest_raw, sizeof (digest_raw), digest_b64);

  return snprintf (line_buf, line_size, "%.*s$%s", esalt->setting_len, (const char *) esalt->setting_buf, digest_b64);
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
  module_ctx->module_bridge_name              = MODULE_DEFAULT;
  module_ctx->module_bridge_type              = MODULE_DEFAULT;
  module_ctx->module_build_plain_postprocess  = MODULE_DEFAULT;
  module_ctx->module_deep_comp_kernel         = MODULE_DEFAULT;
  module_ctx->module_deprecated_notice        = MODULE_DEFAULT;
  module_ctx->module_dgst_pos0                = module_dgst_pos0;
  module_ctx->module_dgst_pos1                = module_dgst_pos1;
  module_ctx->module_dgst_pos2                = module_dgst_pos2;
  module_ctx->module_dgst_pos3                = module_dgst_pos3;
  module_ctx->module_dgst_size                = module_dgst_size;
  module_ctx->module_esalt_size               = module_esalt_size;
  module_ctx->module_extra_buffer_size        = module_extra_buffer_size;
  module_ctx->module_extra_tmp_size           = MODULE_DEFAULT;
  module_ctx->module_extra_tuningdb_block     = module_extra_tuningdb_block;
  module_ctx->module_forced_outfile_format    = MODULE_DEFAULT;
  module_ctx->module_hash_binary_count        = MODULE_DEFAULT;
  module_ctx->module_hash_binary_parse        = MODULE_DEFAULT;
  module_ctx->module_hash_binary_save         = MODULE_DEFAULT;
  module_ctx->module_hash_decode_postprocess  = MODULE_DEFAULT;
  module_ctx->module_hash_decode_potfile      = MODULE_DEFAULT;
  module_ctx->module_hash_decode_zero_hash    = MODULE_DEFAULT;
  module_ctx->module_hash_decode              = module_hash_decode;
  module_ctx->module_hash_encode_status       = MODULE_DEFAULT;
  module_ctx->module_hash_encode_potfile      = MODULE_DEFAULT;
  module_ctx->module_hash_encode              = module_hash_encode;
  module_ctx->module_hash_init_selftest       = MODULE_DEFAULT;
  module_ctx->module_hash_mode                = MODULE_DEFAULT;
  module_ctx->module_hash_category            = module_hash_category;
  module_ctx->module_hash_name                = module_hash_name;
  module_ctx->module_hashes_count_min         = MODULE_DEFAULT;
  module_ctx->module_hashes_count_max         = MODULE_DEFAULT;
  module_ctx->module_hlfmt_disable            = MODULE_DEFAULT;
  module_ctx->module_hook_extra_param_size    = MODULE_DEFAULT;
  module_ctx->module_hook_extra_param_init    = MODULE_DEFAULT;
  module_ctx->module_hook_extra_param_term    = MODULE_DEFAULT;
  module_ctx->module_hook12                   = MODULE_DEFAULT;
  module_ctx->module_hook23                   = MODULE_DEFAULT;
  module_ctx->module_hook_salt_size           = MODULE_DEFAULT;
  module_ctx->module_hook_size                = MODULE_DEFAULT;
  module_ctx->module_jit_build_options        = module_jit_build_options;
  module_ctx->module_jit_cache_disable        = MODULE_DEFAULT;
  module_ctx->module_kernel_accel_max         = MODULE_DEFAULT;
  module_ctx->module_kernel_accel_min         = MODULE_DEFAULT;
  module_ctx->module_kernel_loops_max         = module_kernel_loops_max;
  module_ctx->module_kernel_loops_min         = module_kernel_loops_min;
  module_ctx->module_kernel_threads_max       = module_kernel_threads_max;
  module_ctx->module_kernel_threads_min       = MODULE_DEFAULT;
  module_ctx->module_kern_type                = module_kern_type;
  module_ctx->module_kern_type_dynamic        = MODULE_DEFAULT;
  module_ctx->module_opti_type                = module_opti_type;
  module_ctx->module_opts_type                = module_opts_type;
  module_ctx->module_outfile_check_disable    = MODULE_DEFAULT;
  module_ctx->module_outfile_check_nocomp     = MODULE_DEFAULT;
  module_ctx->module_potfile_custom_check     = MODULE_DEFAULT;
  module_ctx->module_potfile_disable          = MODULE_DEFAULT;
  module_ctx->module_potfile_keep_all_hashes  = MODULE_DEFAULT;
  module_ctx->module_pwdump_column            = MODULE_DEFAULT;
  module_ctx->module_pw_max                   = MODULE_DEFAULT;
  module_ctx->module_pw_min                   = MODULE_DEFAULT;
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

/**
 * phpBB 3.1+ legacy-password rehashes.
 *
 * phpBB marks a phpass hash rehashed through bcrypt as:
 *
 *   $H\2y$<phpass-count><phpass-salt>$<bcrypt-cost>\<bcrypt-salt>$<bcrypt-digest>
 *
 * The outer bcrypt password is only the 22-character phpass checksum. The
 * $H$ marker, count character, and eight-byte phpass salt are not included.
 */

#include "common.h"
#include "types.h"
#include "modules.h"
#include "bitops.h"
#include "convert.h"
#include "shared.h"
#include "parser.h"

static const u32   ATTACK_EXEC    = ATTACK_EXEC_OUTSIDE_KERNEL;
static const u32   DGST_POS0      = 0;
static const u32   DGST_POS1      = 1;
static const u32   DGST_POS2      = 2;
static const u32   DGST_POS3      = 3;
static const u32   DGST_SIZE      = DGST_SIZE_4_6;
static const u32   HASH_CATEGORY  = HASH_CATEGORY_FORUM_SOFTWARE;
#if PHPBB3_BCRYPT_PREHASH_MD5
static const char *HASH_NAME      = "phpBB3 rehash [bcrypt(phpass(md5($pass)))]";
#else
static const char *HASH_NAME      = "phpBB3 rehash [bcrypt(phpass($pass))]";
#endif
static const u64   KERN_TYPE      = PHPBB3_BCRYPT_HASH_MODE;
static const u32   OPTI_TYPE      = OPTI_TYPE_ZERO_BYTE;
static const u64   OPTS_TYPE      = OPTS_TYPE_STOCK_MODULE
                                  | OPTS_TYPE_PT_GENERATE_LE
                                  | OPTS_TYPE_INIT2
                                  | OPTS_TYPE_LOOP2
                                  | OPTS_TYPE_DYNAMIC_SHARED;
static const u32   SALT_TYPE      = SALT_TYPE_EMBEDDED;
static const char *ST_PASS        = "123456";
#if PHPBB3_BCRYPT_PREHASH_MD5
static const char *ST_HASH        = "$H\\2y$7RsqOrLNk$10\\vw31ldi5VPlyG2t5HxqIKe$Aevp.Ptx1lbafMKDSGRhyMEDsrf4Sda";
#else
static const char *ST_HASH        = "$H\\2y$7RsqOrLNk$10\\vw31ldi5VPlyG2t5HxqIKe$yFv5dvOFakbr/GBBC0oyfIGMOkSD1dq";
#endif

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

#include "blowfish_common.c"

static const char *SIGNATURE_BCRYPT1 = "$2a$";
static const char *SIGNATURE_BCRYPT2 = "$2b$";
static const char *SIGNATURE_BCRYPT3 = "$2x$";
static const char *SIGNATURE_BCRYPT4 = "$2y$";
static const char *ITOA64 = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

u32         module_attack_exec    (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return ATTACK_EXEC; }
u32         module_dgst_pos0      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_POS0; }
u32         module_dgst_pos1      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_POS1; }
u32         module_dgst_pos2      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_POS2; }
u32         module_dgst_pos3      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_POS3; }
u32         module_dgst_size      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return DGST_SIZE; }
u32         module_hash_category  (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return HASH_CATEGORY; }
const char *module_hash_name      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return HASH_NAME; }
u64         module_kern_type      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return KERN_TYPE; }
u32         module_opti_type      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return OPTI_TYPE; }
u64         module_opts_type      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return OPTS_TYPE; }
u32         module_salt_type      (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return SALT_TYPE; }
const char *module_st_hash        (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return ST_HASH; }
const char *module_st_pass        (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra) { return ST_PASS; }

u64 module_tmp_size (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return (const u64) sizeof (phpbb3_bcrypt_tmp_t);
}

u32 module_pw_max (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return PW_MAX;
}

// The normal outside-kernel autotuner measures the first loop stage. For
// these modes that stage is inexpensive phpass MD5, while loop2 is bcrypt.
// Without mode-specific limits the MD5 timing can select Accel:488 and
// Loops:1024 on an RTX 4090, creating multi-minute bcrypt launches that delay
// status, runtime, quit, and checkpoints. Measurements on the target 4090s
// show that eight queued waves and eight rounds per launch retain peak
// completed-candidate throughput while keeping every control responsive.

u32 module_kernel_accel_max (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return 8;
}

u32 module_kernel_loops_min (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return 8;
}

u32 module_kernel_loops_max (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return 8;
}

static bool is_bcrypt_variant (const char c)
{
  return (c == 'a') || (c == 'b') || (c == 'x') || (c == 'y');
}

static int parse_phpbb3_rehash (const char *line_buf, const int line_len, char bcrypt[61], u8 phpass_setting[9])
{
  if (line_len == 73)
  {
    if ((line_buf[0] != '$') || (line_buf[1] != 'H') || (line_buf[2] != '\\')) return PARSER_SIGNATURE_UNMATCHED;
    if ((line_buf[3] != '2') || (is_bcrypt_variant (line_buf[4]) == false) || (line_buf[5] != '$')) return PARSER_SIGNATURE_UNMATCHED;
    if ((line_buf[15] != '$') || (line_buf[18] != '\\') || (line_buf[41] != '$')) return PARSER_SEPARATOR_UNMATCHED;

    bcrypt[0] = '$';
    bcrypt[1] = line_buf[3];
    bcrypt[2] = line_buf[4];
    bcrypt[3] = '$';
    bcrypt[4] = line_buf[16];
    bcrypt[5] = line_buf[17];
    bcrypt[6] = '$';

    memcpy (bcrypt + 7,  line_buf + 19, 22);
    memcpy (bcrypt + 29, line_buf + 42, 31);

    bcrypt[60] = 0;

    memcpy (phpass_setting, line_buf + 6, 9);

    return PARSER_OK;
  }

  // Also accept the useful extracted representation:
  // $2y$10$<22-byte-bcrypt-salt><31-byte-digest>:<count><8-byte-phpass-salt>

  if ((line_len == 70) && (line_buf[60] == ':'))
  {
    memcpy (bcrypt, line_buf, 60);

    bcrypt[60] = 0;

    memcpy (phpass_setting, line_buf + 61, 9);

    return PARSER_OK;
  }

  return PARSER_HASH_LENGTH;
}

int module_hash_decode (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED void *digest_buf, MAYBE_UNUSED salt_t *salt, MAYBE_UNUSED void *esalt_buf, MAYBE_UNUSED void *hook_salt_buf, MAYBE_UNUSED hashinfo_t *hash_info, const char *line_buf, const int line_len)
{
  u32 *digest = (u32 *) digest_buf;

  char bcrypt[61];
  u8 phpass_setting[9];

  const int rc_rehash = parse_phpbb3_rehash (line_buf, line_len, bcrypt, phpass_setting);

  if (rc_rehash != PARSER_OK) return rc_rehash;

  hc_token_t token;

  memset (&token, 0, sizeof (hc_token_t));

  token.token_cnt = 4;

  token.signatures_cnt    = 4;
  token.signatures_buf[0] = SIGNATURE_BCRYPT1;
  token.signatures_buf[1] = SIGNATURE_BCRYPT2;
  token.signatures_buf[2] = SIGNATURE_BCRYPT3;
  token.signatures_buf[3] = SIGNATURE_BCRYPT4;

  token.len[0]  = 4;
  token.attr[0] = TOKEN_ATTR_FIXED_LENGTH | TOKEN_ATTR_VERIFY_SIGNATURE;

  token.sep[1]  = '$';
  token.len[1]  = 2;
  token.attr[1] = TOKEN_ATTR_FIXED_LENGTH | TOKEN_ATTR_VERIFY_DIGIT;

  token.len[2]  = 22;
  token.attr[2] = TOKEN_ATTR_FIXED_LENGTH | TOKEN_ATTR_VERIFY_BASE64B;

  token.len[3]  = 31;
  token.attr[3] = TOKEN_ATTR_FIXED_LENGTH | TOKEN_ATTR_VERIFY_BASE64B;

  const int rc_tokenizer = input_tokenizer ((const u8 *) bcrypt, 60, &token);

  if (rc_tokenizer != PARSER_OK) return rc_tokenizer;

  const u32 bcrypt_cost = hc_strtoul ((const char *) token.buf[1], NULL, 10);

  if ((bcrypt_cost < 4) || (bcrypt_cost > 31)) return PARSER_SALT_ITERATION;

  const char *count_ptr = strchr (ITOA64, phpass_setting[0]);

  if (count_ptr == NULL) return PARSER_SALT_ITERATION;

  const u32 phpass_count_log2 = (u32) (count_ptr - ITOA64);

  if ((phpass_count_log2 < 7) || (phpass_count_log2 > 30)) return PARSER_SALT_ITERATION;

  for (int i = 1; i < 9; i++)
  {
    if (strchr (ITOA64, phpass_setting[i]) == NULL) return PARSER_SALT_VALUE;
  }

  const u8 *bcrypt_salt = token.buf[2];
  const u8 *bcrypt_hash = token.buf[3];

  salt->salt_len    = 16;
  salt->salt_len_pc = 8;
  salt->salt_iter   = 1u << phpass_count_log2;
  salt->salt_iter2  = 1u << bcrypt_cost;

  memcpy ((u8 *) salt->salt_sign, bcrypt, 4);

  salt->salt_sign[1] = PHPBB3_BCRYPT_PREHASH_MD5;

  memcpy ((u8 *) salt->salt_buf_pc, phpass_setting + 1, 8);

  u8 tmp_buf[100] = { 0 };

  base64_decode (bf64_to_int, bcrypt_salt, 22, tmp_buf);

  memcpy ((u8 *) salt->salt_buf, tmp_buf, 16);

  salt->salt_buf[0] = byte_swap_32 (salt->salt_buf[0]);
  salt->salt_buf[1] = byte_swap_32 (salt->salt_buf[1]);
  salt->salt_buf[2] = byte_swap_32 (salt->salt_buf[2]);
  salt->salt_buf[3] = byte_swap_32 (salt->salt_buf[3]);

  // hashcat's salt grouping compares salt_buf but not salt_iter2 or
  // salt_sign. Preserve the outer bcrypt parameters after the 16-byte salt so
  // equal salts with different costs or variants remain independent. The
  // bcrypt kernels and encoder intentionally consume only the first 4 words.

  salt->salt_buf[4] = bcrypt_cost;
  salt->salt_buf[5] = (u32) bcrypt[2];

  memset (tmp_buf, 0, sizeof (tmp_buf));

  base64_decode (bf64_to_int, bcrypt_hash, 31, tmp_buf);

  memcpy (digest, tmp_buf, 24);

  digest[0] = byte_swap_32 (digest[0]);
  digest[1] = byte_swap_32 (digest[1]);
  digest[2] = byte_swap_32 (digest[2]);
  digest[3] = byte_swap_32 (digest[3]);
  digest[4] = byte_swap_32 (digest[4]);
  digest[5] = byte_swap_32 (digest[5]);

  digest[5] &= ~0xffu;

  return PARSER_OK;
}

int module_hash_encode (MAYBE_UNUSED const hashconfig_t *hashconfig, const void *digest_buf, const salt_t *salt, MAYBE_UNUSED const void *esalt_buf, MAYBE_UNUSED const void *hook_salt_buf, MAYBE_UNUSED const hashinfo_t *hash_info, char *line_buf, const int line_size)
{
  const u32 *digest = (const u32 *) digest_buf;

  u32 tmp_digest[6];

  tmp_digest[0] = byte_swap_32 (digest[0]);
  tmp_digest[1] = byte_swap_32 (digest[1]);
  tmp_digest[2] = byte_swap_32 (digest[2]);
  tmp_digest[3] = byte_swap_32 (digest[3]);
  tmp_digest[4] = byte_swap_32 (digest[4]);
  tmp_digest[5] = byte_swap_32 (digest[5]);

  u32 tmp_salt[4];

  tmp_salt[0] = byte_swap_32 (salt->salt_buf[0]);
  tmp_salt[1] = byte_swap_32 (salt->salt_buf[1]);
  tmp_salt[2] = byte_swap_32 (salt->salt_buf[2]);
  tmp_salt[3] = byte_swap_32 (salt->salt_buf[3]);

  char tmp_buf[64];

  base64_encode (int_to_bf64, (const u8 *) tmp_salt,   16, (u8 *) tmp_buf + 0);
  base64_encode (int_to_bf64, (const u8 *) tmp_digest, 23, (u8 *) tmp_buf + 22);

  tmp_buf[53] = 0;

  u32 phpass_count_log2 = 0;
  u32 bcrypt_cost = 0;

  for (u32 iter = salt->salt_iter;  iter > 1; iter >>= 1) phpass_count_log2++;
  for (u32 iter = salt->salt_iter2; iter > 1; iter >>= 1) bcrypt_cost++;

  const char *bcrypt_sign = (const char *) salt->salt_sign;
  const char phpass_count = int_to_itoa64 ((u8) phpass_count_log2);

  return snprintf (line_buf, line_size, "$H\\%c%c$%c%.8s$%02u\\%.22s$%.31s",
                   bcrypt_sign[1], bcrypt_sign[2], phpass_count,
                   (const char *) salt->salt_buf_pc, bcrypt_cost,
                   tmp_buf, tmp_buf + 22);
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
  module_ctx->module_esalt_size               = MODULE_DEFAULT;
  module_ctx->module_extra_buffer_size        = MODULE_DEFAULT;
  module_ctx->module_extra_tmp_size           = MODULE_DEFAULT;
  module_ctx->module_extra_tuningdb_block     = MODULE_DEFAULT;
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
  module_ctx->module_jit_build_options        = blowfish_module_jit_build_options;
  module_ctx->module_jit_cache_disable        = MODULE_DEFAULT;
  module_ctx->module_kernel_accel_max         = module_kernel_accel_max;
  module_ctx->module_kernel_accel_min         = MODULE_DEFAULT;
  module_ctx->module_kernel_loops_max         = module_kernel_loops_max;
  module_ctx->module_kernel_loops_min         = module_kernel_loops_min;
  module_ctx->module_kernel_threads_max       = MODULE_DEFAULT;
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
  module_ctx->module_pw_max                   = module_pw_max;
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

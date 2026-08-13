/**
 * mdxfind-compatible Argon2 wrapper.
 *
 * In addition to standard PHC strings, mdxfind e987 accepts Magento's
 * Argon2id spelling:
 *
 *   hex_digest:salt:2
 *   hex_digest:salt:3_digest_length_iterations_memory_bytes
 *
 * mdxfind uses the first 16 salt characters, Argon2 version 19, and one
 * lane.  Convert that spelling to the standard form understood by hashcat's
 * existing mode 34000 parser while keeping the original line for output.
 */

#ifndef MDXFIND_ARGON2_MODULE_H
#define MDXFIND_ARGON2_MODULE_H

#if !defined (MDXFIND_MODE_ID) || !defined (MDXFIND_MODE_NAME)
#error "MDXFIND_MODE_ID and MDXFIND_MODE_NAME must be defined"
#endif

#define MDXFIND_ARGON2_STRINGIFY_INNER(x) #x
#define MDXFIND_ARGON2_STRINGIFY(x) MDXFIND_ARGON2_STRINGIFY_INNER (x)

static int mdxfind_argon2_hash_decode (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED void *digest_buf, MAYBE_UNUSED salt_t *salt, MAYBE_UNUSED void *esalt_buf, MAYBE_UNUSED void *hook_salt_buf, MAYBE_UNUSED hashinfo_t *hash_info, const char *line_buf, MAYBE_UNUSED const int line_len)
{
  if ((line_len >= 9) && (memcmp (line_buf, "$argon2", 7) == 0))
  {
    return module_hash_decode (hashconfig, digest_buf, salt, esalt_buf, hook_salt_buf, hash_info, line_buf, line_len);
  }

  const char *separator1 = memchr (line_buf, ':', line_len);

  if (separator1 == NULL) return (PARSER_SEPARATOR_UNMATCHED);

  const int digest_hex_len = (int) (separator1 - line_buf);

  if ((digest_hex_len < 2) || (digest_hex_len > 256) || (digest_hex_len & 1)) return (PARSER_HASH_LENGTH);
  if (is_valid_hex_string ((const u8 *) line_buf, digest_hex_len) == false) return (PARSER_HASH_ENCODING);

  const char *salt_pos = separator1 + 1;
  const int remaining_len = line_len - (int) (salt_pos - line_buf);
  const char *separator2 = memchr (salt_pos, ':', remaining_len);

  if (separator2 == NULL) return (PARSER_SEPARATOR_UNMATCHED);

  const int salt_field_len = (int) (separator2 - salt_pos);

  if (salt_field_len < 1) return (PARSER_SALT_LENGTH);

  const char *version_pos = separator2 + 1;
  const int version_len = line_len - (int) (version_pos - line_buf);

  if ((version_len < 1) || (version_len > 63)) return (PARSER_HASH_VALUE);
  if (memchr (version_pos, ':', version_len) != NULL) return (PARSER_SEPARATOR_UNMATCHED);
  if ((version_pos[0] != '2') && (version_pos[0] != '3')) return (PARSER_HASH_VALUE);

  const int digest_len = digest_hex_len / 2;

  int iterations = 2;
  int memory_kib = 65536;

  if (version_len > 1)
  {
    if (version_pos[1] != '_') return (PARSER_HASH_VALUE);

    char parameters[64];

    memcpy (parameters, version_pos + 2, version_len - 2);
    parameters[version_len - 2] = 0;

    int encoded_digest_len = 0;
    int consumed = 0;
    long long memory_bytes = 0;

    if (sscanf (parameters, "%d_%d_%lld%n", &encoded_digest_len, &iterations, &memory_bytes, &consumed) != 3) return (PARSER_HASH_VALUE);
    if (consumed != (version_len - 2)) return (PARSER_HASH_VALUE);
    if (encoded_digest_len != digest_len) return (PARSER_HASH_LENGTH);
    if (iterations < 1) return (PARSER_HASH_VALUE);
    if ((memory_bytes < 8192) || ((memory_bytes / 1024) > 2147483647LL)) return (PARSER_HASH_VALUE);

    memory_kib = (int) (memory_bytes / 1024);
  }

  u8 digest[128];

  hex_decode ((const u8 *) line_buf, digest_hex_len, digest);

  const int salt_len = MIN (salt_field_len, 16);

  char salt_base64[32];
  char digest_base64[176];

  int salt_base64_len = (int) base64_encode (int_to_base64, (const u8 *) salt_pos, salt_len, (u8 *) salt_base64);
  int digest_base64_len = (int) base64_encode (int_to_base64, digest, digest_len, (u8 *) digest_base64);

  while ((salt_base64_len > 0) && (salt_base64[salt_base64_len - 1] == '=')) salt_base64_len--;
  while ((digest_base64_len > 0) && (digest_base64[digest_base64_len - 1] == '=')) digest_base64_len--;

  salt_base64[salt_base64_len] = 0;
  digest_base64[digest_base64_len] = 0;

  char standard_hash[512];

  const int standard_hash_len = snprintf (standard_hash, sizeof (standard_hash), "$argon2id$v=19$m=%d,t=%d,p=1$%s$%s", memory_kib, iterations, salt_base64, digest_base64);

  if ((standard_hash_len < 0) || (standard_hash_len >= (int) sizeof (standard_hash))) return (PARSER_HASH_LENGTH);

  return module_hash_decode (hashconfig, digest_buf, salt, esalt_buf, hook_salt_buf, hash_info, standard_hash, standard_hash_len);
}

static int mdxfind_argon2_hash_encode (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const void *digest_buf, MAYBE_UNUSED const salt_t *salt, MAYBE_UNUSED const void *esalt_buf, MAYBE_UNUSED const void *hook_salt_buf, MAYBE_UNUSED const hashinfo_t *hash_info, char *line_buf, MAYBE_UNUSED const int line_size)
{
  if ((hash_info != NULL) && (hash_info->orighash != NULL))
  {
    return snprintf (line_buf, line_size, "%s", hash_info->orighash);
  }

  return module_hash_encode (hashconfig, digest_buf, salt, esalt_buf, hook_salt_buf, hash_info, line_buf, line_size);
}

static u64 mdxfind_argon2_opts_type (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return module_opts_type (hashconfig, user_options, user_options_extra) | OPTS_TYPE_HASH_COPY;
}

static int mdxfind_argon2_hash_mode (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return MDXFIND_HASH_MODE_FROM_ID (MDXFIND_MODE_ID);
}

static const char *mdxfind_argon2_hash_name (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return "mdxfind e" MDXFIND_ARGON2_STRINGIFY (MDXFIND_MODE_ID) " " MDXFIND_MODE_NAME;
}

void module_init (module_ctx_t *module_ctx)
{
  mdxfind_base_module_init (module_ctx);

  module_ctx->module_hash_decode = mdxfind_argon2_hash_decode;
  module_ctx->module_hash_encode = mdxfind_argon2_hash_encode;
  module_ctx->module_hash_mode   = mdxfind_argon2_hash_mode;
  module_ctx->module_hash_name   = mdxfind_argon2_hash_name;
  module_ctx->module_opts_type   = mdxfind_argon2_opts_type;
}

#endif

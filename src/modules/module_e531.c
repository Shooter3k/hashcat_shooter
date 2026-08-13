/**
 * Auto-generated mdxfind alias module: e531 PBKDF2-MD5
 * Source: tools/generate_mdxfind_modules.py
 *
 * The existing hashcat module is included unchanged.  This file only gives
 * that implementation an mdxfind-compatible module filename.
 */

#include "common.h"
#include "types.h"
#include "modules.h"
#include "mdxfind_modes.h"

#define module_init mdxfind_base_module_init
#include "module_11900.c"
#undef module_init

static const char *MDXFIND_HASH_NAME = "mdxfind e531 PBKDF2-MD5";

static int mdxfind_alias_hash_mode (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return MDXFIND_HASH_MODE_FROM_ID (531);
}

static const char *mdxfind_alias_hash_name (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{
  return MDXFIND_HASH_NAME;
}

void module_init (module_ctx_t *module_ctx)
{
  mdxfind_base_module_init (module_ctx);

  module_ctx->module_hash_mode = mdxfind_alias_hash_mode;
  module_ctx->module_hash_name = mdxfind_alias_hash_name;
}

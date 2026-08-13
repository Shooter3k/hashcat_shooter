/**
 * Auto-generated specialized mdxfind alias: e987 ARGON2
 * Source: tools/generate_mdxfind_modules.py
 *
 * The compatibility header preserves mdxfind-specific input formats before
 * delegating computation to the existing hashcat module.
 */

#include "common.h"
#include "types.h"
#include "modules.h"
#include "mdxfind_modes.h"

#define module_init mdxfind_base_module_init
#include "module_34000.c"
#undef module_init

#define MDXFIND_MODE_ID 987
#define MDXFIND_MODE_NAME "ARGON2"
#include "mdxfind_argon2_module.h"

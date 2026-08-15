/**
 * Native CPU bridge for mdxfind eN expression modules.
 *
 * Hashpipe's tested implementations are used when available.  The checked-in
 * mdxfind expression VM remains as a fallback for registry entries that do
 * not have a Hashpipe handler.  No external process is launched and
 * candidates never leave hashcat's address space.
 */

#include "common.h"
#include "types.h"
#include "bridges.h"
#include "event.h"
#include "memory.h"
#include "shared.h"
#include "system.h"
#include "mdxfind_modes.h"

#include "mdxfind/hx_vm.h"
#include "mdxfind/codegen/hx_spec_entry.h"
#include "mdxfind/hashpipe/hashpipe_engine.h"

#define MDXFIND_TEXT_MAX 1024
#define MDXFIND_FIELD_MAX 256
#define MDXFIND_FIELD_COUNT 5
#define MDXFIND_ACCEL 32

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

typedef struct mdxfind_unit
{
  char unit_info[128];

  hx_vm vm;
  void *hashpipe_workspace;

} mdxfind_unit_t;

typedef struct bridge_mdxfind
{
  hx_program program;

  int mdxfind_id;
  bool use_hashpipe;

  mdxfind_unit_t *units;
  int units_cnt;

} bridge_mdxfind_t;

void platform_term (hashcat_ctx_t *hashcat_ctx, void *platform_context);

static bool mdxfind_program_init (hashcat_ctx_t *hashcat_ctx, bridge_mdxfind_t *bridge)
{
  const int hash_mode = hashcat_ctx->user_options->hash_mode;

  if (MDXFIND_HASH_MODE_IS_NAMED (hash_mode) == false) return false;

  const int mdxfind_id = MDXFIND_HASH_MODE_TO_ID (hash_mode);

  const struct hx_spec_entry *entry = hx_specs_lookup (mdxfind_id);

  if ((entry == NULL) || (entry->program == NULL) || (entry->is_outlier != 0) || (entry->compile_failed != 0))
  {
    event_log_error (hashcat_ctx, "mdxfind e%d has no expression-VM program and no native hashcat alias.", mdxfind_id);

    return false;
  }

  bridge->program = *entry->program;

  bridge->program.code = (hx_inst *) hcmalloc ((size_t) bridge->program.ncode * sizeof (hx_inst));

  memcpy (bridge->program.code, entry->program->code, (size_t) bridge->program.ncode * sizeof (hx_inst));

  for (int idx = 0; idx < bridge->program.ncode; idx++)
  {
    hx_inst *instruction = &bridge->program.code[idx];

    if (instruction->op != OP_CALL) continue;

    const char *call_name = (entry->call_names != NULL) ? entry->call_names[idx] : NULL;

    if (call_name == NULL)
    {
      event_log_error (hashcat_ctx, "mdxfind e%d has an unresolved expression call at instruction %d.", mdxfind_id, idx);

      return false;
    }

    instruction->u.call.entry = hx_func_lookup (call_name);

    if (instruction->u.call.entry == NULL)
    {
      event_log_error (hashcat_ctx, "mdxfind e%d requires primitive '%s', which is not available in this bridge build.", mdxfind_id, call_name);

      return false;
    }
  }

  return true;
}

void *platform_init (hashcat_ctx_t *hashcat_ctx)
{
  bridge_mdxfind_t *bridge = (bridge_mdxfind_t *) hccalloc (1, sizeof (*bridge));

  bridge->mdxfind_id = MDXFIND_HASH_MODE_TO_ID (hashcat_ctx->user_options->hash_mode);

  hp_engine_init ();

  bridge->use_hashpipe = hp_engine_has_handler (bridge->mdxfind_id) != 0;

  if ((bridge->use_hashpipe == false) && (mdxfind_program_init (hashcat_ctx, bridge) == false))
  {
    hcfree (bridge->program.code);
    hcfree (bridge);

    return NULL;
  }

  int units_cnt = hc_get_processor_count ();

  units_cnt = MAX (units_cnt, 1);

  bridge->units = (mdxfind_unit_t *) hccalloc (units_cnt, sizeof (mdxfind_unit_t));
  bridge->units_cnt = units_cnt;

  for (int unit_idx = 0; unit_idx < units_cnt; unit_idx++)
  {
    mdxfind_unit_t *unit = &bridge->units[unit_idx];

    if (bridge->use_hashpipe == true)
    {
      snprintf (unit->unit_info, sizeof (unit->unit_info), "mdxfind e%d Hashpipe verifier (CPU)", bridge->mdxfind_id);

      unit->hashpipe_workspace = hp_engine_workspace_create ();

      if (unit->hashpipe_workspace == NULL)
      {
        event_log_error (hashcat_ctx, "mdxfind e%d could not allocate a Hashpipe workspace.", bridge->mdxfind_id);

        platform_term (hashcat_ctx, bridge);

        return NULL;
      }
    }
    else
    {
      snprintf (unit->unit_info, sizeof (unit->unit_info), "mdxfind e%d expression VM fallback (CPU)", bridge->mdxfind_id);

      hx_vm_init (&unit->vm, &bridge->program);
    }
  }

  return bridge;
}

void platform_term (MAYBE_UNUSED hashcat_ctx_t *hashcat_ctx, void *platform_context)
{
  bridge_mdxfind_t *bridge = (bridge_mdxfind_t *) platform_context;

  if (bridge == NULL) return;

  for (int unit_idx = 0; unit_idx < bridge->units_cnt; unit_idx++)
  {
    if (bridge->use_hashpipe == true)
      hp_engine_workspace_destroy (bridge->units[unit_idx].hashpipe_workspace);
    else
      hx_vm_free (&bridge->units[unit_idx].vm);
  }

  hcfree (bridge->units);
  hcfree (bridge->program.code);
  hcfree (bridge);
}

int get_unit_count (MAYBE_UNUSED hashcat_ctx_t *hashcat_ctx, void *platform_context)
{
  const bridge_mdxfind_t *bridge = (const bridge_mdxfind_t *) platform_context;

  return bridge->units_cnt;
}

char *get_unit_info (MAYBE_UNUSED hashcat_ctx_t *hashcat_ctx, void *platform_context, const int unit_idx)
{
  bridge_mdxfind_t *bridge = (bridge_mdxfind_t *) platform_context;

  return bridge->units[unit_idx].unit_info;
}

int get_workitem_count (MAYBE_UNUSED hashcat_ctx_t *hashcat_ctx, MAYBE_UNUSED void *platform_context, MAYBE_UNUSED const int unit_idx)
{
  return MDXFIND_ACCEL;
}

int get_workitem_multiple (MAYBE_UNUSED hashcat_ctx_t *hashcat_ctx, MAYBE_UNUSED void *platform_context, MAYBE_UNUSED const int unit_idx)
{
  return 1;
}

bool launch_loop (MAYBE_UNUSED hashcat_ctx_t *hashcat_ctx, void *platform_context, hc_device_param_t *device_param, MAYBE_UNUSED hashconfig_t *hashconfig, hashes_t *hashes, const u32 salt_pos, const u64 pws_cnt)
{
  bridge_mdxfind_t *bridge = (bridge_mdxfind_t *) platform_context;

  mdxfind_unit_t *unit = &bridge->units[device_param->bridge_link_device];

  mdxfind_esalt_t *esalts = (mdxfind_esalt_t *) hashes->esalts_buf;
  mdxfind_esalt_t *esalt = &esalts[salt_pos];

  mdxfind_tmp_t *tmps = (mdxfind_tmp_t *) device_param->h_tmps;

  const char *salt = (esalt->field_cnt > 0) ? (const char *) esalt->field_buf[0] : "";
  const int salt_len = (esalt->field_cnt > 0) ? (int) esalt->field_len[0] : 0;
  const char *salt2 = (esalt->field_cnt > 1) ? (const char *) esalt->field_buf[1] : "";
  const int salt2_len = (esalt->field_cnt > 1) ? (int) esalt->field_len[1] : 0;
  const char *pepper = (esalt->field_cnt > 2) ? (const char *) esalt->field_buf[2] : "";
  const int pepper_len = (esalt->field_cnt > 2) ? (int) esalt->field_len[2] : 0;
  const char *user = (esalt->field_cnt > 3) ? (const char *) esalt->field_buf[3] : "";
  const int user_len = (esalt->field_cnt > 3) ? (int) esalt->field_len[3] : 0;

  const u32 suffix_len = esalt->target_len - esalt->suffix_pos;
  const u8 *suffix = esalt->target_buf + esalt->suffix_pos;

  for (u64 idx = 0; idx < pws_cnt; idx++)
  {
    mdxfind_tmp_t *tmp = &tmps[idx];

    tmp->out_cnt = 0;

    if (bridge->use_hashpipe == true)
    {
      const int matched = hp_engine_verify (unit->hashpipe_workspace,
                                            bridge->mdxfind_id,
                                            (const char *) esalt->target_buf,
                                            (int) esalt->target_len,
                                            (const u8 *) tmp->pw_buf,
                                            (int) tmp->pw_len);

      if (matched == 0) continue;
      if (esalt->target_len > sizeof (tmp->out_buf[0])) continue;

      memcpy (tmp->out_buf[0], esalt->target_buf, esalt->target_len);

      tmp->out_len[0] = esalt->target_len;
      tmp->out_cnt = 1;

      continue;
    }

    hx_val result = hx_vm_run (&unit->vm,
                               (const char *) tmp->pw_buf, (int) tmp->pw_len,
                               salt, salt_len,
                               salt2, salt2_len,
                               pepper, pepper_len,
                               user, user_len);

    if ((result.data == NULL) || (result.len <= 0)) continue;

    const u32 output_len = (u32) result.len + suffix_len;

    if (output_len > sizeof (tmp->out_buf[0])) continue;

    memcpy (tmp->out_buf[0], result.data, result.len);
    memcpy ((u8 *) tmp->out_buf[0] + result.len, suffix, suffix_len);

    tmp->out_len[0] = output_len;
    tmp->out_cnt = 1;
  }

  return true;
}

#if defined (_WIN32)
__declspec (dllexport)
#endif
void bridge_init (bridge_ctx_t *bridge_ctx)
{
  bridge_ctx->bridge_context_size       = BRIDGE_CONTEXT_SIZE_CURRENT;
  bridge_ctx->bridge_interface_version  = BRIDGE_INTERFACE_VERSION_CURRENT;

  bridge_ctx->platform_init         = platform_init;
  bridge_ctx->platform_term         = platform_term;
  bridge_ctx->get_unit_count        = get_unit_count;
  bridge_ctx->get_unit_info         = get_unit_info;
  bridge_ctx->get_workitem_count    = get_workitem_count;
  bridge_ctx->get_workitem_multiple = get_workitem_multiple;
  bridge_ctx->thread_init           = BRIDGE_DEFAULT;
  bridge_ctx->thread_term           = BRIDGE_DEFAULT;
  bridge_ctx->salt_prepare          = BRIDGE_DEFAULT;
  bridge_ctx->salt_destroy          = BRIDGE_DEFAULT;
  bridge_ctx->launch_loop           = launch_loop;
  bridge_ctx->launch_loop2          = BRIDGE_DEFAULT;
  bridge_ctx->st_update_hash        = BRIDGE_DEFAULT;
  bridge_ctx->st_update_pass        = BRIDGE_DEFAULT;

  bridge_ctx->get_unit_temperature       = BRIDGE_DEFAULT;
  bridge_ctx->get_unit_temperature_str   = BRIDGE_DEFAULT;
  bridge_ctx->get_unit_temperature_abort = BRIDGE_DEFAULT;
  bridge_ctx->get_unit_fanspeed          = BRIDGE_DEFAULT;
  bridge_ctx->get_unit_utilization       = BRIDGE_DEFAULT;
  bridge_ctx->get_unit_corespeed         = BRIDGE_DEFAULT;
  bridge_ctx->get_unit_memoryspeed       = BRIDGE_DEFAULT;
  bridge_ctx->get_unit_buslanes          = BRIDGE_DEFAULT;
  bridge_ctx->get_unit_power             = BRIDGE_DEFAULT;
}

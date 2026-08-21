/**
 * Author......: See docs/credits.txt
 * License.....: MIT
 */

// A deterministic probabilistic context-free grammar feed.
//
// The companion tools/train_pcfg.py compiler learns two distributions from plaintext passwords:
// class structures (for example U1,L7,D4) and terminal strings for every class/length token.  This
// feed merges every structure's Cartesian product in increasing negative-log probability order.
// The ordering is deterministic, which is required for --skip, --restore and multiple devices.

#define XXH_INLINE_ALL

#include "xxhash.h"

#include "common.h"
#include "types.h"
#include "memory.h"
#include "filehandling.h"
#include "generic.h"

const int GENERIC_PLUGIN_VERSION = FEEDS_INTERFACE_VERSION_CURRENT;
const int GENERIC_PLUGIN_OPTIONS = GENERIC_PLUGIN_OPTIONS_RULES;

#define PCFG_LINE_MAX       4096
#define PCFG_TOKEN_MAX        16
#define PCFG_SEGMENTS_MAX      32

typedef struct pcfg_terminal
{
  u64 score;

  u8 *value;
  u32 value_len;

} pcfg_terminal_t;

typedef struct pcfg_table
{
  char name[PCFG_TOKEN_MAX];

  pcfg_terminal_t *terminals;
  u32 terminals_cnt;
  u32 terminals_avail;

} pcfg_table_t;

typedef struct pcfg_structure
{
  u64 score;

  pcfg_table_t **tables;
  u32 tables_cnt;

  u32 candidate_len;

  bool enabled;

} pcfg_structure_t;

typedef struct pcfg_global
{
  char *model_path;

  pcfg_table_t *tables;
  u32 tables_cnt;
  u32 tables_avail;

  pcfg_structure_t *structures;
  u32 structures_cnt;
  u32 structures_avail;

  u64 keyspace;

} pcfg_global_t;

typedef struct pcfg_node
{
  u64 score;
  u32 structure_idx;
  u32 *terminal_idx;

} pcfg_node_t;

typedef struct pcfg_thread
{
  pcfg_node_t *heap;
  u32 heap_cnt;
  u32 heap_avail;

  u64 pos;

} pcfg_thread_t;

static void error_set (generic_global_ctx_t *global_ctx, const char *fmt, ...)
{
  global_ctx->error = true;

  va_list ap;
  va_start (ap, fmt);

  vsnprintf (global_ctx->error_msg, sizeof (global_ctx->error_msg), fmt, ap);

  va_end (ap);
}

static void thread_error_set (generic_thread_ctx_t *thread_ctx, const char *fmt, ...)
{
  thread_ctx->error = true;

  va_list ap;
  va_start (ap, fmt);

  vsnprintf (thread_ctx->error_msg, sizeof (thread_ctx->error_msg), fmt, ap);

  va_end (ap);
}

static int class_slot (const char class_name)
{
  switch (class_name)
  {
    case 'U': return 0;
    case 'L': return 1;
    case 'D': return 2;
    case 'S': return 3;
  }

  return -1;
}

static bool byte_matches_class (const int slot, const u8 value)
{
  const bool upper = (value >= 'A') && (value <= 'Z');
  const bool lower = (value >= 'a') && (value <= 'z');
  const bool digit = (value >= '0') && (value <= '9');

  if (slot == 0) return upper;
  if (slot == 1) return lower;
  if (slot == 2) return digit;
  if (slot == 3) return (upper == false) && (lower == false) && (digit == false);

  return false;
}

static bool parse_u32 (const char *text, u32 *value)
{
  if ((text == NULL) || (text[0] == 0)) return false;

  char *end = NULL;

  errno = 0;

  const unsigned long parsed = strtoul (text, &end, 10);

  if ((errno != 0) || (end[0] != 0) || (parsed > 0xffffffffUL)) return false;

  value[0] = (u32) parsed;

  return true;
}

static bool parse_u64 (const char *text, u64 *value)
{
  if ((text == NULL) || (text[0] == 0) || (text[0] == '-')) return false;

  char *end = NULL;

  errno = 0;

  const unsigned long long parsed = strtoull (text, &end, 10);

  if ((errno != 0) || (end[0] != 0)) return false;

  value[0] = (u64) parsed;

  return true;
}

static bool parse_token (const char *name, int *slot, u32 *length)
{
  if ((name == NULL) || (name[0] == 0) || (name[1] == 0)) return false;

  slot[0] = class_slot (name[0]);

  if (slot[0] == -1) return false;

  if (parse_u32 (name + 1, length) == false) return false;

  if ((length[0] == 0) || (length[0] > PW_MAX)) return false;

  return true;
}

static int hex_value (const u8 c)
{
  if ((c >= '0') && (c <= '9')) return c - '0';
  if ((c >= 'a') && (c <= 'f')) return c - 'a' + 10;
  if ((c >= 'A') && (c <= 'F')) return c - 'A' + 10;

  return -1;
}

static bool hex_decode (const char *hex, u8 **value, u32 *value_len)
{
  const size_t hex_len = strlen (hex);

  if ((hex_len & 1) != 0) return false;
  if ((hex_len / 2) > PW_MAX) return false;

  value_len[0] = (u32) (hex_len / 2);
  value[0] = (u8 *) hcmalloc (MAX ((size_t) 1, hex_len / 2));

  for (size_t i = 0; i < hex_len; i += 2)
  {
    const int hi = hex_value ((u8) hex[i + 0]);
    const int lo = hex_value ((u8) hex[i + 1]);

    if ((hi == -1) || (lo == -1))
    {
      hcfree (value[0]);

      value[0] = NULL;

      return false;
    }

    value[0][i / 2] = (u8) ((hi << 4) | lo);
  }

  return true;
}

static pcfg_table_t *table_find (pcfg_global_t *pcfg_global, const char *name)
{
  for (u32 i = 0; i < pcfg_global->tables_cnt; i++)
  {
    if (strcmp (pcfg_global->tables[i].name, name) == 0) return &pcfg_global->tables[i];
  }

  return NULL;
}

static pcfg_table_t *table_get (generic_global_ctx_t *global_ctx, pcfg_global_t *pcfg_global, const char *name)
{
  pcfg_table_t *table = table_find (pcfg_global, name);

  if (table != NULL) return table;

  if (strlen (name) >= PCFG_TOKEN_MAX)
  {
    error_set (global_ctx, "PCFG token is too long: %s", name);

    return NULL;
  }

  int slot = 0;
  u32 length = 0;

  if (parse_token (name, &slot, &length) == false)
  {
    error_set (global_ctx, "Invalid PCFG token: %s", name);

    return NULL;
  }

  if (pcfg_global->tables_cnt == pcfg_global->tables_avail)
  {
    const u32 old_avail = pcfg_global->tables_avail;
    const u32 new_avail = MAX (16, old_avail * 2);

    pcfg_global->tables = (pcfg_table_t *) hcrealloc
    (
      pcfg_global->tables,
      (size_t) old_avail * sizeof (pcfg_table_t),
      (size_t) new_avail * sizeof (pcfg_table_t)
    );

    pcfg_global->tables_avail = new_avail;
  }

  table = &pcfg_global->tables[pcfg_global->tables_cnt++];

  memset (table, 0, sizeof (pcfg_table_t));

  snprintf (table->name, sizeof (table->name), "%s", name);

  return table;
}

static bool terminal_add (generic_global_ctx_t *global_ctx, pcfg_global_t *pcfg_global, char *token, char *score_text, char *hex)
{
  pcfg_table_t *table = table_get (global_ctx, pcfg_global, token);

  if (table == NULL) return false;

  u64 score = 0;

  if (parse_u64 (score_text, &score) == false)
  {
    error_set (global_ctx, "Invalid terminal score for %s: %s", token, score_text);

    return false;
  }

  u8 *value = NULL;
  u32 value_len = 0;

  if (hex_decode (hex, &value, &value_len) == false)
  {
    error_set (global_ctx, "Invalid terminal hex for %s: %s", token, hex);

    return false;
  }

  int slot = 0;
  u32 expected_len = 0;

  parse_token (token, &slot, &expected_len);

  if (value_len != expected_len)
  {
    error_set (global_ctx, "Terminal %s has %u bytes, expected %u", token, value_len, expected_len);

    hcfree (value);

    return false;
  }

  for (u32 i = 0; i < value_len; i++)
  {
    if (byte_matches_class (slot, value[i]) == false)
    {
      error_set (global_ctx, "Terminal %s contains a byte outside its declared class", token);

      hcfree (value);

      return false;
    }
  }

  if (table->terminals_cnt == table->terminals_avail)
  {
    const u32 old_avail = table->terminals_avail;
    const u32 new_avail = MAX (16, old_avail * 2);

    table->terminals = (pcfg_terminal_t *) hcrealloc
    (
      table->terminals,
      (size_t) old_avail * sizeof (pcfg_terminal_t),
      (size_t) new_avail * sizeof (pcfg_terminal_t)
    );

    table->terminals_avail = new_avail;
  }

  pcfg_terminal_t *terminal = &table->terminals[table->terminals_cnt++];

  terminal->score     = score;
  terminal->value     = value;
  terminal->value_len = value_len;

  return true;
}

static bool structure_add (generic_global_ctx_t *global_ctx, pcfg_global_t *pcfg_global, char *score_text, char *tokens)
{
  u64 score = 0;

  if (parse_u64 (score_text, &score) == false)
  {
    error_set (global_ctx, "Invalid structure score: %s", score_text);

    return false;
  }

  pcfg_table_t *tables[PCFG_SEGMENTS_MAX];

  u32 tables_cnt = 0;
  u32 candidate_len = 0;

  char *saveptr = NULL;
  char *token = strtok_r (tokens, ",", &saveptr);

  while (token != NULL)
  {
    if (tables_cnt == PCFG_SEGMENTS_MAX)
    {
      error_set (global_ctx, "Structure exceeds %u segments", PCFG_SEGMENTS_MAX);

      return false;
    }

    pcfg_table_t *table = table_find (pcfg_global, token);

    if ((table == NULL) || (table->terminals_cnt == 0))
    {
      error_set (global_ctx, "Structure references missing terminal table: %s", token);

      return false;
    }

    int slot = 0;
    u32 length = 0;

    if (parse_token (token, &slot, &length) == false)
    {
      error_set (global_ctx, "Invalid structure token: %s", token);

      return false;
    }

    if (candidate_len > (PW_MAX - length))
    {
      error_set (global_ctx, "Structure exceeds Hashcat's %u-byte candidate limit", PW_MAX);

      return false;
    }

    tables[tables_cnt++] = table;
    candidate_len += length;

    token = strtok_r (NULL, ",", &saveptr);
  }

  if (tables_cnt == 0)
  {
    error_set (global_ctx, "PCFG structure has no tokens");

    return false;
  }

  if (pcfg_global->structures_cnt == pcfg_global->structures_avail)
  {
    const u32 old_avail = pcfg_global->structures_avail;
    const u32 new_avail = MAX (16, old_avail * 2);

    pcfg_global->structures = (pcfg_structure_t *) hcrealloc
    (
      pcfg_global->structures,
      (size_t) old_avail * sizeof (pcfg_structure_t),
      (size_t) new_avail * sizeof (pcfg_structure_t)
    );

    pcfg_global->structures_avail = new_avail;
  }

  pcfg_structure_t *structure = &pcfg_global->structures[pcfg_global->structures_cnt++];

  memset (structure, 0, sizeof (pcfg_structure_t));

  structure->score         = score;
  structure->tables        = (pcfg_table_t **) hcmalloc ((size_t) tables_cnt * sizeof (pcfg_table_t *));
  structure->tables_cnt    = tables_cnt;
  structure->candidate_len = candidate_len;

  memcpy (structure->tables, tables, (size_t) tables_cnt * sizeof (pcfg_table_t *));

  return true;
}

static int terminal_compare (const void *v1, const void *v2)
{
  const pcfg_terminal_t *t1 = (const pcfg_terminal_t *) v1;
  const pcfg_terminal_t *t2 = (const pcfg_terminal_t *) v2;

  if (t1->score < t2->score) return -1;
  if (t1->score > t2->score) return  1;

  const u32 compare_len = MIN (t1->value_len, t2->value_len);

  const int rc = memcmp (t1->value, t2->value, compare_len);

  if (rc != 0) return rc;

  if (t1->value_len < t2->value_len) return -1;
  if (t1->value_len > t2->value_len) return  1;

  return 0;
}

static bool model_load (generic_global_ctx_t *global_ctx, pcfg_global_t *pcfg_global)
{
  HCFILE fp;

  if (hc_fopen (&fp, pcfg_global->model_path, "rb") == false)
  {
    error_set (global_ctx, "%s: %s", pcfg_global->model_path, strerror (errno));

    return false;
  }

  char line[PCFG_LINE_MAX];

  u64 line_num = 0;
  bool header_seen = false;

  while (!hc_feof (&fp))
  {
    const int line_len = fgetl (&fp, line, sizeof (line));

    line_num++;

    if (line_len == 0) continue;
    if (line[0] == '#') continue;

    if (header_seen == false)
    {
      if (strcmp (line, "SHOOTER-PCFG\t1") != 0)
      {
        error_set (global_ctx, "%s:%zu: expected SHOOTER-PCFG version 1 header", pcfg_global->model_path, (size_t) line_num);

        break;
      }

      header_seen = true;

      continue;
    }

    char *field1 = strchr (line, '\t');

    if (field1 == NULL)
    {
      error_set (global_ctx, "%s:%zu: malformed record", pcfg_global->model_path, (size_t) line_num);

      break;
    }

    field1[0] = 0;
    field1++;

    char *field2 = strchr (field1, '\t');

    if (field2 == NULL)
    {
      error_set (global_ctx, "%s:%zu: malformed record", pcfg_global->model_path, (size_t) line_num);

      break;
    }

    field2[0] = 0;
    field2++;

    char *field3 = strchr (field2, '\t');

    if (field3 == NULL)
    {
      error_set (global_ctx, "%s:%zu: malformed record", pcfg_global->model_path, (size_t) line_num);

      break;
    }

    field3[0] = 0;
    field3++;

    bool rc = false;

    if (strcmp (line, "T") == 0)
    {
      rc = terminal_add (global_ctx, pcfg_global, field1, field2, field3);
    }
    else if (strcmp (line, "S") == 0)
    {
      // S has an empty second field so that every record has four fields.

      if (field1[0] != 0)
      {
        error_set (global_ctx, "%s:%zu: structure record reserved field must be empty", pcfg_global->model_path, (size_t) line_num);
      }
      else
      {
        rc = structure_add (global_ctx, pcfg_global, field2, field3);
      }
    }
    else
    {
      error_set (global_ctx, "%s:%zu: unknown record type: %s", pcfg_global->model_path, (size_t) line_num, line);
    }

    if (rc == false) break;
  }

  hc_fclose (&fp);

  if ((global_ctx->error == false) && (header_seen == false))
  {
    error_set (global_ctx, "%s: empty model", pcfg_global->model_path);
  }

  if ((global_ctx->error == false) && (pcfg_global->structures_cnt == 0))
  {
    error_set (global_ctx, "%s: model contains no structures", pcfg_global->model_path);
  }

  for (u32 i = 0; i < pcfg_global->tables_cnt; i++)
  {
    pcfg_table_t *table = &pcfg_global->tables[i];

    qsort (table->terminals, table->terminals_cnt, sizeof (pcfg_terminal_t), terminal_compare);
  }

  return global_ctx->error == false;
}

static void model_free (pcfg_global_t *pcfg_global)
{
  if (pcfg_global == NULL) return;

  for (u32 i = 0; i < pcfg_global->tables_cnt; i++)
  {
    pcfg_table_t *table = &pcfg_global->tables[i];

    for (u32 j = 0; j < table->terminals_cnt; j++)
    {
      hcfree (table->terminals[j].value);
    }

    hcfree (table->terminals);
  }

  for (u32 i = 0; i < pcfg_global->structures_cnt; i++)
  {
    hcfree (pcfg_global->structures[i].tables);
  }

  hcfree (pcfg_global->structures);
  hcfree (pcfg_global->tables);
  hcfree (pcfg_global->model_path);
  hcfree (pcfg_global);
}

static bool keyspace_prepare (generic_global_ctx_t *global_ctx, pcfg_global_t *pcfg_global)
{
  u64 keyspace = 0;

  for (u32 i = 0; i < pcfg_global->structures_cnt; i++)
  {
    pcfg_structure_t *structure = &pcfg_global->structures[i];

    structure->enabled = true;

    u64 combinations = 1;
    u64 maximum_score = structure->score;

    for (u32 j = 0; j < structure->tables_cnt; j++)
    {
      const u64 count = structure->tables[j]->terminals_cnt;
      const u64 terminal_score = structure->tables[j]->terminals[count - 1].score;

      if (combinations > (0xffffffffffffffffULL / count))
      {
        error_set (global_ctx, "PCFG keyspace exceeds 64 bits; retrain with tighter structure/terminal limits");

        return false;
      }

      combinations *= count;

      if (maximum_score > (0xffffffffffffffffULL - terminal_score))
      {
        error_set (global_ctx, "PCFG candidate score exceeds 64 bits");

        return false;
      }

      maximum_score += terminal_score;
    }

    if (keyspace > (0xffffffffffffffffULL - combinations))
    {
      error_set (global_ctx, "PCFG keyspace exceeds 64 bits; retrain with tighter structure/terminal limits");

      return false;
    }

    keyspace += combinations;
  }

  pcfg_global->keyspace = keyspace;

  return true;
}

static int node_compare (const pcfg_global_t *pcfg_global, const pcfg_node_t *n1, const pcfg_node_t *n2)
{
  if (n1->score < n2->score) return -1;
  if (n1->score > n2->score) return  1;

  if (n1->structure_idx < n2->structure_idx) return -1;
  if (n1->structure_idx > n2->structure_idx) return  1;

  const pcfg_structure_t *structure = &pcfg_global->structures[n1->structure_idx];

  for (u32 i = 0; i < structure->tables_cnt; i++)
  {
    if (n1->terminal_idx[i] < n2->terminal_idx[i]) return -1;
    if (n1->terminal_idx[i] > n2->terminal_idx[i]) return  1;
  }

  return 0;
}

static bool heap_push (generic_thread_ctx_t *thread_ctx, const pcfg_global_t *pcfg_global, pcfg_thread_t *pcfg_thread, const pcfg_node_t node)
{
  if (pcfg_thread->heap_cnt == pcfg_thread->heap_avail)
  {
    const u32 old_avail = pcfg_thread->heap_avail;
    const u32 new_avail = MAX (16, old_avail * 2);

    pcfg_thread->heap = (pcfg_node_t *) hcrealloc
    (
      pcfg_thread->heap,
      (size_t) old_avail * sizeof (pcfg_node_t),
      (size_t) new_avail * sizeof (pcfg_node_t)
    );

    if (pcfg_thread->heap == NULL)
    {
      thread_error_set (thread_ctx, "Unable to grow PCFG priority queue");

      return false;
    }

    pcfg_thread->heap_avail = new_avail;
  }

  u32 idx = pcfg_thread->heap_cnt++;

  pcfg_thread->heap[idx] = node;

  while (idx > 0)
  {
    const u32 parent = (idx - 1) / 2;

    if (node_compare (pcfg_global, &pcfg_thread->heap[parent], &pcfg_thread->heap[idx]) <= 0) break;

    const pcfg_node_t tmp = pcfg_thread->heap[parent];

    pcfg_thread->heap[parent] = pcfg_thread->heap[idx];
    pcfg_thread->heap[idx] = tmp;

    idx = parent;
  }

  return true;
}

static pcfg_node_t heap_pop (const pcfg_global_t *pcfg_global, pcfg_thread_t *pcfg_thread)
{
  const pcfg_node_t result = pcfg_thread->heap[0];

  pcfg_thread->heap_cnt--;

  if (pcfg_thread->heap_cnt == 0) return result;

  pcfg_thread->heap[0] = pcfg_thread->heap[pcfg_thread->heap_cnt];

  u32 idx = 0;

  while (true)
  {
    const u32 left  = idx * 2 + 1;
    const u32 right = left + 1;

    if (left >= pcfg_thread->heap_cnt) break;

    u32 smallest = left;

    if ((right < pcfg_thread->heap_cnt)
     && (node_compare (pcfg_global, &pcfg_thread->heap[right], &pcfg_thread->heap[left]) < 0))
    {
      smallest = right;
    }

    if (node_compare (pcfg_global, &pcfg_thread->heap[idx], &pcfg_thread->heap[smallest]) <= 0) break;

    const pcfg_node_t tmp = pcfg_thread->heap[idx];

    pcfg_thread->heap[idx] = pcfg_thread->heap[smallest];
    pcfg_thread->heap[smallest] = tmp;

    idx = smallest;
  }

  return result;
}

static void thread_reset_free (pcfg_thread_t *pcfg_thread)
{
  for (u32 i = 0; i < pcfg_thread->heap_cnt; i++)
  {
    hcfree (pcfg_thread->heap[i].terminal_idx);
  }

  hcfree (pcfg_thread->heap);

  pcfg_thread->heap       = NULL;
  pcfg_thread->heap_cnt   = 0;
  pcfg_thread->heap_avail = 0;
  pcfg_thread->pos        = 0;
}

static bool thread_reset (generic_thread_ctx_t *thread_ctx, const pcfg_global_t *pcfg_global, pcfg_thread_t *pcfg_thread)
{
  thread_reset_free (pcfg_thread);

  for (u32 structure_idx = 0; structure_idx < pcfg_global->structures_cnt; structure_idx++)
  {
    const pcfg_structure_t *structure = &pcfg_global->structures[structure_idx];

    if (structure->enabled == false) continue;

    pcfg_node_t node;

    node.score         = structure->score;
    node.structure_idx = structure_idx;
    node.terminal_idx  = (u32 *) hccalloc (structure->tables_cnt, sizeof (u32));

    for (u32 i = 0; i < structure->tables_cnt; i++)
    {
      if (node.score > (0xffffffffffffffffULL - structure->tables[i]->terminals[0].score))
      {
        hcfree (node.terminal_idx);

        thread_error_set (thread_ctx, "PCFG candidate score exceeds 64 bits");

        return false;
      }

      node.score += structure->tables[i]->terminals[0].score;
    }

    if (heap_push (thread_ctx, pcfg_global, pcfg_thread, node) == false)
    {
      hcfree (node.terminal_idx);

      return false;
    }
  }

  return true;
}

bool global_init (generic_global_ctx_t *global_ctx, MAYBE_UNUSED generic_thread_ctx_t **thread_ctx, MAYBE_UNUSED hashcat_ctx_t *hashcat_ctx)
{
  pcfg_global_t *pcfg_global = (pcfg_global_t *) hccalloc (1, sizeof (pcfg_global_t));

  global_ctx->gbldata = pcfg_global;

  if (global_ctx->workc != 2)
  {
    error_set (global_ctx, "Usage: pcfg <model>");

    return false;
  }

  pcfg_global->model_path = hcstrdup (global_ctx->workv[1]);

  if (model_load (global_ctx, pcfg_global) == false) return false;
  if (keyspace_prepare (global_ctx, pcfg_global) == false) return false;

  snprintf (global_ctx->guess_base, sizeof (global_ctx->guess_base), "PCFG model %s", pcfg_global->model_path);

  const char format_ident[] = "SHOOTER-PCFG-1";

  global_ctx->source_ident = XXH64 (format_ident, sizeof (format_ident) - 1, 0);

  for (u32 i = 0; i < pcfg_global->tables_cnt; i++)
  {
    const pcfg_table_t *table = &pcfg_global->tables[i];

    const u32 name_len = (u32) strlen (table->name);

    global_ctx->source_ident = XXH64 (&name_len, sizeof (name_len), global_ctx->source_ident);
    global_ctx->source_ident = XXH64 (table->name, strlen (table->name), global_ctx->source_ident);
    global_ctx->source_ident = XXH64 (&table->terminals_cnt, sizeof (table->terminals_cnt), global_ctx->source_ident);

    for (u32 j = 0; j < table->terminals_cnt; j++)
    {
      const pcfg_terminal_t *terminal = &table->terminals[j];

      global_ctx->source_ident = XXH64 (&terminal->score, sizeof (terminal->score), global_ctx->source_ident);
      global_ctx->source_ident = XXH64 (&terminal->value_len, sizeof (terminal->value_len), global_ctx->source_ident);
      global_ctx->source_ident = XXH64 (terminal->value, terminal->value_len, global_ctx->source_ident);
    }
  }

  global_ctx->source_ident = XXH64 (&pcfg_global->structures_cnt, sizeof (pcfg_global->structures_cnt), global_ctx->source_ident);

  for (u32 i = 0; i < pcfg_global->structures_cnt; i++)
  {
    const pcfg_structure_t *structure = &pcfg_global->structures[i];

    global_ctx->source_ident = XXH64 (&structure->score, sizeof (structure->score), global_ctx->source_ident);
    global_ctx->source_ident = XXH64 (&structure->tables_cnt, sizeof (structure->tables_cnt), global_ctx->source_ident);

    for (u32 j = 0; j < structure->tables_cnt; j++)
    {
      const u32 name_len = (u32) strlen (structure->tables[j]->name);

      global_ctx->source_ident = XXH64 (&name_len, sizeof (name_len), global_ctx->source_ident);
      global_ctx->source_ident = XXH64 (structure->tables[j]->name, name_len, global_ctx->source_ident);
    }
  }

  return true;
}

void global_term (generic_global_ctx_t *global_ctx, MAYBE_UNUSED generic_thread_ctx_t **thread_ctx, MAYBE_UNUSED hashcat_ctx_t *hashcat_ctx)
{
  model_free ((pcfg_global_t *) global_ctx->gbldata);

  global_ctx->gbldata = NULL;
}

u64 global_keyspace (generic_global_ctx_t *global_ctx, MAYBE_UNUSED generic_thread_ctx_t **thread_ctx, MAYBE_UNUSED hashcat_ctx_t *hashcat_ctx)
{
  const pcfg_global_t *pcfg_global = (const pcfg_global_t *) global_ctx->gbldata;

  return pcfg_global->keyspace;
}

bool thread_init (generic_global_ctx_t *global_ctx, generic_thread_ctx_t *thread_ctx)
{
  const pcfg_global_t *pcfg_global = (const pcfg_global_t *) global_ctx->gbldata;

  pcfg_thread_t *pcfg_thread = (pcfg_thread_t *) hccalloc (1, sizeof (pcfg_thread_t));

  thread_ctx->thrdata = pcfg_thread;

  if (thread_reset (thread_ctx, pcfg_global, pcfg_thread) == false) return false;

  return true;
}

void thread_term (MAYBE_UNUSED generic_global_ctx_t *global_ctx, generic_thread_ctx_t *thread_ctx)
{
  pcfg_thread_t *pcfg_thread = (pcfg_thread_t *) thread_ctx->thrdata;

  if (pcfg_thread == NULL) return;

  thread_reset_free (pcfg_thread);

  hcfree (pcfg_thread);

  thread_ctx->thrdata = NULL;
}

int thread_next (generic_global_ctx_t *global_ctx, generic_thread_ctx_t *thread_ctx, u8 *out_buf, const int out_size)
{
  const pcfg_global_t *pcfg_global = (const pcfg_global_t *) global_ctx->gbldata;
  pcfg_thread_t *pcfg_thread = (pcfg_thread_t *) thread_ctx->thrdata;

  if (pcfg_thread->pos >= pcfg_global->keyspace) return GENERIC_RC_EOF;
  if (pcfg_thread->heap_cnt == 0) return GENERIC_RC_EOF;

  pcfg_node_t node = heap_pop (pcfg_global, pcfg_thread);

  const pcfg_structure_t *structure = &pcfg_global->structures[node.structure_idx];

  u32 out_len = 0;

  for (u32 i = 0; i < structure->tables_cnt; i++)
  {
    const pcfg_terminal_t *terminal = &structure->tables[i]->terminals[node.terminal_idx[i]];

    const u32 room = (out_len < (u32) out_size) ? ((u32) out_size - out_len) : 0;
    const u32 copy_len = MIN (terminal->value_len, room);

    memcpy (out_buf + out_len, terminal->value, copy_len);

    out_len += terminal->value_len;
  }

  // A k-dimensional sorted sum can be enumerated without duplicates by incrementing dimension j
  // only while every earlier dimension is zero. Every nonzero tuple then has exactly one parent.

  bool earlier_zero = true;

  for (u32 i = 0; (i < structure->tables_cnt) && (earlier_zero == true); i++)
  {
    const pcfg_table_t *table = structure->tables[i];
    const u32 old_idx = node.terminal_idx[i];

    if ((old_idx + 1) < table->terminals_cnt)
    {
      pcfg_node_t child;

      child.structure_idx = node.structure_idx;
      child.terminal_idx = (u32 *) hcmalloc ((size_t) structure->tables_cnt * sizeof (u32));

      memcpy (child.terminal_idx, node.terminal_idx, (size_t) structure->tables_cnt * sizeof (u32));

      child.terminal_idx[i]++;
      child.score = node.score - table->terminals[old_idx].score + table->terminals[old_idx + 1].score;

      if (heap_push (thread_ctx, pcfg_global, pcfg_thread, child) == false)
      {
        hcfree (child.terminal_idx);
        hcfree (node.terminal_idx);

        return GENERIC_RC_ERROR;
      }
    }

    if (node.terminal_idx[i] != 0) earlier_zero = false;
  }

  hcfree (node.terminal_idx);

  pcfg_thread->pos++;

  return (int) out_len;
}

bool thread_seek (generic_global_ctx_t *global_ctx, generic_thread_ctx_t *thread_ctx, const u64 offset)
{
  const pcfg_global_t *pcfg_global = (const pcfg_global_t *) global_ctx->gbldata;
  pcfg_thread_t *pcfg_thread = (pcfg_thread_t *) thread_ctx->thrdata;

  if (offset > pcfg_global->keyspace)
  {
    thread_error_set (thread_ctx, "seek target past EOF: %zu", (size_t) offset);

    return false;
  }

  if (offset < pcfg_thread->pos)
  {
    if (thread_reset (thread_ctx, pcfg_global, pcfg_thread) == false) return false;
  }

  u8 scratch[PW_MAX];

  while (pcfg_thread->pos < offset)
  {
    const int rc = thread_next (global_ctx, thread_ctx, scratch, PW_MAX);

    if (rc < 0) return false;
  }

  return true;
}

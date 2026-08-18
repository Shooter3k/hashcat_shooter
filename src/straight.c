/**
 * Author......: See docs/credits.txt
 * License.....: MIT
 */

#include "common.h"
#include "types.h"
#include "memory.h"
#include "event.h"
#include "logfile.h"
#include "shared.h"
#include "path.h"
#include "filehandling.h"
#include "hlfmt.h"
#include "folder.h"
#include "rp.h"
#include "wordlist.h"
#include "restore.h"
#include "dispatch.h"
#include "generic.h"
#include "straight.h"

#define ATTACK13_GPU_RULES_MAX 65536U

typedef struct attack13_gpu_wordlist
{
  u8  *buf;
  int *len;

} attack13_gpu_wordlist_t;

static void attack13_gpu_wordlists_destroy (attack13_gpu_wordlist_t *wordlists, const u32 stages_cnt)
{
  if (wordlists == NULL) return;

  for (u32 i = 0; i < stages_cnt; i++)
  {
    hcfree (wordlists[i].buf);
    hcfree (wordlists[i].len);
  }

  hcfree (wordlists);
}

static int attack13_gpu_wordlist_load (hashcat_ctx_t *hashcat_ctx, const attack13_stage_t *stage, attack13_gpu_wordlist_t *wordlist)
{
  const user_options_t       *user_options       = hashcat_ctx->user_options;
  const user_options_extra_t *user_options_extra = hashcat_ctx->user_options_extra;

  HCFILE fp;

  if (hc_fopen (&fp, stage->source, "rb") == false) return -1;

  const int   rule_len = (stage->wordlist_ordinal == 0) ? user_options_extra->rule_len_l : user_options_extra->rule_len_r;
  const char *rule_buf = (stage->wordlist_ordinal == 0) ? user_options->rule_buf_l       : user_options->rule_buf_r;

  pw_transform_t transform;

  if (pw_transform_init_wordlist (&transform, hashcat_ctx, rule_len, rule_buf) == -1)
  {
    hc_fclose (&fp);

    return -1;
  }

  wordlist->buf = (u8 *) hccalloc ((size_t) stage->candidates, RP_PASSWORD_SIZE);
  wordlist->len = (int *) hccalloc ((size_t) stage->candidates, sizeof (int));

  char *line_buf = (char *) hcmalloc (HCBUFSIZ_LARGE);

  int rc = 0;

  for (u64 i = 0; i < stage->candidates; i++)
  {
    int line_len = fgetl (&fp, line_buf, HCBUFSIZ_LARGE);

    if (line_len == -1)
    {
      rc = -1;

      break;
    }

    line_len = pw_transform_apply (&transform, (u8 *) line_buf, line_len, HCBUFSIZ_LARGE);

    // A rejected or very long line cannot be encoded as a literal kernel-rule suffix without
    // changing the Cartesian position. Keep this pipeline on the exact host path instead.

    if ((line_len < 0) || (line_len > MAX_KERNEL_RULES))
    {
      rc = 1;

      break;
    }

    wordlist->len[i] = line_len;

    memcpy (wordlist->buf + (i * RP_PASSWORD_SIZE), line_buf, line_len);
  }

  hcfree (line_buf);

  pw_transform_term (&transform);

  hc_fclose (&fp);

  return rc;
}

static u32 attack13_gpu_rule_compact (kernel_rule_t *rule)
{
  u32 out = 0;

  for (u32 i = 0; i < MAX_KERNEL_RULES; i++)
  {
    const u32 cmd = rule->cmds[i];

    if (cmd == 0) break;
    if ((cmd & 0xff) == RULE_OP_MANGLE_NOOP) continue;

    rule->cmds[out++] = cmd;
  }

  for (u32 i = out; i < RULES_MAX; i++) rule->cmds[i] = 0;

  return out;
}

static int attack13_gpu_rules_build (hashcat_ctx_t *hashcat_ctx, const u32 gpu_start, const u64 amplifier, kernel_rule_t **rules_out)
{
  const mask_ctx_t *mask_ctx = hashcat_ctx->mask_ctx;

  const u32 stages_cnt = mask_ctx->attack13_stages_cnt;

  attack13_gpu_wordlist_t *wordlists = (attack13_gpu_wordlist_t *) hccalloc (stages_cnt, sizeof (attack13_gpu_wordlist_t));

  int rc = 0;

  for (u32 i = gpu_start; i < stages_cnt; i++)
  {
    const attack13_stage_t *stage = &mask_ctx->attack13_stages[i];

    if (stage->type != ATTACK13_STAGE_WORDLIST) continue;

    rc = attack13_gpu_wordlist_load (hashcat_ctx, stage, &wordlists[i]);

    if (rc != 0) break;
  }

  kernel_rule_t *rules = NULL;

  if (rc == 0) rules = (kernel_rule_t *) hccalloc ((size_t) amplifier, sizeof (kernel_rule_t));

  for (u64 amplifier_pos = 0; (amplifier_pos < amplifier) && (rc == 0); amplifier_pos++)
  {
    kernel_rule_t *rule = &rules[amplifier_pos];

    u32 commands_cnt = 0;

    for (u32 i = gpu_start; i < stages_cnt; i++)
    {
      const attack13_stage_t *stage = &mask_ctx->attack13_stages[i];
      const u64 stage_idx = (amplifier_pos / stage->stride) % stage->candidates;

      if (stage->type == ATTACK13_STAGE_RULES)
      {
        kernel_rule_t stage_rule;

        memset (&stage_rule, 0, sizeof (stage_rule));

        if (stage->generated == true)
        {
          memcpy (&stage_rule, &stage->generated_rules[stage_idx], sizeof (kernel_rule_t));
        }
        else if (cpu_rule_to_kernel_rule (stage->rules[stage_idx], stage->rule_lens[stage_idx], &stage_rule) == -1)
        {
          rc = 1;

          break;
        }

        const u32 stage_commands_cnt = attack13_gpu_rule_compact (&stage_rule);

        if (commands_cnt + stage_commands_cnt > MAX_KERNEL_RULES)
        {
          rc = 1;

          break;
        }

        memcpy (rule->cmds + commands_cnt, stage_rule.cmds, stage_commands_cnt * sizeof (u32));

        commands_cnt += stage_commands_cnt;

        continue;
      }

      u8  literal[RP_PASSWORD_SIZE];
      int literal_len = 0;

      if (stage->type == ATTACK13_STAGE_WORDLIST)
      {
        literal_len = wordlists[i].len[stage_idx];

        memcpy (literal, wordlists[i].buf + (stage_idx * RP_PASSWORD_SIZE), literal_len);
      }
      else if (attack13_mask_append (mask_ctx, stage, stage_idx, (char *) literal, &literal_len) == -1)
      {
        rc = 1;

        break;
      }

      if (commands_cnt + (u32) literal_len > MAX_KERNEL_RULES)
      {
        rc = 1;

        break;
      }

      for (int j = 0; j < literal_len; j++)
      {
        rule->cmds[commands_cnt++] = RULE_OP_MANGLE_APPEND | (((u32) literal[j] & 0xff) << 8);
      }
    }
  }

  attack13_gpu_wordlists_destroy (wordlists, stages_cnt);

  if (rc != 0)
  {
    hcfree (rules);

    return rc;
  }

  rules_out[0] = rules;

  return 0;
}

int straight_ctx_attack13_amplifier_init (hashcat_ctx_t *hashcat_ctx)
{
  mask_ctx_t           *mask_ctx     = hashcat_ctx->mask_ctx;
  const restore_ctx_t  *restore_ctx  = hashcat_ctx->restore_ctx;
  straight_ctx_t       *straight_ctx = hashcat_ctx->straight_ctx;
  const user_options_t *user_options = hashcat_ctx->user_options;

  const u32 stages_cnt = mask_ctx->attack13_stages_cnt;

  // stdout promises exact command-line order. Old restore files store full-pipeline positions, so
  // they also stay on the old mapping. Divisible skip/limit boundaries retain their exact meaning.

  // Mode 13 historically reports its complete ordered product through --keyspace. There is no
  // cracking work to accelerate in this mode, so keep that public value independent of whether a
  // later real attack can split the product into host prefixes and a GPU suffix.

  if (user_options->stdout_flag == true) return 0;

  if (user_options->keyspace == true) return 0;

  if ((restore_ctx->restore_execute == true)
   && ((restore_ctx->rd->stdout_flags & RESTORE_DATA_ATTACK13_AMPLIFIED) == 0)) return 0;

  u32 *attempts = (u32 *) hccalloc (stages_cnt, sizeof (u32));
  u32 attempts_cnt = 0;

  u64 suffix_amplifier = 1;

  for (u32 pos = stages_cnt; pos > 0; pos--)
  {
    const u32 i = pos - 1;
    const attack13_stage_t *stage = &mask_ctx->attack13_stages[i];

    if (stage->candidates > ATTACK13_GPU_RULES_MAX / suffix_amplifier) break;

    suffix_amplifier *= stage->candidates;

    if (suffix_amplifier > 1) attempts[attempts_cnt++] = i;
  }

  // The starts were discovered from the shortest suffix to the longest. Try the largest multiplier
  // first, then progressively drop leftmost stages if a rule cannot be compiled or exceeds 31
  // kernel commands once its literal append stages are included.

  for (u32 attempt_pos = attempts_cnt; attempt_pos > 0; attempt_pos--)
  {
    const u32 gpu_start = attempts[attempt_pos - 1];
    const u64 amplifier = mask_ctx->attack13_stages[gpu_start].candidates * mask_ctx->attack13_stages[gpu_start].stride;

    if (((user_options->skip  > 0) && ((user_options->skip  % amplifier) != 0))
     || ((user_options->limit > 0) && ((user_options->limit % amplifier) != 0))) continue;

    kernel_rule_t *rules = NULL;

    const int rc = attack13_gpu_rules_build (hashcat_ctx, gpu_start, amplifier, &rules);

    if (rc == -1)
    {
      hcfree (attempts);

      return -1;
    }
    if (rc == 1) continue;

    if ((restore_ctx->restore_execute == true) && (restore_ctx->rd->attack13_amplifier != amplifier))
    {
      hcfree (rules);

      event_log_error (hashcat_ctx, "Attack-mode 13 restore amplifier no longer matches this pipeline.");

      hcfree (attempts);

      return -1;
    }

    u32 host_wordlists_cnt = 0;

    for (u32 i = 0; i < gpu_start; i++)
    {
      if (mask_ctx->attack13_stages[i].type == ATTACK13_STAGE_WORDLIST) host_wordlists_cnt++;
    }

    hcfree (straight_ctx->kernel_rules_buf);

    straight_ctx->kernel_rules_buf = rules;
    straight_ctx->kernel_rules_cnt = (u32) amplifier;

    mask_ctx->attack13_host_stages_cnt    = gpu_start;
    mask_ctx->attack13_host_wordlists_cnt = host_wordlists_cnt;
    mask_ctx->attack13_amplifier           = amplifier;
    mask_ctx->attack13_gpu_amplified       = true;

    hcfree (attempts);

    return 0;
  }

  hcfree (attempts);

  if ((restore_ctx->restore_execute == true)
   && ((restore_ctx->rd->stdout_flags & RESTORE_DATA_ATTACK13_AMPLIFIED) != 0))
  {
    event_log_error (hashcat_ctx, "Attack-mode 13 could not reconstruct the saved GPU amplifier.");

    return -1;
  }

  return 0;
}

static int straight_ctx_add_wl (hashcat_ctx_t *hashcat_ctx, const char *dict)
{
  if (hc_path_has_bom (dict) == true)
  {
    event_log_warning (hashcat_ctx, "%s: Byte Order Mark (BOM) was detected", dict);

    //return -1;
  }

  straight_ctx_t *straight_ctx = hashcat_ctx->straight_ctx;

  if (straight_ctx->dicts_avail == straight_ctx->dicts_cnt)
  {
    straight_ctx->dicts = (char **) hcrealloc (straight_ctx->dicts, straight_ctx->dicts_avail * sizeof (char *), INCR_DICTS * sizeof (char *));

    straight_ctx->dicts_avail += INCR_DICTS;
  }

  straight_ctx->dicts[straight_ctx->dicts_cnt] = hcstrdup (dict);

  straight_ctx->dicts_cnt++;

  return 0;
}

// The rounds of -a 9 splitting its own hash file. There is no file per round: a round is "try the Nth
// word of every account name", so the list is as long as the widest account name in the file.
//
// The names are walked here rather than the count being asked of the feed, because the round list has to
// exist before any round is opened and the feed is opened one round at a time.

static int straight_ctx_add_association_rounds (hashcat_ctx_t *hashcat_ctx)
{
  const hashes_t *hashes = hashcat_ctx->hashes;

  straight_ctx_t *straight_ctx = hashcat_ctx->straight_ctx;

  u32 words_max = 1;

  if (hashes->hash_info)
  {
    hlfmt_word_t words[ASSOCIATION_WORDS_MAX];

    for (u32 i = 0; i < hashes->digests_cnt; i++)
    {
      const user_t *user = hashes->hash_info[i]->user;

      if (user == NULL) continue;

      const u32 words_cnt = hlfmt_user_words (user->user_name, user->user_len, words, ASSOCIATION_WORDS_MAX);

      if (words_cnt > words_max) words_max = words_cnt;
    }
  }

  straight_ctx->dicts = (char **) hcmalloc (words_max * sizeof (char *));

  straight_ctx->dicts_avail = words_max;
  straight_ctx->dicts_cnt   = words_max;

  for (u32 i = 0; i < words_max; i++)
  {
    char *name = NULL;

    hc_asprintf (&name, "%u", i);

    straight_ctx->dicts[i] = name;
  }

  return 0;
}

// Turn a range of the work arguments into the dictionary list. A directory becomes every readable file
// inside it, sorted by name so the keyspace is the same on every machine, and anything else is added as
// it stands.
//
// The range is the only thing the attack modes disagree about. -a 0 and -a 9 take every argument, -a 6
// leaves the last one to the mask, and -a 7 leaves the first one to it.

static int straight_ctx_add_workv (hashcat_ctx_t *hashcat_ctx, const int from, const int to)
{
  straight_ctx_t       *straight_ctx       = hashcat_ctx->straight_ctx;
  user_options_extra_t *user_options_extra = hashcat_ctx->user_options_extra;

  for (int i = from; i < to; i++)
  {
    char *l0_filename = user_options_extra->hc_workv[i];

    // at this point we already verified the path actually exist and is readable

    if (hc_path_is_directory (l0_filename) == false)
    {
      if (straight_ctx_add_wl (hashcat_ctx, l0_filename) == -1) return -1;

      continue;
    }

    char **dictionary_files = scan_directory (l0_filename);

    if (dictionary_files != NULL)
    {
      qsort (dictionary_files, (size_t) count_dictionaries (dictionary_files), sizeof (char *), sort_by_stringptr);

      for (int d = 0; dictionary_files[d] != NULL; d++)
      {
        char *l1_filename = dictionary_files[d];

        if (hc_path_read (l1_filename) == false)
        {
          event_log_error (hashcat_ctx, "%s: %s", l1_filename, strerror (errno));

          hcfree (dictionary_files);

          return -1;
        }

        if (hc_path_is_file (l1_filename) == true)
        {
          if (straight_ctx_add_wl (hashcat_ctx, l1_filename) == -1)
          {
            hcfree (dictionary_files);

            return -1;
          }
        }
      }
    }

    hcfree (dictionary_files);
  }

  if (straight_ctx->dicts_cnt == 0)
  {
    event_log_error (hashcat_ctx, "No usable dictionary file found.");

    return -1;
  }

  return 0;
}

// Point the base word instance at the one dictionary this round reads, and say how many base words
// that is. Only the per round scope comes here: an induction round, and -a 9 over more than one
// dictionary. Everything else opened its instance once, over every source at once.
//
// An empty dictionary is not an error, it is a round with nothing in it, and the caller skips the
// round. A feed refuses a source it can get no words out of, which is right when that source is the
// whole attack and wrong when it is one file of a directory, so the empty case is answered before the
// feed is asked.

static u64 straight_ctx_round_words (hashcat_ctx_t *hashcat_ctx, const char *dict)
{
  const user_options_extra_t *user_options_extra = hashcat_ctx->user_options_extra;

  // An empty dictionary is a round with nothing in it rather than a failure, and that is worth knowing
  // before a feed is stood up for it. -a 9 splitting its own hash file has no file here: its rounds are
  // the words an account name became, so there is nothing to stat and nothing that can be empty.

  if (user_options_extra->association_autosplit == false)
  {
    HCFILE fp;

    if (hc_fopen (&fp, dict, "rb") == false)
    {
      event_log_error (hashcat_ctx, "%s: %s", dict, strerror (errno));

      return GENERIC_KEYSPACE_ERROR;
    }

    struct stat st;

    const int rc_stat = hc_fstat (&fp, &st);

    hc_fclose (&fp);

    if (rc_stat == 0)
    {
      if (st.st_size == 0) return 0;
    }
  }

  if (generic_ctx_base_round (hashcat_ctx, dict) == -1) return GENERIC_KEYSPACE_ERROR;

  const generic_ctx_t *generic_ctx = &hashcat_ctx->generic_ctx[GENERIC_ROLE_BASE];

  if (generic_ctx->keyspace == GENERIC_KEYSPACE_UNKNOWN)
  {
    event_log_error (hashcat_ctx, "%s: feed cannot report a keyspace.", dict);

    return GENERIC_KEYSPACE_ERROR;
  }

  return generic_ctx->keyspace;
}

// Finish a keyspace: base words times whatever one base word stands for. That is the rules for a
// straight attack, the amplifier words for a combinator one and the mask for the hybrids. It cannot
// happen any earlier than here, because mask_ctx_update_loop sizes the mask once per round.

static int straight_ctx_words_apply (hashcat_ctx_t *hashcat_ctx, const u64 words_cnt, const u64 amplifier, const char *dict)
{
  status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  if (overflow_check_u64_mul (words_cnt, amplifier) == true)
  {
    event_log_error (hashcat_ctx, "Integer overflow detected in keyspace of wordlist: %s", dict);

    return -1;
  }

  status_ctx->words_cnt = words_cnt * amplifier;

  return 0;
}

int straight_ctx_update_loop (hashcat_ctx_t *hashcat_ctx)
{
  combinator_ctx_t     *combinator_ctx     = hashcat_ctx->combinator_ctx;
  induct_ctx_t         *induct_ctx         = hashcat_ctx->induct_ctx;
  logfile_ctx_t        *logfile_ctx        = hashcat_ctx->logfile_ctx;
  mask_ctx_t           *mask_ctx           = hashcat_ctx->mask_ctx;
  status_ctx_t         *status_ctx         = hashcat_ctx->status_ctx;
  straight_ctx_t       *straight_ctx       = hashcat_ctx->straight_ctx;
  user_options_t       *user_options       = hashcat_ctx->user_options;
  user_options_extra_t *user_options_extra = hashcat_ctx->user_options_extra;

  // Whichever scope the base word instance has, what is left to do here is the amplifier, and that is
  // why this happens per round rather than at init. -a 6 and -a 7 amplify with the mask, and the mask
  // is only sized a few lines earlier, by mask_ctx_update_loop.

  // A pipe comes here too and is the one whose keyspace is never known, so it leaves below with
  // words_cnt set to GENERIC_KEYSPACE_UNKNOWN and the run has no denominator.

  if (user_options->attack_mode == ATTACK_MODE_MULTI_HYBRID)
  {
    for (u32 i = 0; i < mask_ctx->attack13_stages_cnt; i++)
    {
      const attack13_stage_t *stage = &mask_ctx->attack13_stages[i];

      if (stage->type == ATTACK13_STAGE_RULES) logfile_sub_var_string ("rule-stage", stage->source);
      else                                     logfile_sub_string (stage->source);
    }

    status_ctx->words_cnt = mask_ctx->attack13_candidates;

    return 0;
  }

  if (user_options_extra->whole_candidate_rules == true)
  {
    u64 base_candidates = 0;

    if (user_options->attack_mode == ATTACK_MODE_COMBI)
    {
      base_candidates = 1;

      for (int i = 0; i < combinator_ctx->dicts_cnt; i++)
      {
        logfile_sub_string (combinator_ctx->dicts[i]);

        if (overflow_check_u64_mul (base_candidates, combinator_ctx->combs_counts[i]) == true)
        {
          event_log_error (hashcat_ctx, "Integer overflow detected in whole-candidate combination keyspace.");

          return -1;
        }

        base_candidates *= combinator_ctx->combs_counts[i];
      }
    }
    else if (user_options->attack_mode == ATTACK_MODE_BF)
    {
      logfile_sub_string (mask_ctx->mask);

      base_candidates = mask_ctx->bfs_cnt;
    }
    else if ((user_options->attack_mode == ATTACK_MODE_HYBRID1) || (user_options->attack_mode == ATTACK_MODE_HYBRID2))
    {
      const generic_ctx_t *generic_ctx = &hashcat_ctx->generic_ctx[GENERIC_ROLE_BASE];

      logfile_sub_string (mask_ctx->mask);

      if (overflow_check_u64_mul (generic_ctx->keyspace, mask_ctx->bfs_cnt) == true)
      {
        event_log_error (hashcat_ctx, "Integer overflow detected in whole-candidate hybrid keyspace.");

        return -1;
      }

      base_candidates = generic_ctx->keyspace * mask_ctx->bfs_cnt;
    }

    for (u32 i = 0; i < user_options->rp_files_cnt; i++) logfile_sub_var_string ("rulefile", user_options->rp_files[i]);

    if (overflow_check_u64_mul (base_candidates, straight_ctx->kernel_rules_cnt) == true)
    {
      event_log_error (hashcat_ctx, "Integer overflow detected after applying whole-candidate rules.");

      return -1;
    }

    status_ctx->words_cnt = base_candidates * straight_ctx->kernel_rules_cnt;

    return 0;
  }

  // Multi-file -a 1 builds its base candidates in fill_multi(), not through a generic base feed.
  // Finish its full Cartesian count before the feed branch sees the intentionally unopened base
  // instance. The final wordlist remains the GPU combinator amplifier.

  if ((user_options->attack_mode == ATTACK_MODE_COMBI) && (combinator_ctx->dicts_cnt > 2))
  {
    u64 total = 1;

    for (int i = 0; i < combinator_ctx->dicts_cnt; i++)
    {
      logfile_sub_string (combinator_ctx->dicts[i]);

      if (overflow_check_u64_mul (total, combinator_ctx->combs_counts[i]) == true)
      {
        event_log_error (hashcat_ctx, "Integer overflow detected in multi-file combination keyspace.");

        return -1;
      }

      total *= combinator_ctx->combs_counts[i];
    }

    status_ctx->words_cnt = total;

    return 0;
  }

  if (user_options_extra->base_source == BASE_SOURCE_FEED)
  {
    // An attack that is really a queue of attacks reads one dictionary per round, and this is the
    // round that says which. The instance is opened here rather than at init because an induction
    // dictionary does not exist until the round before it is read.

    if (user_options_extra->base_scope == BASE_SCOPE_PER_ROUND)
    {
      if (induct_ctx->induction_dictionaries_cnt)
      {
        straight_ctx->dict = induct_ctx->induction_dictionaries[induct_ctx->induction_dictionaries_pos];
      }
      else
      {
        straight_ctx->dict = straight_ctx->dicts[straight_ctx->dicts_pos];
      }

      logfile_sub_string (straight_ctx->dict);

      for (u32 i = 0; i < user_options->rp_files_cnt; i++)
      {
        logfile_sub_var_string ("rulefile", user_options->rp_files[i]);
      }

      const u64 round_words = straight_ctx_round_words (hashcat_ctx, straight_ctx->dict);

      if (round_words == GENERIC_KEYSPACE_ERROR) return -1;

      if (round_words == 0)
      {
        status_ctx->words_cnt = 0;

        logfile_sub_msg ("STOP");

        return 0;
      }
    }

    const generic_ctx_t *generic_ctx = &hashcat_ctx->generic_ctx[GENERIC_ROLE_BASE];

    if (generic_ctx->keyspace == GENERIC_KEYSPACE_UNKNOWN)
    {
      status_ctx->words_cnt = GENERIC_KEYSPACE_UNKNOWN;

      return 0;
    }

    // -a 9 pairs word N with salt N, so the two counts have to agree exactly. Per round, because with
    // more than one dictionary each one is its own attack over the same salts and each has to line up
    // on its own.

    if (user_options->attack_mode == ATTACK_MODE_ASSOCIATION)
    {
      if (generic_association_in_sync (hashcat_ctx, generic_ctx) == -1) return -1;
    }

    u64 amplifier = 1;

    if (user_options_extra->attack_kern == ATTACK_KERN_STRAIGHT)
    {
      amplifier = straight_ctx->kernel_rules_cnt;
    }
    else if (user_options_extra->attack_kern == ATTACK_KERN_COMBI)
    {
      amplifier = combinator_ctx->combs_cnt;
    }

    if (overflow_check_u64_mul (generic_ctx->keyspace, amplifier) == true)
    {
      event_log_error (hashcat_ctx, "Integer overflow detected in keyspace of feed: %s", generic_ctx->plugin_name);

      return -1;
    }

    status_ctx->words_cnt = generic_ctx->keyspace * amplifier;

    return 0;
  }

  // What is left below is the attacks whose base word is not a feed. Shooter's unaliased multi-file
  // -a 1 reads its base with the wordlist reader. -a 7 under the pure kernel takes its base words from
  // the mask, as does -a 12 under the pure kernel when its mask ends in ?w.

  if (user_options->attack_mode == ATTACK_MODE_COMBI)
  {
    for (int i = 0; i < combinator_ctx->dicts_cnt; i++) logfile_sub_string (combinator_ctx->dicts[i]);

    // Both dictionaries were counted by their own feed instance and the base one sits in the base
    // slot, whichever of the two the combinator picked. There is nothing to re-read per round.

    const generic_ctx_t *generic_ctx = &hashcat_ctx->generic_ctx[GENERIC_ROLE_BASE];

    const char *dict = (combinator_ctx->combs_mode == COMBINATOR_MODE_BASE_LEFT) ? combinator_ctx->dict1 : combinator_ctx->dict2;

    if (straight_ctx_words_apply (hashcat_ctx, generic_ctx->keyspace, combinator_ctx->combs_cnt, dict) == -1) return -1;

    if (status_ctx->words_cnt == 0)
    {
      logfile_sub_msg ("STOP");

      return 0;
    }
  }
  else if (user_options->attack_mode == ATTACK_MODE_BF)
  {
    logfile_sub_string (mask_ctx->mask);
  }
  else if (user_options->attack_mode == ATTACK_MODE_HYBRID)
  {
    straight_ctx->dict = straight_ctx->dicts[straight_ctx->dicts_pos];

    logfile_sub_string (straight_ctx->dict);
    logfile_sub_string (mask_ctx->mask);

    // The pure kernel amplifies with the dictionary and takes its base words from the mask, so the
    // keyspace is the mask size times the dictionary, and the dictionary was counted once by the
    // amplifier instance.

    const u64 words_cnt = hashcat_ctx->generic_ctx[GENERIC_ROLE_AMP].keyspace;

    if (straight_ctx_words_apply (hashcat_ctx, words_cnt, mask_ctx->bfs_cnt, straight_ctx->dict) == -1) return -1;

    if (status_ctx->words_cnt == 0)
    {
      logfile_sub_msg ("STOP");

      return 0;
    }
  }

  return 0;
}

int straight_ctx_init (hashcat_ctx_t *hashcat_ctx)
{
  straight_ctx_t       *straight_ctx       = hashcat_ctx->straight_ctx;
  user_options_t       *user_options       = hashcat_ctx->user_options;
  user_options_extra_t *user_options_extra = hashcat_ctx->user_options_extra;

  straight_ctx->enabled = false;

  if (user_options->usage         > 0)    return 0;
  if (user_options->backend_info  > 0)    return 0;
  if (user_options->hash_info     > 0)    return 0;

  if (user_options->left         == true) return 0;
  if (user_options->show         == true) return 0;
  if (user_options->version      == true) return 0;

  if ((user_options->attack_mode == ATTACK_MODE_BF) && (user_options_extra->whole_candidate_rules == false)) return 0;

  straight_ctx->enabled = true;

  /**
   * generate NOP rules
   */

  if (user_options->attack_mode == ATTACK_MODE_MULTI_HYBRID)
  {
    straight_ctx->kernel_rules_buf = (kernel_rule_t *) hcmalloc (sizeof (kernel_rule_t));

    memset (straight_ctx->kernel_rules_buf, 0, sizeof (kernel_rule_t));

    straight_ctx->kernel_rules_buf[0].cmds[0] = RULE_OP_MANGLE_NOOP;

    straight_ctx->kernel_rules_cnt = 1;
  }
  else if ((user_options->rp_files_cnt == 0) && (user_options->rp_gen == 0))
  {
    straight_ctx->kernel_rules_buf = (kernel_rule_t *) hcmalloc (sizeof (kernel_rule_t));

    memset (straight_ctx->kernel_rules_buf, 0, sizeof (kernel_rule_t));

    straight_ctx->kernel_rules_buf[0].cmds[0] = RULE_OP_MANGLE_NOOP;

    straight_ctx->kernel_rules_cnt = 1;
  }
  else
  {
    if (user_options->rp_files_cnt)
    {
      EVENT (EVENT_RULESFILES_PARSE_PRE);

      if (kernel_rules_load (hashcat_ctx, &straight_ctx->kernel_rules_buf, &straight_ctx->kernel_rules_cnt) == -1) return -1;

      EVENT (EVENT_RULESFILES_PARSE_POST);
    }
    else if (user_options->rp_gen)
    {
      if (kernel_rules_generate (hashcat_ctx, &straight_ctx->kernel_rules_buf, &straight_ctx->kernel_rules_cnt, user_options->rp_gen_func_sel) == -1) return -1;
    }
  }

  /**
   * wordlist based work
   */

  // A feed handed every source at once lays them end to end into one keyspace, so there is no
  // dictionary list to build here and no dictionary loop for inner1_loop to run. A feed scoped to one
  // round still needs the list, because that loop is what says which dictionary each round reads. The
  // rules above are this context's either way, because the feed amplifies with them and does not own
  // them.

  if (user_options_extra->base_source == BASE_SOURCE_FEED)
  {
    if (user_options_extra->base_scope == BASE_SCOPE_ALL_SOURCES) return 0;
  }

  // -a 9 splitting its own hash file has no dictionaries. Its rounds are the words one account name
  // becomes, so the list is a round per word and the widest name in the file says how many. Every round
  // hands out one word per hash, and an account with fewer words repeats its last one, because the
  // kernel reads the salt index off the word's position in the batch and no account can sit a round out.

  if (user_options_extra->association_autosplit == true)
  {
    if (straight_ctx_add_association_rounds (hashcat_ctx) == -1) return -1;
  }
  else if ((user_options->attack_mode == ATTACK_MODE_STRAIGHT) || (user_options->attack_mode == ATTACK_MODE_ASSOCIATION))
  {
    // Reading candidates from stdin is the one case with no dictionaries to list. Testing for that
    // rather than for the wordlist reader is what lets a feed scoped to one round have the list too.

    if (user_options_extra->wordlist_mode != WL_MODE_STDIN)
    {
      if (straight_ctx_add_workv (hashcat_ctx, 0, user_options_extra->hc_workc) == -1) return -1;
    }
  }
  else if (user_options->attack_mode == ATTACK_MODE_HYBRID)
  {
    // the mask is first and the wordlists follow it. A ?q wordlist is not in this list: it amplifies,
    // and only the base word source is listed here.

    const int to = user_options_extra->hc_workc - ((user_options_extra->hybrid_q == true) ? 1 : 0);

    if (straight_ctx_add_workv (hashcat_ctx, 1, to) == -1) return -1;
  }
  return 0;
}

void straight_ctx_destroy (hashcat_ctx_t *hashcat_ctx)
{
  straight_ctx_t *straight_ctx = hashcat_ctx->straight_ctx;

  if (straight_ctx->enabled == false) return;

  for (u32 dict_pos = 0; dict_pos < straight_ctx->dicts_cnt; dict_pos++)
  {
    hcfree (straight_ctx->dicts[dict_pos]);
  }

  hcfree (straight_ctx->dicts);
  hcfree (straight_ctx->kernel_rules_buf);

  memset (straight_ctx, 0, sizeof (straight_ctx_t));
}

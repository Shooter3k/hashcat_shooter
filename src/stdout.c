/**
 * Author......: See docs/credits.txt
 * License.....: MIT
 */

#include "common.h"
#include "types.h"
#include "event.h"
#include "locking.h"
#include "emu_inc_rp.h"
#include "emu_inc_rp_optimized.h"
#include "mpsp.h"
#include "backend.h"
#include "memory.h"
#include "shared.h"
#include "thread.h"
#include "outfile.h"
#include "stdout.h"

typedef struct stdout_rule_pool stdout_rule_pool_t;

typedef struct stdout_rule_worker
{
  hashcat_ctx_t *hashcat_ctx;

  stdout_rule_pool_t *pool;

  const u32 *pw;

  u32 pw_cnt;
  u32 pw_len;

  u64 rule_off;
  u32 rule_cnt;

  char  *out_buf;
  size_t out_len;
  size_t out_size;

  bool error;

  hc_thread_t           thread;
  hc_thread_semaphore_t start;

  bool live;

} stdout_rule_worker_t;

struct stdout_rule_pool
{
  stdout_rule_worker_t *workers;

  u32 workers_cnt;

  hc_thread_semaphore_t done;

  bool stop;
};

static void out_check_control (hashcat_ctx_t *hashcat_ctx, out_t *out)
{
  status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  if (status_ctx->run_thread_level1 == false)
  {
    out->abort = true;

    return;
  }

  // Candidate generation happens on the host inside process_stdout(), so the
  // backend pause checks cannot stop a large buffered batch by themselves.
  // Checking at each output write keeps [p]ause responsive without a branch
  // on every generated candidate.

  if ((status_ctx->devices_status == STATUS_PAUSED)
   && (status_ctx->run_thread_level1 == true))
  {
    outfile_ctx_t *outfile_ctx = hashcat_ctx->outfile_ctx;

    // Let the monitor save the last fully committed batch while candidate
    // generation is paused. The current partial batch is deliberately not
    // part of that checkpoint and will be truncated away on restore.

    hc_thread_mutex_unlock (outfile_ctx->mux_outfile);

    while ((status_ctx->devices_status == STATUS_PAUSED)
        && (status_ctx->run_thread_level1 == true))
    {
      usleep (10000);
    }

    hc_thread_mutex_lock (outfile_ctx->mux_outfile);

    if (status_ctx->run_thread_level1 == false) out->abort = true;
  }
}

static void out_flush (hashcat_ctx_t *hashcat_ctx, out_t *out)
{
  if (out->len == 0) return;
  if (out->error == true) return;

  if (hc_fwrite (out->buf, 1, out->len, &out->fp) != (size_t) out->len)
  {
    out->error = true;

    return;
  }

  out->len = 0;

  out_check_control (hashcat_ctx, out);
}

static void out_push (hashcat_ctx_t *hashcat_ctx, out_t *out, const u8 *pw_buf, const int pw_len)
{
  if ((out->error == true) || (out->abort == true)) return;

  char *ptr = out->buf + out->len;

  memcpy (ptr, pw_buf, pw_len);

  #if defined (_WIN)

  ptr[pw_len + 0] = '\r';
  ptr[pw_len + 1] = '\n';

  out->len += pw_len + 2;

  #else

  ptr[pw_len] = '\n';

  out->len += pw_len + 1;

  #endif

  if (out->len >= HCBUFSIZ_SMALL - 300)
  {
    out_flush (hashcat_ctx, out);
  }
}

static void out_push_raw (hashcat_ctx_t *hashcat_ctx, out_t *out, const char *buf, size_t len)
{
  out_flush (hashcat_ctx, out);

  while ((len > 0) && (out->error == false) && (out->abort == false))
  {
    const size_t chunk = MIN (len, 256 * 1024);

    if (hc_fwrite (buf, 1, chunk, &out->fp) != chunk)
    {
      out->error = true;

      break;
    }

    buf += chunk;
    len -= chunk;

    out_check_control (hashcat_ctx, out);
  }
}

static bool stdout_rule_worker_reserve (stdout_rule_worker_t *worker, const size_t need)
{
  if (need <= worker->out_size) return true;

  size_t new_size = (worker->out_size > 0) ? worker->out_size : (1024 * 1024);

  while (new_size < need)
  {
    if (new_size > (SIZE_MAX / 2))
    {
      new_size = need;

      break;
    }

    new_size *= 2;
  }

  char *new_buf = (char *) realloc (worker->out_buf, new_size);

  if (new_buf == NULL) return false;

  worker->out_buf  = new_buf;
  worker->out_size = new_size;

  return true;
}

static void stdout_rule_worker_generate (stdout_rule_worker_t *worker)
{
  hashconfig_t   *hashconfig   = worker->hashcat_ctx->hashconfig;
  straight_ctx_t *straight_ctx = worker->hashcat_ctx->straight_ctx;

  #define RULE_BUF_SZ (PW_MAX / sizeof (u32))

  u32 plain_buf[RULE_BUF_SZ] = { 0 };

  const u8 *plain_ptr = (const u8 *) plain_buf;

  worker->out_len = 0;
  worker->error   = false;

  for (u32 rule_pos = 0; rule_pos < worker->rule_cnt; rule_pos++)
  {
    for (u32 i = 0; i < worker->pw_cnt; i++) plain_buf[i] = worker->pw[i];

    const u64 off = worker->rule_off + rule_pos;

    u32 plain_len;

    if (hashconfig->opti_type & OPTI_TYPE_OPTIMIZED_KERNEL)
    {
      plain_len = apply_rules_optimized (straight_ctx->kernel_rules_buf[off].cmds, &plain_buf[0], &plain_buf[4], worker->pw_len);
    }
    else
    {
      plain_len = apply_rules (straight_ctx->kernel_rules_buf[off].cmds, plain_buf, worker->pw_len);
    }

    if (plain_len > hashconfig->pw_max) plain_len = hashconfig->pw_max;

    #if defined (_WIN)
    const size_t line_len = plain_len + 2;
    #else
    const size_t line_len = plain_len + 1;
    #endif

    const size_t need = worker->out_len + line_len;

    if (stdout_rule_worker_reserve (worker, need) == false)
    {
      worker->error = true;

      break;
    }

    char *dst = worker->out_buf + worker->out_len;

    memcpy (dst, plain_ptr, plain_len);

    #if defined (_WIN)
    dst[plain_len + 0] = '\r';
    dst[plain_len + 1] = '\n';
    #else
    dst[plain_len] = '\n';
    #endif

    worker->out_len = need;

    memset (plain_buf, 0, PW_MAX);
  }

  #undef RULE_BUF_SZ

}

#if defined (_WIN)
static HC_API_CALL DWORD stdout_rule_worker_run (void *p)
#else
static HC_API_CALL void *stdout_rule_worker_run (void *p)
#endif
{
  stdout_rule_worker_t *worker = (stdout_rule_worker_t *) p;

  stdout_rule_pool_t *pool = worker->pool;

  while (true)
  {
    hc_thread_sem_wait (worker->start);

    if (pool->stop == true) break;

    stdout_rule_worker_generate (worker);

    hc_thread_sem_post (pool->done);
  }

  return 0;
}

static bool stdout_rule_pool_init (stdout_rule_pool_t *pool, const u32 workers_cnt)
{
  memset (pool, 0, sizeof (stdout_rule_pool_t));

  pool->workers = (stdout_rule_worker_t *) hccalloc (workers_cnt, sizeof (stdout_rule_worker_t));

  if (pool->workers == NULL) return false;

  pool->workers_cnt = workers_cnt;

  hc_thread_sem_init (pool->done);

  for (u32 worker_pos = 0; worker_pos < workers_cnt; worker_pos++)
  {
    stdout_rule_worker_t *worker = &pool->workers[worker_pos];

    worker->pool = pool;

    hc_thread_sem_init (worker->start);

    #if defined (_WIN)
    hc_thread_create (worker->thread, stdout_rule_worker_run, worker);

    worker->live = (worker->thread != NULL);
    #else
    worker->live = (hc_thread_create (worker->thread, stdout_rule_worker_run, worker) == 0);
    #endif
  }

  return true;
}

static void stdout_rule_pool_destroy (stdout_rule_pool_t *pool)
{
  pool->stop = true;

  for (u32 worker_pos = 0; worker_pos < pool->workers_cnt; worker_pos++)
  {
    stdout_rule_worker_t *worker = &pool->workers[worker_pos];

    if (worker->live == true) hc_thread_sem_post (worker->start);
  }

  for (u32 worker_pos = 0; worker_pos < pool->workers_cnt; worker_pos++)
  {
    stdout_rule_worker_t *worker = &pool->workers[worker_pos];

    if (worker->live == true)
    {
      hc_thread_join (worker->thread);

      #if defined (_WIN)
      CloseHandle (worker->thread);
      #endif
    }

    hc_thread_sem_close (worker->start);

    hcfree (worker->out_buf);
  }

  hc_thread_sem_close (pool->done);

  hcfree (pool->workers);
}

static void stdout_rule_pool_run (stdout_rule_pool_t *pool, const u32 active_cnt)
{
  u32 live_cnt = 0;

  for (u32 worker_pos = 0; worker_pos < active_cnt; worker_pos++)
  {
    stdout_rule_worker_t *worker = &pool->workers[worker_pos];

    if (worker->live == true)
    {
      hc_thread_sem_post (worker->start);

      live_cnt++;
    }
  }

  for (u32 worker_pos = 0; worker_pos < active_cnt; worker_pos++)
  {
    stdout_rule_worker_t *worker = &pool->workers[worker_pos];

    if (worker->live == false) stdout_rule_worker_generate (worker);
  }

  for (u32 worker_pos = 0; worker_pos < live_cnt; worker_pos++) hc_thread_sem_wait (pool->done);
}

static u32 stdout_rule_threads (const hashcat_ctx_t *hashcat_ctx, const u32 rule_cnt)
{
  const user_options_t *user_options = hashcat_ctx->user_options;

  const int processor_count = hc_get_processor_count ();

  int threads = 0;

  const char *threads_env = getenv ("HASHCAT_STDOUT_THREADS");

  if (threads_env != NULL) threads = atoi (threads_env);

  if (threads <= 0)
  {
    if (user_options->workload_profile == 4)
    {
      // Rule application writes a variable-length output stream as well as
      // doing branch-heavy CPU work. On large hosts it stops scaling before
      // every logical processor is occupied; about five workers per sixteen
      // processors was the best sustained setting on the 64-processor group
      // used by the shooter host. Keep small hosts at their old -w 4 floor.

      threads = MAX (8, (processor_count * 5) / 16);
    }
    else
    {
      threads = 1 << (user_options->workload_profile - 1);
    }
  }

  threads = MIN (threads, processor_count);
  threads = MIN (threads, 64);
  threads = MIN (threads, (int) CEILDIV (rule_cnt, 4096));

  return (threads > 1) ? (u32) threads : 1;
}

static int stdout_rules_parallel (hashcat_ctx_t *hashcat_ctx, out_t *out, const u32 *pw, const u32 pw_cnt, const u32 pw_len, const u64 rule_off, const u32 rule_cnt, stdout_rule_pool_t *pool)
{
  stdout_rule_worker_t *workers = pool->workers;

  const u32 workers_cnt = pool->workers_cnt;

  u64 off = rule_off;

  const u32 rules_per_worker = rule_cnt / workers_cnt;
  const u32 rules_remainder  = rule_cnt % workers_cnt;

  for (u32 worker_pos = 0; worker_pos < workers_cnt; worker_pos++)
  {
    stdout_rule_worker_t *worker = &workers[worker_pos];

    worker->hashcat_ctx = hashcat_ctx;
    worker->pw          = pw;
    worker->pw_cnt      = pw_cnt;
    worker->pw_len      = pw_len;
    worker->rule_off    = off;
    worker->rule_cnt    = rules_per_worker + ((worker_pos < rules_remainder) ? 1 : 0);

    off += worker->rule_cnt;

  }

  stdout_rule_pool_run (pool, workers_cnt);

  for (u32 worker_pos = 0; worker_pos < workers_cnt; worker_pos++)
  {
    stdout_rule_worker_t *worker = &workers[worker_pos];

    if (worker->error == true)
    {
      event_log_error (hashcat_ctx, "Insufficient memory while buffering parallel --stdout rule output.");

      return -1;
    }

    out_push_raw (hashcat_ctx, out, worker->out_buf, worker->out_len);
  }

  return 0;
}

static int stdout_words_parallel (hashcat_ctx_t *hashcat_ctx, out_t *out, const u32 *pws_comp, const u32 off_blk, const pw_idx_t **pw_idx_io, const pw_idx_t *pw_idx_last, const u64 rule_off, const u32 rule_cnt, stdout_rule_pool_t *pool)
{
  stdout_rule_worker_t *workers = pool->workers;

  const u32 workers_cnt = pool->workers_cnt;

  const pw_idx_t *pw_idx = *pw_idx_io;

  while ((pw_idx <= pw_idx_last) && (out->abort == false))
  {
    const u64 words_left = (u64) (pw_idx_last - pw_idx) + 1;

    const u32 active_cnt = (u32) MIN (words_left, workers_cnt);

    for (u32 worker_pos = 0; worker_pos < active_cnt; worker_pos++)
    {
      stdout_rule_worker_t *worker = &workers[worker_pos];

      worker->hashcat_ctx = hashcat_ctx;
      worker->pw          = pws_comp + (pw_idx[worker_pos].off - off_blk);
      worker->pw_cnt      = pw_idx[worker_pos].cnt;
      worker->pw_len      = pw_idx[worker_pos].len;
      worker->rule_off    = rule_off;
      worker->rule_cnt    = rule_cnt;

    }

    stdout_rule_pool_run (pool, active_cnt);

    for (u32 worker_pos = 0; worker_pos < active_cnt; worker_pos++)
    {
      stdout_rule_worker_t *worker = &workers[worker_pos];

      if (worker->error == true)
      {
        event_log_error (hashcat_ctx, "Insufficient memory while buffering parallel --stdout rule output.");

        return -1;
      }

      out_push_raw (hashcat_ctx, out, worker->out_buf, worker->out_len);
    }

    pw_idx += active_cnt;
  }

  *pw_idx_io = pw_idx;

  return 0;
}

static int stdout_lock_turn (hashcat_ctx_t *hashcat_ctx, const u64 words_off)
{
  outfile_ctx_t *outfile_ctx = hashcat_ctx->outfile_ctx;
  restore_ctx_t *restore_ctx = hashcat_ctx->restore_ctx;
  status_ctx_t  *status_ctx  = hashcat_ctx->status_ctx;

  while (true)
  {
    hc_thread_mutex_lock (outfile_ctx->mux_outfile);

    if (restore_ctx->stdout_next_words == words_off) return 0;

    if (restore_ctx->stdout_next_words > words_off)
    {
      hc_thread_mutex_unlock (outfile_ctx->mux_outfile);

      event_log_error (hashcat_ctx, "Internal --stdout ordering error at position %" PRIu64 ".", words_off);

      return -1;
    }

    const bool keep_running = status_ctx->run_thread_level1;

    hc_thread_mutex_unlock (outfile_ctx->mux_outfile);

    if (keep_running == false) return 1;

    usleep (1000);
  }
}

static int stdout_commit_locked (hashcat_ctx_t *hashcat_ctx, const u64 words_fin, const bool update_output_size)
{
  outfile_ctx_t *outfile_ctx = hashcat_ctx->outfile_ctx;
  restore_ctx_t *restore_ctx = hashcat_ctx->restore_ctx;

  if ((outfile_ctx->filename != NULL) && (update_output_size == true))
  {
    struct stat st;

    if (stat (outfile_ctx->filename, &st) == -1)
    {
      event_log_error (hashcat_ctx, "Failed to record the --stdout outfile position: %s", strerror (errno));

      return -1;
    }

    restore_ctx->stdout_output_size       = (u64) st.st_size;
    restore_ctx->stdout_output_size_valid = true;
  }

  restore_ctx->stdout_next_words      = words_fin;
  restore_ctx->stdout_committed_words = words_fin;

  return 0;
}

void stdout_restore_reset (hashcat_ctx_t *hashcat_ctx, const u64 words_off)
{
  outfile_ctx_t *outfile_ctx = hashcat_ctx->outfile_ctx;
  restore_ctx_t *restore_ctx = hashcat_ctx->restore_ctx;

  hc_thread_mutex_lock (outfile_ctx->mux_outfile);

  restore_ctx->stdout_next_words      = words_off;
  restore_ctx->stdout_committed_words = words_off;

  hc_thread_mutex_unlock (outfile_ctx->mux_outfile);
}

int stdout_restore_skip (hashcat_ctx_t *hashcat_ctx, const u64 words_off, const u64 words_fin)
{
  outfile_ctx_t *outfile_ctx = hashcat_ctx->outfile_ctx;

  const int lock_rc = stdout_lock_turn (hashcat_ctx, words_off);

  if (lock_rc != 0) return (lock_rc == 1) ? 0 : -1;

  const int rc = stdout_commit_locked (hashcat_ctx, words_fin, false);

  hc_thread_mutex_unlock (outfile_ctx->mux_outfile);

  return rc;
}

int process_stdout (hashcat_ctx_t *hashcat_ctx, hc_device_param_t *device_param, const u64 pws_cnt)
{
  combinator_ctx_t     *combinator_ctx     = hashcat_ctx->combinator_ctx;
  hashconfig_t         *hashconfig         = hashcat_ctx->hashconfig;
  mask_ctx_t           *mask_ctx           = hashcat_ctx->mask_ctx;
  outfile_ctx_t         *outfile_ctx         = hashcat_ctx->outfile_ctx;
  straight_ctx_t        *straight_ctx        = hashcat_ctx->straight_ctx;
  user_options_extra_t *user_options_extra = hashcat_ctx->user_options_extra;
  user_options_t       *user_options       = hashcat_ctx->user_options;

  // Multiple devices reserve disjoint keyspace ranges concurrently. Commit
  // their candidate batches in range order so the outfile is a true prefix of
  // the attack and can be truncated to a saved restore boundary exactly.

  const u64 words_off = device_param->words_off_launch;
  const u64 words_fin = device_param->words_fin_launch;

  // run_cracker() may call process_stdout() several times for one base-word
  // batch, once per amplifier chunk. Keep the same batch at the head of the
  // output queue until its final chunk has been written. A restore point can
  // then describe the attack with the existing base-word position while its
  // byte offset always points to the end of a complete batch.

  const bool batch_complete = (device_param->outerloop_multi <= 1.0);

  const int lock_rc = stdout_lock_turn (hashcat_ctx, words_off);

  if (lock_rc != 0) return (lock_rc == 1) ? 0 : -1;

  char *filename = outfile_ctx->filename;

  out_t out;

  if (filename)
  {
    if (outfile_open_file (hashcat_ctx, &out.fp) == -1)
    {
      hc_thread_mutex_unlock (outfile_ctx->mux_outfile);

      return -1;
    }
  }
  else
  {
    HCFILE *fp = &out.fp;

    fp->fd       = fileno (stdout);
    fp->pfp      = stdout;
    fp->gfp      = NULL;
    fp->ufp      = NULL;
    fp->bom_size = 0;
    fp->path     = NULL;
    fp->mode     = NULL;
  }

  out.len   = 0;
  out.error = false;
  out.abort = false;

  #define BUF_SZ (PW_MAX / sizeof(u32))

  u32 plain_buf[BUF_SZ] = { 0 };

  u8 *const plain_ptr = (u8 *) plain_buf;

  u32 plain_len = 0;

  const u32 il_cnt = device_param->kernel_param.il_cnt; // ugly, i know

  int rc = 0;

  if ((user_options->attack_mode == ATTACK_MODE_BF) && (user_options_extra->whole_candidate_rules == false))
  {
    for (u64 gidvid = 0; (gidvid < pws_cnt) && (out.abort == false); gidvid++)
    {
      for (u32 il_pos = 0; (il_pos < il_cnt) && (out.abort == false); il_pos++)
      {
        u64 l_off = device_param->kernel_params_mp_l_buf64[3] + gidvid;
        u64 r_off = device_param->kernel_params_mp_r_buf64[3] + il_pos;

        u32 l_start = device_param->kernel_params_mp_l_buf32[5];
        u32 r_start = device_param->kernel_params_mp_r_buf32[5];

        u32 l_stop = device_param->kernel_params_mp_l_buf32[4];
        u32 r_stop = device_param->kernel_params_mp_r_buf32[4];

        sp_exec (l_off, (char *) plain_ptr + l_start, mask_ctx->root_css_buf, mask_ctx->markov_css_buf, l_start, l_start + l_stop);
        sp_exec (r_off, (char *) plain_ptr + r_start, mask_ctx->root_css_buf, mask_ctx->markov_css_buf, r_start, r_start + r_stop);

        plain_len = mask_ctx->css_cnt;

        out_push (hashcat_ctx, &out, plain_ptr, plain_len);
      }
    }
  }
  // The base words are the mask and they exist only on the device, so there is no host word buffer to
  // copy and the candidate is put together from the outer loop position and the amplifier. That is
  // -a 7 under a pure kernel, and -a 12 under a pure kernel when its mask ends in ?w.

  else if ((user_options_extra->attack_kern == ATTACK_KERN_COMBI) && (user_options_extra->base_source == BASE_SOURCE_MASK))
  {
    for (u64 gidvid = 0; (gidvid < pws_cnt) && (out.abort == false); gidvid++)
    {
      for (u32 il_pos = 0; (il_pos < il_cnt) && (out.abort == false); il_pos++)
      {
        u64 off = device_param->kernel_params_mp_buf64[3] + gidvid;

        u32 start = 0;
        u32 stop  = device_param->kernel_params_mp_buf32[4];

        sp_exec (off, (char *) plain_ptr, mask_ctx->root_css_buf, mask_ctx->markov_css_buf, start, start + stop);

        plain_len = stop;

        char *comb_buf = (char *) device_param->combs_buf[il_pos].i;
        u32   comb_len =          device_param->combs_buf[il_pos].pw_len;

        memcpy (plain_ptr + plain_len, comb_buf, comb_len);

        plain_len += comb_len;

        if (plain_len > hashconfig->pw_max) plain_len = hashconfig->pw_max;

        out_push (hashcat_ctx, &out, plain_ptr, plain_len);
      }
    }
  }
  else
  {
    // Most modes below require transferring pw index/buffer data from device
    // to host. Straight --stdout still owns the compressed host batch and can
    // consume it in place.

    const bool host_straight = (user_options->stdout_flag == true)
                            && (user_options->attack_mode == ATTACK_MODE_STRAIGHT)
                            && (user_options_extra->attack_kern == ATTACK_KERN_STRAIGHT);

    const u64 blk_cnt_max = (host_straight == true)
                          ? pws_cnt
                          : device_param->size_pws_idx / (sizeof (pw_idx_t));

    pw_idx_t *const pws_idx_blk  = device_param->pws_idx;
    u32      *const pws_comp_blk = device_param->pws_comp;

    u64 gidvid_blk = 0; // gidvid of first password in current block

    while ((gidvid_blk < pws_cnt) && (out.abort == false))
    {
      // copy the pw indexes from device for this block

      u64 remain  = pws_cnt - gidvid_blk;
      u64 blk_cnt = MIN (remain, blk_cnt_max);

      if (host_straight == false)
      {
        rc = copy_pws_idx (hashcat_ctx, device_param, gidvid_blk, blk_cnt, pws_idx_blk);
      }

      if (rc == -1) break;

      const u32 off_blk = (blk_cnt > 0) ? pws_idx_blk[0].off : 0;

      const pw_idx_t *pw_idx      = device_param->pws_idx;
      const pw_idx_t *pw_idx_last = pw_idx + (blk_cnt - 1);

      // copy the pw buffer data from device for this block

      u32 copy_cnt = (pw_idx_last->off + pw_idx_last->cnt) - pws_idx_blk->off;

      if (host_straight == false)
      {
        rc = copy_pws_comp (hashcat_ctx, device_param, off_blk, copy_cnt, pws_comp_blk);
      }

      if (rc == -1) break;

      if ((user_options_extra->attack_kern == ATTACK_KERN_STRAIGHT) || (user_options_extra->whole_candidate_rules == true))
      {
        const bool parallel_rules = (user_options_extra->attack_kern == ATTACK_KERN_STRAIGHT)
                                 && (user_options->attack_mode == ATTACK_MODE_STRAIGHT);

        const u32 workers_cnt = (parallel_rules == true) ? stdout_rule_threads (hashcat_ctx, il_cnt) : 1;

        if (workers_cnt > 1)
        {
          stdout_rule_pool_t pool;

          if (stdout_rule_pool_init (&pool, workers_cnt) == false)
          {
            rc = -1;
          }
          else
          {
            const u64 words_in_block = (u64) (pw_idx_last - pw_idx) + 1;

            if (words_in_block >= workers_cnt)
            {
              rc = stdout_words_parallel (hashcat_ctx, &out, pws_comp_blk, off_blk, &pw_idx, pw_idx_last, device_param->innerloop_pos, il_cnt, &pool);
            }
            else
            {
              while ((pw_idx <= pw_idx_last) && (out.abort == false))
              {
                const u32 *pw = pws_comp_blk + (pw_idx->off - off_blk);

                rc = stdout_rules_parallel (hashcat_ctx, &out, pw, pw_idx->cnt, pw_idx->len, device_param->innerloop_pos, il_cnt, &pool);

                if (rc == -1) break;

                pw_idx++;
              }
            }
          }

          if (pool.workers != NULL) stdout_rule_pool_destroy (&pool);
        }
        else
        {
          while ((pw_idx <= pw_idx_last) && (out.abort == false))
          {
            u32 *pw = pws_comp_blk + (pw_idx->off - off_blk);

            for (u32 il_pos = 0; (il_pos < il_cnt) && (out.abort == false); il_pos++)
            {
              const u64 off = device_param->innerloop_pos + il_pos;

              for (u32 i = 0; i < pw_idx->cnt; i++)
              {
                plain_buf[i] = pw[i];
              }

              if (hashconfig->opti_type & OPTI_TYPE_OPTIMIZED_KERNEL)
              {
                plain_len = apply_rules_optimized (straight_ctx->kernel_rules_buf[off].cmds, &plain_buf[0], &plain_buf[4], pw_idx->len);
              }
              else
              {
                plain_len = apply_rules (straight_ctx->kernel_rules_buf[off].cmds, plain_buf, pw_idx->len);
              }

              if (plain_len > hashconfig->pw_max) plain_len = hashconfig->pw_max;

              out_push (hashcat_ctx, &out, plain_ptr, plain_len);

              memset (plain_ptr, 0, PW_MAX);
            }

            pw_idx++;
          }
        }
      }
      else if (user_options->attack_mode == ATTACK_MODE_COMBI)
      {
        while ((pw_idx <= pw_idx_last) && (out.abort == false))
        {
          u32 *pw = pws_comp_blk + (pw_idx->off - off_blk);

          for (u32 il_pos = 0; (il_pos < il_cnt) && (out.abort == false); il_pos++)
          {
            for (u32 i = 0; i < pw_idx->cnt; i++)
            {
              plain_buf[i] = pw[i];
            }

            plain_len = pw_idx->len;

            char *comb_buf = (char *) device_param->combs_buf[il_pos].i;
            u32   comb_len =          device_param->combs_buf[il_pos].pw_len;

            if (combinator_ctx->combs_mode == COMBINATOR_MODE_BASE_LEFT)
            {
              memcpy (plain_ptr + plain_len, comb_buf, comb_len);
            }
            else
            {
              memmove (plain_ptr + comb_len, plain_ptr, plain_len);

              memcpy (plain_ptr, comb_buf, comb_len);
            }

            plain_len += comb_len;

            if (plain_len > hashconfig->pw_max) plain_len = hashconfig->pw_max;

            out_push (hashcat_ctx, &out, plain_ptr, plain_len);
          }

          pw_idx++;
        }
      }
      else if (user_options->attack_mode == ATTACK_MODE_HYBRID)
      {
        char mask_buf[256];

        while ((pw_idx <= pw_idx_last) && (out.abort == false))
        {
          const u8 *pw = (const u8 *) (pws_comp_blk + (pw_idx->off - off_blk));

          for (u32 il_pos = 0; (il_pos < il_cnt) && (out.abort == false); il_pos++)
          {
            // Assembled by the same code the outfile uses, so the two cannot drift apart.

            if (device_param->combs_on_host == true)
            {
              plain_len = hybrid_amp_rebuild (hashcat_ctx, device_param, il_pos, plain_ptr, pw, pw_idx->len);
            }
            else
            {
              const u64 off = device_param->kernel_params_mp_buf64[3] + il_pos;

              hybrid_amp_mask (hashcat_ctx, off, mask_buf);

              plain_len = hybrid_assemble (hashcat_ctx, plain_ptr, mask_buf, pw, pw_idx->len, NULL, 0);
            }

            if (plain_len > hashconfig->pw_max) plain_len = hashconfig->pw_max;

            out_push (hashcat_ctx, &out, plain_ptr, plain_len);
          }

          pw_idx++;
        }
      }
      else if (user_options->attack_mode == ATTACK_MODE_HYBRID1)
      {
        while ((pw_idx <= pw_idx_last) && (out.abort == false))
        {
          u32 *pw = pws_comp_blk + (pw_idx->off - off_blk);

          for (u32 il_pos = 0; (il_pos < il_cnt) && (out.abort == false); il_pos++)
          {
            for (u32 i = 0; i < pw_idx->cnt; i++)
            {
              plain_buf[i] = pw[i];
            }

            plain_len = pw_idx->len;

            u64 off = device_param->kernel_params_mp_buf64[3] + il_pos;

            u32 start = 0;
            u32 stop  = device_param->kernel_params_mp_buf32[4];

            sp_exec (off, (char *) plain_ptr + plain_len, mask_ctx->root_css_buf, mask_ctx->markov_css_buf, start, start + stop);

            plain_len += start + stop;

            out_push (hashcat_ctx, &out, plain_ptr, plain_len);
          }

          pw_idx++;
        }
      }
      else if ((user_options->attack_mode == ATTACK_MODE_HYBRID2) && (hashconfig->opti_type & OPTI_TYPE_OPTIMIZED_KERNEL))
      {
        while ((pw_idx <= pw_idx_last) && (out.abort == false))
        {
          char *pw = (char *) (pws_comp_blk + (pw_idx->off - off_blk));

          for (u32 il_pos = 0; (il_pos < il_cnt) && (out.abort == false); il_pos++)
          {
            u64 off = device_param->kernel_params_mp_buf64[3] + il_pos;

            u32 start = 0;
            u32 stop  = device_param->kernel_params_mp_buf32[4];

            sp_exec (off, (char *) plain_ptr, mask_ctx->root_css_buf, mask_ctx->markov_css_buf, start, start + stop);

            plain_len = stop;

            memcpy (plain_ptr + plain_len, pw, pw_idx->len);

            plain_len += pw_idx->len;

            if (plain_len > hashconfig->pw_max) plain_len = hashconfig->pw_max;

            out_push (hashcat_ctx, &out, plain_ptr, plain_len);
          }

          pw_idx++;
        }
      }

      if (rc == -1) break;

      gidvid_blk += blk_cnt; // prepare for next block
    }
  }

  out_flush (hashcat_ctx, &out);

  hc_fflush (&out.fp);

  if (out.error == true)
  {
    event_log_error (hashcat_ctx, "Failed to write generated candidates: %s", strerror (errno));

    rc = -1;
  }

  if (filename)
  {
    hc_unlockfile (&out.fp);

    hc_fclose (&out.fp);
  }

  if ((rc == 0) && (out.abort == false) && (batch_complete == true))
  {
    if (stdout_commit_locked (hashcat_ctx, words_fin, filename != NULL) == -1) rc = -1;
  }

  hc_thread_mutex_unlock (outfile_ctx->mux_outfile);

  return rc;
}

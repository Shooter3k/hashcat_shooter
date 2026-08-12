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
#include "shared.h"
#include "thread.h"
#include "outfile.h"
#include "stdout.h"

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

  status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  if (status_ctx->run_thread_level1 == false)
  {
    out->abort = true;

    return;
  }

  // Candidate generation happens on the host inside process_stdout(), so the
  // backend pause checks cannot stop a large buffered batch by themselves.
  // Checking at each small output flush makes [p]ause responsive without a
  // branch on every generated candidate.

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
  combinator_ctx_t *combinator_ctx = hashcat_ctx->combinator_ctx;
  hashconfig_t     *hashconfig     = hashcat_ctx->hashconfig;
  mask_ctx_t       *mask_ctx       = hashcat_ctx->mask_ctx;
  outfile_ctx_t         *outfile_ctx         = hashcat_ctx->outfile_ctx;
  straight_ctx_t        *straight_ctx        = hashcat_ctx->straight_ctx;
  user_options_t       *user_options       = hashcat_ctx->user_options;
  user_options_extra_t *user_options_extra = hashcat_ctx->user_options_extra;

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
  else if ((user_options->attack_mode == ATTACK_MODE_HYBRID2)
        && (user_options_extra->whole_candidate_rules == false)
        && ((hashconfig->opti_type & OPTI_TYPE_OPTIMIZED_KERNEL) == 0))
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
    // modes below require transferring pw index/buffer data from device to host

    const u64 blk_cnt_max = device_param->size_pws_idx / (sizeof (pw_idx_t));

    pw_idx_t *const pws_idx_blk  = device_param->pws_idx;
    u32      *const pws_comp_blk = device_param->pws_comp;

    u64 gidvid_blk = 0; // gidvid of first password in current block

    while ((gidvid_blk < pws_cnt) && (out.abort == false))
    {
      // copy the pw indexes from device for this block

      u64 remain  = pws_cnt - gidvid_blk;
      u64 blk_cnt = MIN (remain, blk_cnt_max);

      rc = copy_pws_idx (hashcat_ctx, device_param, gidvid_blk, blk_cnt, pws_idx_blk);

      if (rc == -1) break;

      const u32 off_blk = (blk_cnt > 0) ? pws_idx_blk[0].off : 0;

      const pw_idx_t *pw_idx      = device_param->pws_idx;
      const pw_idx_t *pw_idx_last = pw_idx + (blk_cnt - 1);

      // copy the pw buffer data from device for this block

      u32 copy_cnt = (pw_idx_last->off + pw_idx_last->cnt) - pws_idx_blk->off;

      rc = copy_pws_comp (hashcat_ctx, device_param, off_blk, copy_cnt, pws_comp_blk);

      if (rc == -1) break;

      if ((user_options_extra->attack_kern == ATTACK_KERN_STRAIGHT) || (user_options_extra->whole_candidate_rules == true))
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

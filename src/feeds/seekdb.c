/**
 * Author......: See docs/credits.txt
 * License.....: MIT
 */

// Legacy databases put one byte offset after every 8192 lines. New databases put a line/offset pair
// near every four MiB of input instead. Byte-spaced ranges can be scanned independently, which lets
// first use of a multi-gigabyte wordlist use the host CPUs and the NVMe queue in parallel rather than
// making one core page-fault through the whole file. Legacy files are still loaded and converted.

static const size_t SEEKDB_LEGACY_STEP       = 8192;
static const size_t SEEKDB_BYTE_STEP         = 4 * 1024 * 1024;
static const size_t SEEKDB_BYTES_PER_THREAD  = 256 * 1024 * 1024;
static const size_t SAMPLE_SIZE              = 65536;
static const u32    SEEKDB_THREADS_MAX       = 64;

static const u8 SEEKDB_MAGIC[8] = { 'S', 'H', 'S', 'E', 'E', 'K', '0', '1' };

// Where this wordlist's seek database lives, and what the wordlist is.
//
// The two are the same question. A seek database is only valid for the exact file it was built from,
// so it is named after a hash of that file's size, modification time and both of its ends, and looking
// the name up is what decides whether the cached one can be reused. That same hash answers "is this
// still the same wordlist" for anyone else who has to know, so it is handed back rather than left
// inside the file name.
//
// ident is where it goes. It is only written when a path could be built, so a caller that got NULL
// has nothing to read.

static char *seekdb_path (generic_global_ctx_t *global_ctx, const char *wordlist, u64 *ident)
{
  char *seekdb_dir = NULL;

  hc_asprintf (&seekdb_dir, "%s/seekdbs", global_ctx->cache_dir);

  hc_mkdir (seekdb_dir, 0700);

  HCFILE fp;

  if (hc_fopen_raw (&fp, wordlist, "rb") == false)
  {
    hcfree (seekdb_dir);

    return NULL;
  }

  struct stat st;

  if (hc_fstat (&fp, &st) == -1)
  {
    hc_fclose (&fp);

    hcfree (seekdb_dir);

    return NULL;
  }

  XXH64_state_t *state = XXH64_createState ();

  XXH64_reset (state, 0);

  //would work better with realpath(), but maybe overkill
  //XXH64_update (state, wordlist, strlen (wordlist));

  XXH64_update (state, &st.st_size,  sizeof (st.st_size));
  XXH64_update (state, &st.st_mtime, sizeof (st.st_mtime));

  u8 *buf = (u8 *) hcmalloc (SAMPLE_SIZE);

  hc_fseek (&fp, 0, SEEK_SET);

  const size_t nread1 = hc_fread (buf, 1, SAMPLE_SIZE, &fp);

  XXH64_update (state, buf, nread1);

  const size_t file_size = (size_t) st.st_size;

  if (file_size > SAMPLE_SIZE)
  {
    hc_fseek (&fp, file_size - SAMPLE_SIZE, SEEK_SET);

    const size_t nread2 = hc_fread (buf, 1, SAMPLE_SIZE, &fp);

    XXH64_update (state, buf, nread2);
  }

  hcfree (buf);

  hc_fclose (&fp);

  u64 hash = XXH64_digest (state);

  XXH64_freeState (state);

  char *path = NULL;

  hc_asprintf (&path, "%s/%016" PRIx64 ".seekdb", seekdb_dir, hash);

  hcfree (seekdb_dir);

  ident[0] = hash;

  return path;
}

static bool seekdb_validate (const feed_seek_t *db, const u64 count, const u64 line_count, const u64 size)
{
  if ((db == NULL) || (count == 0)) return false;

  if ((db[0].line != 0) || (db[0].offset != 0)) return false;

  for (u64 i = 0; i < count; i++)
  {
    if (db[i].line > line_count) return false;
    if (db[i].offset > size) return false;

    if (i == 0) continue;

    if (db[i].line   < db[i - 1].line)   return false;
    if (db[i].offset < db[i - 1].offset) return false;
  }

  return true;
}

static bool seekdb_save (const char *path, const u64 line_count, const feed_seek_t *db, const u64 count, const u64 size)
{
  HCFILE fp;

  if (hc_fopen (&fp, path, "wb") == false) return false;

  bool ok = true;

  if (hc_fwrite (SEEKDB_MAGIC, 1, sizeof (SEEKDB_MAGIC), &fp) != sizeof (SEEKDB_MAGIC)) ok = false;
  if (ok && (hc_fwrite (&line_count, sizeof (u64), 1, &fp) != 1)) ok = false;
  if (ok && (hc_fwrite (&size,       sizeof (u64), 1, &fp) != 1)) ok = false;
  if (ok && (hc_fwrite (&count,      sizeof (u64), 1, &fp) != 1)) ok = false;
  if (ok && (hc_fwrite (db, sizeof (feed_seek_t), count, &fp) != count)) ok = false;

  hc_fclose (&fp);

  return ok;
}

static feed_seek_t *seekdb_load_new (HCFILE *fp, const struct stat *st, u64 *count, u64 *line_count, u64 *size)
{
  u64 entries = 0;

  if (hc_fread (line_count, sizeof (u64), 1, fp) != 1) return NULL;
  if (hc_fread (size,       sizeof (u64), 1, fp) != 1) return NULL;
  if (hc_fread (&entries,   sizeof (u64), 1, fp) != 1) return NULL;

  if ((entries == 0) || (entries > (SIZE_MAX / sizeof (feed_seek_t)))) return NULL;

  const u64 header_size = sizeof (SEEKDB_MAGIC) + (3 * sizeof (u64));
  const u64 entries_size = entries * sizeof (feed_seek_t);

  if ((u64) st->st_size != header_size + entries_size) return NULL;

  feed_seek_t *db = (feed_seek_t *) hcmalloc ((size_t) entries_size);

  if (db == NULL) return NULL;

  if (hc_fread (db, sizeof (feed_seek_t), entries, fp) != entries)
  {
    hcfree (db);

    return NULL;
  }

  if (seekdb_validate (db, entries, *line_count, *size) == false)
  {
    hcfree (db);

    return NULL;
  }

  *count = entries;

  return db;
}

static feed_seek_t *seekdb_load_legacy (HCFILE *fp, const struct stat *st, u64 *count, u64 *line_count, u64 *size)
{
  const u64 header_size = 2 * sizeof (u64);

  if ((u64) st->st_size < header_size) return NULL;
  if ((((u64) st->st_size - header_size) % sizeof (u64)) != 0) return NULL;

  hc_fseek (fp, 0, SEEK_SET);

  if (hc_fread (line_count, sizeof (u64), 1, fp) != 1) return NULL;
  if (hc_fread (size,       sizeof (u64), 1, fp) != 1) return NULL;

  const u64 entries = ((u64) st->st_size - header_size) / sizeof (u64);

  if ((entries == 0) || (entries > (SIZE_MAX / sizeof (feed_seek_t)))) return NULL;

  u64 *offsets = (u64 *) hcmalloc ((size_t) entries * sizeof (u64));

  if (offsets == NULL) return NULL;

  if (hc_fread (offsets, sizeof (u64), entries, fp) != entries)
  {
    hcfree (offsets);

    return NULL;
  }

  feed_seek_t *db = (feed_seek_t *) hcmalloc ((size_t) entries * sizeof (feed_seek_t));

  if (db == NULL)
  {
    hcfree (offsets);

    return NULL;
  }

  for (u64 i = 0; i < entries; i++)
  {
    db[i].line   = i * SEEKDB_LEGACY_STEP;
    db[i].offset = offsets[i];
  }

  hcfree (offsets);

  if (seekdb_validate (db, entries, *line_count, *size) == false)
  {
    hcfree (db);

    return NULL;
  }

  *count = entries;

  return db;
}

static feed_seek_t *seekdb_load (const char *path, u64 *count, u64 *line_count, u64 *size)
{
  HCFILE fp;

  if (hc_fopen (&fp, path, "rb") == false) return NULL;

  struct stat st;

  if (hc_fstat (&fp, &st) == -1)
  {
    hc_fclose (&fp);

    return NULL;
  }

  if (st.st_size < (ssize_t) (2 * sizeof (u64)))
  {
    hc_fclose (&fp);

    return NULL;
  }

  u8 magic[sizeof (SEEKDB_MAGIC)] = { 0 };

  if (hc_fread (magic, 1, sizeof (magic), &fp) != sizeof (magic))
  {
    hc_fclose (&fp);

    return NULL;
  }

  feed_seek_t *db = NULL;

  if (memcmp (magic, SEEKDB_MAGIC, sizeof (magic)) == 0)
  {
    db = seekdb_load_new (&fp, &st, count, line_count, size);
  }
  else
  {
    db = seekdb_load_legacy (&fp, &st, count, line_count, size);
  }

  hc_fclose (&fp);

  return db;
}

typedef struct seekdb_scan_shared
{
  const u8 *input;
  size_t    input_size;

  feed_seek_t *entries;

  hashcat_ctx_t *hashcat_ctx;
  const char    *wordlist;
  hc_timer_t     start;

  u64 bytes_done;
  u64 lines_done;
  u64 report_next;
  u64 report_step;

} seekdb_scan_shared_t;

typedef struct seekdb_scan_thread_param
{
  seekdb_scan_shared_t *shared;

  u64 first_entry;
  u64 last_entry;

  size_t from;
  size_t to;

  u64 newlines;

} seekdb_scan_thread_param_t;

static void seekdb_scan_progress (seekdb_scan_shared_t *shared, const u64 bytes, const u64 lines)
{
  const u64 done = __atomic_add_fetch (&shared->bytes_done, bytes, __ATOMIC_RELAXED);

  __atomic_add_fetch (&shared->lines_done, lines, __ATOMIC_RELAXED);

  u64 report = __atomic_load_n (&shared->report_next, __ATOMIC_RELAXED);

  while ((done >= report) && (report < shared->input_size))
  {
    const u64 next = report + shared->report_step;

    if (__atomic_compare_exchange_n (&shared->report_next, &report, next, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
    {
      const u64 lines_done = __atomic_load_n (&shared->lines_done, __ATOMIC_RELAXED);

      cache_generate_t cache_generate;

      cache_generate.dictfile = shared->wordlist;
      cache_generate.comp     = MIN (done, shared->input_size);
      cache_generate.percent  = ((double) MIN (done, shared->input_size) / (double) shared->input_size) * 100;
      cache_generate.cnt      = lines_done;
      cache_generate.cnt2     = lines_done;
      cache_generate.runtime  = hc_timer_get (shared->start);

      if (cache_generate.percent < 100)
      {
        event_call (EVENT_WORDLIST_CACHE_GENERATE, shared->hashcat_ctx, &cache_generate, sizeof (cache_generate));
      }

      break;
    }
  }
}

#if defined (_WIN)
static HC_API_CALL DWORD seekdb_scan_thread (void *p)
#else
static HC_API_CALL void *seekdb_scan_thread (void *p)
#endif
{
  seekdb_scan_thread_param_t *param = (seekdb_scan_thread_param_t *) p;
  seekdb_scan_shared_t       *shared = param->shared;

  const u8 *input = shared->input;

  hc_memchr_t hc_memchr = hc_memchr_get ();
  hc_memcount_t hc_memcount = hc_memcount_get ();

  u64 entry_pos = param->first_entry;
  u64 lines = 0;

  size_t pos = param->from;

  // Count whole byte ranges with one SIMD pass. Calling memchr once for every short line made a
  // four-billion-line email list a four-billion-call loop; only the sparse checkpoint boundaries
  // need the position of a particular newline.

  while (entry_pos < param->last_entry)
  {
    const size_t boundary = (size_t) entry_pos * SEEKDB_BYTE_STEP;

    if (boundary > pos)
    {
      const size_t bytes = boundary - pos;
      const u64 found = hc_memcount (input + pos, '\n', bytes);

      lines += found;

      seekdb_scan_progress (shared, bytes, found);

      pos = boundary;
    }

    if (boundary == 0)
    {
      shared->entries[entry_pos].line   = 0;
      shared->entries[entry_pos].offset = 0;
    }
    else if (input[boundary - 1] == '\n')
    {
      shared->entries[entry_pos].line   = lines;
      shared->entries[entry_pos].offset = boundary;
    }
    else
    {
      const size_t left = shared->input_size - boundary;
      const size_t step = hc_memchr (input + boundary, '\n', left);

      if (step < left)
      {
        shared->entries[entry_pos].line   = lines + 1;
        shared->entries[entry_pos].offset = boundary + step + 1;
      }
    }

    entry_pos++;
  }

  if (pos < param->to)
  {
    const size_t bytes = param->to - pos;
    const u64 found = hc_memcount (input + pos, '\n', bytes);

    lines += found;

    seekdb_scan_progress (shared, bytes, found);
  }

  param->newlines = lines;

  return 0;
}

static void seekdb_scan_run_threads (hc_thread_t *threads, seekdb_scan_thread_param_t *params, const u32 threads_cnt)
{
  u32 threads_created = 0;

  for (u32 thread_pos = 0; thread_pos < threads_cnt; thread_pos++)
  {
    hc_thread_t thread;

    #if defined (_WIN)
    hc_thread_create (thread, seekdb_scan_thread, &params[thread_pos]);

    if (thread == NULL)
    #else
    if (hc_thread_create (thread, seekdb_scan_thread, &params[thread_pos]) != 0)
    #endif
    {
      seekdb_scan_thread (&params[thread_pos]);
    }
    else
    {
      threads[threads_created++] = thread;
    }
  }

  hc_thread_wait (threads_created, threads);

  #if defined (_WIN)
  for (u32 thread_pos = 0; thread_pos < threads_created; thread_pos++)
  {
    CloseHandle (threads[thread_pos]);
  }
  #endif
}

static u32 seekdb_scan_threads (const size_t input_size, const u64 entries)
{
  u32 threads = (u32) MAX (hc_get_processor_count (), 1);

  threads = MIN (threads, SEEKDB_THREADS_MAX);

  const size_t by_size = 1 + ((input_size - 1) / SEEKDB_BYTES_PER_THREAD);

  threads = MIN (threads, (u32) MIN (by_size, SEEKDB_THREADS_MAX));
  threads = MIN (threads, (u32) MIN (entries, SEEKDB_THREADS_MAX));

  const char *value = getenv ("HASHCAT_SEEKDB_THREADS");

  if ((value != NULL) && (value[0] != 0))
  {
    char *end = NULL;

    const unsigned long parsed = strtoul (value, &end, 10);

    if ((end != value) && (*end == 0) && (parsed >= 1) && (parsed <= SEEKDB_THREADS_MAX))
    {
      threads = MIN ((u32) parsed, (u32) MIN (entries, SEEKDB_THREADS_MAX));
    }
  }

  return MAX (threads, 1);
}

static feed_seek_t *seekdb_build (feed_thread_t *feed_thread, const char *path, const char *wordlist, u64 *count, u64 *line_count, u64 *size, hashcat_ctx_t *hashcat_ctx)
{
  const u8 *input = feed_thread->fd_mem;
  const size_t input_size = feed_thread->fd_len;

  const u64 entries = 1 + ((input_size - 1) / SEEKDB_BYTE_STEP);

  feed_seek_t *tmp = (feed_seek_t *) hcmalloc ((size_t) entries * sizeof (feed_seek_t));

  if (tmp == NULL) return NULL;

  for (u64 i = 0; i < entries; i++)
  {
    tmp[i].line   = UINT64_MAX;
    tmp[i].offset = UINT64_MAX;
  }

  seekdb_scan_shared_t shared;

  memset (&shared, 0, sizeof (shared));

  shared.input       = input;
  shared.input_size  = input_size;
  shared.entries     = tmp;
  shared.hashcat_ctx = hashcat_ctx;
  shared.wordlist    = wordlist;
  shared.report_step = MAX ((u64) input_size / 80, 1);
  shared.report_next = shared.report_step;

  hc_timer_set (&shared.start);

  const u32 threads_cnt = seekdb_scan_threads (input_size, entries);

  hc_thread_t *threads = (hc_thread_t *) hccalloc (threads_cnt, sizeof (hc_thread_t));
  seekdb_scan_thread_param_t *params = (seekdb_scan_thread_param_t *) hccalloc (threads_cnt, sizeof (seekdb_scan_thread_param_t));

  if ((threads == NULL) || (params == NULL))
  {
    hcfree (params);
    hcfree (threads);
    hcfree (tmp);

    return NULL;
  }

  for (u32 thread_pos = 0; thread_pos < threads_cnt; thread_pos++)
  {
    seekdb_scan_thread_param_t *param = &params[thread_pos];

    param->shared      = &shared;
    param->first_entry = (entries * thread_pos) / threads_cnt;
    param->last_entry  = (entries * (thread_pos + 1)) / threads_cnt;
    param->from        = (size_t) param->first_entry * SEEKDB_BYTE_STEP;
    param->to          = (thread_pos + 1 < threads_cnt) ? (size_t) param->last_entry * SEEKDB_BYTE_STEP : input_size;
  }

  seekdb_scan_run_threads (threads, params, threads_cnt);

  u64 total_newlines = 0;

  for (u32 thread_pos = 0; thread_pos < threads_cnt; thread_pos++)
  {
    seekdb_scan_thread_param_t *param = &params[thread_pos];

    for (u64 i = param->first_entry; i < param->last_entry; i++)
    {
      if (tmp[i].line != UINT64_MAX) tmp[i].line += total_newlines;
    }

    total_newlines += param->newlines;
  }

  const u64 lines = total_newlines + ((input[input_size - 1] == '\n') ? 0 : 1);

  u64 valid = 0;

  for (u64 i = 0; i < entries; i++)
  {
    if (tmp[i].line == UINT64_MAX) continue;
    if (tmp[i].line > lines) continue;
    if (tmp[i].offset > input_size) continue;

    if ((valid > 0)
     && (tmp[i].line   == tmp[valid - 1].line)
     && (tmp[i].offset == tmp[valid - 1].offset)) continue;

    tmp[valid++] = tmp[i];
  }

  feed_seek_t *db = (feed_seek_t *) hcmalloc ((size_t) valid * sizeof (feed_seek_t));

  if (db != NULL) memcpy (db, tmp, (size_t) valid * sizeof (feed_seek_t));

  hcfree (params);
  hcfree (threads);
  hcfree (tmp);

  if ((db == NULL) || (seekdb_validate (db, valid, lines, input_size) == false))
  {
    hcfree (db);

    return NULL;
  }

  *count      = valid;
  *line_count = lines;
  *size       = input_size;

  seekdb_save (path, *line_count, db, *count, *size);

  return db;
}

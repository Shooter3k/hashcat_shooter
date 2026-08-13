#include "hashpipe_engine.h"

static int hp_initialized;

int hp_engine_init(void)
{
    if (hp_initialized) return 0;

    init_hashtypes();

    hp_initialized = 1;

    return 0;
}

int hp_engine_has_handler(const int type_idx)
{
    const struct hashtype *ht;

    if (!hp_initialized) hp_engine_init();
    if (type_idx < 1 || type_idx >= Numtypes) return 0;

    ht = &Hashtypes[type_idx];

    return ht->verify != NULL || ht->compute != NULL || ht->nchain > 0;
}

void *hp_engine_workspace_create(void)
{
    struct workspace *ws = (struct workspace *) calloc(1, sizeof(*ws));

    if (ws == NULL) return NULL;

    ws->testvec = (unsigned char *) malloc(TESTVECSIZE + 16);
    if (ws->testvec == NULL)
    {
        free(ws);
        return NULL;
    }

    ws_init_rhash(ws);

    return ws;
}

void hp_engine_workspace_destroy(void *workspace)
{
    struct workspace *ws = (struct workspace *) workspace;

    if (ws == NULL) return;

    ws_free_rhash(ws);
    free(ws->testvec);
    free(ws);
}

static void hp_hex_lower(const unsigned char *src, char *dst, const int len)
{
    static const char hex[] = "0123456789abcdef";
    int i;

    for (i = 0; i < len; i++)
    {
        dst[i * 2 + 0] = hex[src[i] >> 4];
        dst[i * 2 + 1] = hex[src[i] & 15];
    }

    dst[len * 2] = 0;
}

static void hp_hex_upper(const unsigned char *src, char *dst, const int len)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;

    for (i = 0; i < len; i++)
    {
        dst[i * 2 + 0] = hex[src[i] >> 4];
        dst[i * 2 + 1] = hex[src[i] & 15];
    }

    dst[len * 2] = 0;
}

static int hp_hex_value(const unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;

    return -1;
}

static int hp_decode_hex_literal(const unsigned char *src, const int src_len,
    unsigned char *dst, const int dst_size)
{
    int hex_len;
    int i;

    if (src_len < 6) return -1;
    if (memcmp(src, "$HEX[", 5) != 0 || src[src_len - 1] != ']') return -1;

    hex_len = src_len - 6;

    if ((hex_len & 1) != 0 || hex_len / 2 > dst_size) return -1;

    for (i = 0; i < hex_len; i += 2)
    {
        const int hi = hp_hex_value(src[5 + i]);
        const int lo = hp_hex_value(src[5 + i + 1]);

        if (hi < 0 || lo < 0) return -1;

        dst[i / 2] = (unsigned char) ((hi << 4) | lo);
    }

    return hex_len / 2;
}

static void hp_run_chain(const struct hashtype *ht,
    const unsigned char *pass, const int passlen, unsigned char *dest)
{
    unsigned char buf[MAX_HASH_BYTES];
    char hex[MAX_HASH_BYTES * 2 + 1];
    int i;

    ht->chain[0].fn(pass, passlen, NULL, 0, buf);

    for (i = 1; i < ht->nchain; i++)
    {
        if (ht->chain[i - 1].uc_hex == 2)
        {
            ht->chain[i].fn(buf, ht->chain[i - 1].outbytes,
                NULL, 0, buf);
        }
        else
        {
            if (ht->chain[i - 1].uc_hex == 1)
                hp_hex_upper(buf, hex, ht->chain[i - 1].outbytes);
            else
                hp_hex_lower(buf, hex, ht->chain[i - 1].outbytes);

            ht->chain[i].fn((const unsigned char *) hex,
                ht->chain[i - 1].outbytes * 2, NULL, 0, buf);
        }
    }

    memcpy(dest, buf, ht->chain[ht->nchain - 1].outbytes);
}

static int hp_compare_compute(const struct hashtype *ht,
    hashfn_t compute,
    const char *expected, const int expected_len,
    const unsigned char *pass, const int passlen,
    const unsigned char *salt, const int saltlen)
{
    unsigned char dest[MAX_HASH_BYTES];
    char hexout[MAX_HASH_BYTES * 2 + 1];

    if (ht->hashlen <= 0 || ht->hashlen > MAX_HASH_BYTES) return 0;
    if (expected_len != ht->hashlen * 2) return 0;

    memset(dest, 0, sizeof(dest));

    if (compute != NULL)
        compute(pass, passlen, salt, saltlen, dest);
    else
        hp_run_chain(ht, pass, passlen, dest);

    hp_hex_lower(dest, hexout, ht->hashlen);

    return strncasecmp(hexout, expected, expected_len) == 0;
}

int hp_engine_verify(void *workspace, const int type_idx,
    const char *target, const int target_len,
    const unsigned char *pass, const int passlen)
{
    struct workspace *ws = (struct workspace *) workspace;
    const struct hashtype *ht;
    struct hashtype compatibility_ht;
    const char *colon;
    const unsigned char *salt = NULL;
    unsigned char decoded_salt[1024];
    int expected_len = target_len;
    int saltlen = 0;

    if (!hp_initialized) hp_engine_init();
    if (ws == NULL || target == NULL || pass == NULL) return 0;
    if (type_idx < 1 || type_idx >= Numtypes) return 0;

    ht = &Hashtypes[type_idx];
    WS = ws;

    if (ht->verify != NULL)
        return ht->verify(target, target_len, pass, passlen);

    colon = (const char *) memchr(target, ':', target_len);
    if (colon != NULL)
    {
        expected_len = (int) (colon - target);
        salt = (const unsigned char *) colon + 1;
        saltlen = target_len - expected_len - 1;

        {
            const int decoded_len = hp_decode_hex_literal(salt, saltlen,
                decoded_salt, (int) sizeof(decoded_salt));

            if (decoded_len >= 0)
            {
                salt = decoded_salt;
                saltlen = decoded_len;
            }
        }
    }

    /* mdxfind's published e539 vector stores the first 16 bytes of the
     * 20-byte MYSQL5MD5 result.  Continue accepting the full historical
     * value as well. */
    if (type_idx == 539 && expected_len == 32)
    {
        compatibility_ht = *ht;
        compatibility_ht.hashlen = 16;
        ht = &compatibility_ht;
    }

    if (hp_compare_compute(ht, ht->compute, target, expected_len,
        pass, passlen, salt, saltlen)) return 1;

    if (ht->compute_alt != NULL && hp_compare_compute(ht, ht->compute_alt,
        target, expected_len, pass, passlen, salt, saltlen)) return 1;

    if (ht->compute_alt2 != NULL && hp_compare_compute(ht, ht->compute_alt2,
        target, expected_len, pass, passlen, salt, saltlen)) return 1;

    return 0;
}

/**
 * Coverage-guided fuzzing for the two parsers that accept the most
 * user-controlled bytes: module token layouts and rule syntax.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "common.h"
#include "types.h"
#include "parser.h"
#include "rp.h"

static void fuzz_tokenizer (const u8 *data, const size_t size)
{
  if (size == 0) return;

  hc_token_t token;

  memset (&token, 0, sizeof (token));

  token.token_cnt      = 1 + (data[0] % MAX_TOKENS);
  token.signatures_cnt = 1;
  token.signatures_buf[0] = "fuzz";

  static const int separators[] = { 0, ':', '$', '*', '.' };
  static const int validators[] =
  {
    0,
    TOKEN_ATTR_VERIFY_DIGIT,
    TOKEN_ATTR_VERIFY_FLOAT,
    TOKEN_ATTR_VERIFY_HEX,
    TOKEN_ATTR_VERIFY_BASE64A,
    TOKEN_ATTR_VERIFY_BASE64B,
    TOKEN_ATTR_VERIFY_BASE64C,
    TOKEN_ATTR_VERIFY_BASE58,
    TOKEN_ATTR_VERIFY_BECH32
  };

  for (int i = 0; i < token.token_cnt; i++)
  {
    const u8 config = data[(1 + i) % size];

    token.sep[i] = (i + 1 < token.token_cnt)
                 ? separators[config % (sizeof (separators) / sizeof (separators[0]))]
                 : 0;

    token.len[i]     = config & 31;
    token.len_min[i] = (config >> 2) & 7;
    token.len_max[i] = token.len_min[i] + ((config >> 5) & 7);
    token.attr[i]    = validators[(config >> 1) % (sizeof (validators) / sizeof (validators[0]))];

    if (config & 0x40) token.attr[i] |= TOKEN_ATTR_VERIFY_LENGTH;
    if (config & 0x80) token.attr[i] |= TOKEN_ATTR_FIXED_LENGTH;
  }

  const size_t config_size = MIN (size, (size_t) (1 + token.token_cnt));
  const u8 *input = data + config_size;
  const int input_len = (int) MIN (size - config_size, (size_t) INT_MAX);

  parser_error_reset ();

  (void) input_tokenizer (input, input_len, &token);
}

static void fuzz_rule_parser (const u8 *data, const size_t size)
{
  char rule_buf[4096];

  const u32 rule_len = (u32) MIN (size, sizeof (rule_buf));

  memcpy (rule_buf, data, rule_len);

  kernel_rule_t rule;

  memset (&rule, 0, sizeof (rule));

  (void) cpu_rule_to_kernel_rule (rule_buf, rule_len, &rule);
}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  fuzz_tokenizer ((const u8 *) data, size);
  fuzz_rule_parser ((const u8 *) data, size);

  return 0;
}

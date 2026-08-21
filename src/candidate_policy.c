/**
 * Author......: See docs/credits.txt
 * License.....: MIT
 */

#include "common.h"
#include "types.h"
#include "candidate_policy.h"

bool candidate_policy_active (const user_options_t *user_options)
{
  return (user_options->candidate_min_upper  > 0)
      || (user_options->candidate_min_lower  > 0)
      || (user_options->candidate_min_digit  > 0)
      || (user_options->candidate_min_symbol > 0);
}

bool candidate_policy_accept (const user_options_t *user_options, const u8 *candidate, const u32 candidate_len)
{
  if (candidate_policy_active (user_options) == false) return true;

  u32 upper  = 0;
  u32 lower  = 0;
  u32 digit  = 0;
  u32 symbol = 0;

  for (u32 i = 0; i < candidate_len; i++)
  {
    const u8 c = candidate[i];

    if      ((c >= 'A') && (c <= 'Z')) upper++;
    else if ((c >= 'a') && (c <= 'z')) lower++;
    else if ((c >= '0') && (c <= '9')) digit++;
    else                                symbol++;
  }

  return (upper  >= user_options->candidate_min_upper)
      && (lower  >= user_options->candidate_min_lower)
      && (digit  >= user_options->candidate_min_digit)
      && (symbol >= user_options->candidate_min_symbol);
}

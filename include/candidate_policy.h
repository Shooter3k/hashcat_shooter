/**
 * Author......: See docs/credits.txt
 * License.....: MIT
 */

#ifndef HC_CANDIDATE_POLICY_H
#define HC_CANDIDATE_POLICY_H

bool candidate_policy_active (const user_options_t *user_options);
bool candidate_policy_accept (const user_options_t *user_options, const u8 *candidate, const u32 candidate_len);

#endif // HC_CANDIDATE_POLICY_H

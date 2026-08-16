/**
 * Author......: See docs/credits.txt
 * License.....: MIT
 */

#include "common.h"
#include "types.h"
#include "memory.h"
#include "convert.h"
#include "event.h"
#include "shared.h"
#include "system.h"
#include "memchr.h"
#include "filehandling.h"
#include "rp.h"
#include "rp_cpu.h"
#include "thread.h"

static const char grp_op_nop[] =
{
  RULE_OP_MANGLE_LREST,
  RULE_OP_MANGLE_UREST,
  RULE_OP_MANGLE_LREST_UFIRST,
  RULE_OP_MANGLE_UREST_LFIRST,
  RULE_OP_MANGLE_TREST,
  RULE_OP_MANGLE_SHIFT_CASE,
  RULE_OP_MANGLE_REVERSE,
  RULE_OP_MANGLE_DUPEWORD,
  RULE_OP_MANGLE_REFLECT,
  RULE_OP_MANGLE_DELETE_FIRST,
  RULE_OP_MANGLE_DELETE_LAST,
  RULE_OP_MANGLE_ROTATE_LEFT,
  RULE_OP_MANGLE_ROTATE_RIGHT,
  RULE_OP_MANGLE_SWITCH_FIRST,
  RULE_OP_MANGLE_SWITCH_LAST,
  RULE_OP_MANGLE_DUPECHAR_ALL,
  RULE_OP_MANGLE_TITLE,
  RULE_OP_MANGLE_TO_HEX_LOWER,
  RULE_OP_MANGLE_TO_HEX_UPPER
};

static const char grp_op_pos_p0[] =
{
  RULE_OP_MANGLE_TOGGLE_AT,
  RULE_OP_MANGLE_DELETE_AT,
  RULE_OP_MANGLE_TRUNCATE_AT,
  RULE_OP_MANGLE_CHR_INCR,
  RULE_OP_MANGLE_CHR_DECR,
  RULE_OP_MANGLE_CHR_SHIFTL,
  RULE_OP_MANGLE_CHR_SHIFTR,
  RULE_OP_MANGLE_REPLACE_NP1,
  RULE_OP_MANGLE_REPLACE_NM1
};

static const char grp_op_pos_p1[] =
{
  RULE_OP_MANGLE_DUPEWORD_TIMES,
  RULE_OP_MANGLE_DUPECHAR_FIRST,
  RULE_OP_MANGLE_DUPECHAR_LAST,
  RULE_OP_MANGLE_DUPEBLOCK_FIRST,
  RULE_OP_MANGLE_DUPEBLOCK_LAST
};

static const char grp_op_chr[] =
{
  RULE_OP_MANGLE_APPEND,
  RULE_OP_MANGLE_PREPEND,
  RULE_OP_MANGLE_PURGECHAR,
  RULE_OP_MANGLE_TITLE_SEP
};

static const char grp_op_chr_chr[] =
{
  RULE_OP_MANGLE_REPLACE
};

static const char grp_op_pos_chr[] =
{
  RULE_OP_MANGLE_INSERT,
  RULE_OP_MANGLE_INSERT_EVERY,
  RULE_OP_MANGLE_OVERSTRIKE,
  RULE_OP_MANGLE_TOGGLE_AT_SEP,
  RULE_OP_MANGLE_CHR_ADD
};

static const char grp_op_pos_pos0[] =
{
  RULE_OP_MANGLE_SWITCH_AT
};

static const char grp_op_pos_pos1[] =
{
  RULE_OP_MANGLE_EXTRACT,
  RULE_OP_MANGLE_OMIT
};

static const char grp_pos[] =
{
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B'
};

bool class_num (const u8 c)
{
  return ((c >= '0') && (c <= '9'));
}

bool class_lower (const u8 c)
{
  return ((c >= 'a') && (c <= 'z'));
}

bool class_upper (const u8 c)
{
  return ((c >= 'A') && (c <= 'Z'));
}

bool class_lower_hex (const u8 c)
{
  return ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f'));
}

bool class_upper_hex (const u8 c)
{
  return ((c >= '0') && (c <= '9')) || ((c >= 'A') && (c <= 'F'));
}

bool class_sym (const u8 c)
{
  return ((c == ' ') || ((c >= '!') && (c <= '/')) || ((c >= ':') && (c <= '@')) || ((c >= '[') && (c <= '`')) || ((c >= '{') && (c <= '~')));
}

bool class_alpha (const u8 c)
{
  return (class_lower (c) || class_upper (c));
}

int conv_ctoi (const u8 c)
{
  if (class_num (c)) return c - '0';
  if (class_upper (c)) return c - 'A' + 10;

  return -1;
}

int conv_itoc (const u8 c)
{
  if (c < 10) return c + '0';
  if (c < 37) return c + 'A' - 10;

  return -1;
}

int generate_random_rule (char rule_buf[RP_RULE_SIZE], const u32 rp_gen_func_min, const u32 rp_gen_func_max, const rp_gen_ops_t *rp_gen_ops)
{
  // generate them

  const u32 rp_gen_num = get_random_num (rp_gen_func_min, rp_gen_func_max);

  u32 rule_pos = 0;

  for (u32 j = 0; j < rp_gen_num; j++)
  {
    u32 r  = 0;
    u32 p1 = 0;
    u32 p2 = 0;

    const int group_num = get_random_num (0, rp_gen_ops->grp_op_alias_cnt);

    const int group_num_alias = rp_gen_ops->grp_op_alias_buf[group_num];

    switch (group_num_alias)
    {
      case 0:
        r = get_random_num (0, rp_gen_ops->grp_op_nop_cnt);
        rule_buf[rule_pos++] = rp_gen_ops->grp_op_nop_selection[r];
        break;

      case 1:
        r = get_random_num (0, rp_gen_ops->grp_op_pos_p0_cnt);
        rule_buf[rule_pos++] = rp_gen_ops->grp_op_pos_p0_selection[r];
        p1 = get_random_num (0, sizeof (grp_pos));
        rule_buf[rule_pos++] = grp_pos[p1];
        break;

      case 2:
        r = get_random_num (0, rp_gen_ops->grp_op_pos_p1_cnt);
        rule_buf[rule_pos++] = rp_gen_ops->grp_op_pos_p1_selection[r];
        p1 = get_random_num (1, 6);
        rule_buf[rule_pos++] = grp_pos[p1];
        break;

      case 3:
        r = get_random_num (0, rp_gen_ops->grp_op_chr_cnt);
        rule_buf[rule_pos++] = rp_gen_ops->grp_op_chr_selection[r];
        p1 = get_random_num (0x20, 0x7e);
        rule_buf[rule_pos++] = (char) p1;
        break;

      case 4:
        r = get_random_num (0, rp_gen_ops->grp_op_chr_chr_cnt);
        rule_buf[rule_pos++] = rp_gen_ops->grp_op_chr_chr_selection[r];
        p1 = get_random_num (0x20, 0x7e);
        rule_buf[rule_pos++] = (char) p1;
        p2 = get_random_num (0x20, 0x7e);
        while (p1 == p2)
        p2 = get_random_num (0x20, 0x7e);
        rule_buf[rule_pos++] = (char) p2;
        break;

      case 5:
        r = get_random_num (0, rp_gen_ops->grp_op_pos_chr_cnt);
        rule_buf[rule_pos++] = rp_gen_ops->grp_op_pos_chr_selection[r];
        p1 = get_random_num (0, sizeof (grp_pos));
        rule_buf[rule_pos++] = grp_pos[p1];
        p2 = get_random_num (0x20, 0x7e);
        rule_buf[rule_pos++] = (char) p2;
        break;

      case 6:
        r = get_random_num (0, rp_gen_ops->grp_op_pos_pos0_cnt);
        rule_buf[rule_pos++] = rp_gen_ops->grp_op_pos_pos0_selection[r];
        p1 = get_random_num (0, sizeof (grp_pos));
        rule_buf[rule_pos++] = grp_pos[p1];
        p2 = get_random_num (0, sizeof (grp_pos));
        while (p1 == p2)
        p2 = get_random_num (0, sizeof (grp_pos));
        rule_buf[rule_pos++] = grp_pos[p2];
        break;

      case 7:
        r = get_random_num (0, rp_gen_ops->grp_op_pos_pos1_cnt);
        rule_buf[rule_pos++] = rp_gen_ops->grp_op_pos_pos1_selection[r];
        p1 = get_random_num (0, sizeof (grp_pos));
        rule_buf[rule_pos++] = grp_pos[p1];
        p2 = get_random_num (1, sizeof (grp_pos));
        while (p1 == p2)
        p2 = get_random_num (1, sizeof (grp_pos));
        rule_buf[rule_pos++] = grp_pos[p2];
        break;
    }
  }

  return rule_pos;
}

#define INCR_POS if (++rule_pos == rule_len) return (-1)

#define SET_NAME(rule,val) (rule)->cmds[rule_cnt]  = ((val) & 0xff) <<  0
#define SET_P0(rule,val)   do { INCR_POS; if (is_hex_notation (rule_buf, rule_len, rule_pos) == true) { (rule)->cmds[rule_cnt] |= (hex_convert (rule_buf[rule_pos + 3] & 0xff) <<  8) | (hex_convert (rule_buf[rule_pos + 2] & 0xff) << 12); rule_pos += 3; } else { (rule)->cmds[rule_cnt] |= ((val) & 0xff) <<  8; } } while(0)
#define SET_P1(rule,val)   do { INCR_POS; if (is_hex_notation (rule_buf, rule_len, rule_pos) == true) { (rule)->cmds[rule_cnt] |= (hex_convert (rule_buf[rule_pos + 3] & 0xff) << 16) | (hex_convert (rule_buf[rule_pos + 2] & 0xff) << 20); rule_pos += 3; } else { (rule)->cmds[rule_cnt] |= ((val) & 0xff) <<  16; } } while(0)
#define GET_NAME(rule)     rule_cmd = (((rule)->cmds[rule_cnt] >>  0) & 0xff)
#define GET_P0(rule)       INCR_POS; rule_buf[rule_pos] = (((rule)->cmds[rule_cnt] >>  8) & 0xff)
#define GET_P1(rule)       INCR_POS; rule_buf[rule_pos] = (((rule)->cmds[rule_cnt] >> 16) & 0xff)

#define SET_P0_CONV(rule,val)  INCR_POS; (rule)->cmds[rule_cnt] |= ((conv_ctoi (val)) & 0xff) <<  8
#define SET_P1_CONV(rule,val)  INCR_POS; (rule)->cmds[rule_cnt] |= ((conv_ctoi (val)) & 0xff) << 16
#define GET_P0_CONV(rule)      INCR_POS; rule_buf[rule_pos] = (char) conv_itoc (((rule)->cmds[rule_cnt] >>  8) & 0xff)
#define GET_P1_CONV(rule)      INCR_POS; rule_buf[rule_pos] = (char) conv_itoc (((rule)->cmds[rule_cnt] >> 16) & 0xff)

bool is_hex_notation (const char *rule_buf, u32 rule_len, u32 rule_pos)
{
  if ((rule_pos + 4) > rule_len) return false;

  if (rule_buf[rule_pos + 0] != '\\') return false;
  if (rule_buf[rule_pos + 1] != 'x')  return false;

  if (is_valid_hex_char (rule_buf[rule_pos + 2]) == false) return false;
  if (is_valid_hex_char (rule_buf[rule_pos + 3]) == false) return false;

  return true;
}

u32 rule_utf8_char_len (const u8 *rule_buf, const u32 rule_len, const u32 rule_pos)
{
  if (rule_pos >= rule_len) return 0;

  const u8 c0 = rule_buf[rule_pos];

  if (c0 < 0x80) return 1;

  if ((c0 >= 0xc2) && (c0 <= 0xdf))
  {
    if ((rule_pos + 2) > rule_len) return 1;

    const u8 c1 = rule_buf[rule_pos + 1];

    if ((c1 >= 0x80) && (c1 <= 0xbf)) return 2;

    return 1;
  }

  if ((c0 >= 0xe0) && (c0 <= 0xef))
  {
    if ((rule_pos + 3) > rule_len) return 1;

    const u8 c1 = rule_buf[rule_pos + 1];
    const u8 c2 = rule_buf[rule_pos + 2];

    if ((c2 < 0x80) || (c2 > 0xbf)) return 1;

    if (c0 == 0xe0) return ((c1 >= 0xa0) && (c1 <= 0xbf)) ? 3 : 1;
    if (c0 == 0xed) return ((c1 >= 0x80) && (c1 <= 0x9f)) ? 3 : 1;

    return ((c1 >= 0x80) && (c1 <= 0xbf)) ? 3 : 1;
  }

  if ((c0 >= 0xf0) && (c0 <= 0xf4))
  {
    if ((rule_pos + 4) > rule_len) return 1;

    const u8 c1 = rule_buf[rule_pos + 1];
    const u8 c2 = rule_buf[rule_pos + 2];
    const u8 c3 = rule_buf[rule_pos + 3];

    if ((c2 < 0x80) || (c2 > 0xbf)) return 1;
    if ((c3 < 0x80) || (c3 > 0xbf)) return 1;

    if (c0 == 0xf0) return ((c1 >= 0x90) && (c1 <= 0xbf)) ? 4 : 1;
    if (c0 == 0xf4) return ((c1 >= 0x80) && (c1 <= 0x8f)) ? 4 : 1;

    return ((c1 >= 0x80) && (c1 <= 0xbf)) ? 4 : 1;
  }

  return 1;
}

int cpu_rule_to_kernel_rule (char *rule_buf, u32 rule_len, kernel_rule_t *rule)
{
  u32 rule_pos;
  u32 rule_cnt;

  for (rule_pos = 0, rule_cnt = 0; rule_pos < rule_len && rule_cnt < MAX_KERNEL_RULES; rule_pos++, rule_cnt++)
  {
    switch (rule_buf[rule_pos])
    {
      case ' ':
        rule_cnt--;
        break;

      case RULE_OP_MANGLE_NOOP:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_LREST:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_UREST:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_LREST_UFIRST:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_UREST_LFIRST:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_TREST:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_SHIFT_CASE:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_TOGGLE_AT:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_REVERSE:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_DUPEWORD:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_DUPEWORD_TIMES:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_REFLECT:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_ROTATE_LEFT:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_ROTATE_RIGHT:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_APPEND:
      {
        const u32 utf8_len = rule_utf8_char_len ((const u8 *) rule_buf, rule_len, rule_pos + 1);

        if (utf8_len > 1)
        {
          if ((rule_cnt + utf8_len) > MAX_KERNEL_RULES) return -1;

          for (u32 j = 0; j < utf8_len; j++)
          {
            rule->cmds[rule_cnt + j] = RULE_OP_MANGLE_APPEND | ((rule_buf[rule_pos + 1 + j] & 0xff) << 8);
          }

          rule_pos += utf8_len;
          rule_cnt += utf8_len - 1;
        }
        else
        {
          SET_NAME (rule, rule_buf[rule_pos]);
          SET_P0   (rule, rule_buf[rule_pos]);
        }
        break;
      }

      case RULE_OP_MANGLE_PREPEND:
      {
        const u32 utf8_len = rule_utf8_char_len ((const u8 *) rule_buf, rule_len, rule_pos + 1);

        if (utf8_len > 1)
        {
          if ((rule_cnt + utf8_len) > MAX_KERNEL_RULES) return -1;

          for (u32 j = 0; j < utf8_len; j++)
          {
            rule->cmds[rule_cnt + j] = RULE_OP_MANGLE_PREPEND | ((rule_buf[rule_pos + utf8_len - j] & 0xff) << 8);
          }

          rule_pos += utf8_len;
          rule_cnt += utf8_len - 1;
        }
        else
        {
          SET_NAME (rule, rule_buf[rule_pos]);
          SET_P0   (rule, rule_buf[rule_pos]);
        }
        break;
      }

      case RULE_OP_MANGLE_DELETE_FIRST:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_DELETE_LAST:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_DELETE_AT:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_EXTRACT:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        SET_P1_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_OMIT:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        SET_P1_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_INSERT:
      {
        if ((rule_pos + 1) >= rule_len) return -1;

        const int insert_pos = conv_ctoi (rule_buf[rule_pos + 1]);

        if (insert_pos < 0) return -1;

        const u32 utf8_len = rule_utf8_char_len ((const u8 *) rule_buf, rule_len, rule_pos + 2);

        if (utf8_len > 1)
        {
          if ((rule_cnt + utf8_len) > MAX_KERNEL_RULES) return -1;

          for (u32 j = 0; j < utf8_len; j++)
          {
            rule->cmds[rule_cnt + j] = RULE_OP_MANGLE_INSERT
                                     | ((insert_pos & 0xff) << 8)
                                     | ((rule_buf[rule_pos + 1 + utf8_len - j] & 0xff) << 16);
          }

          rule_pos += utf8_len + 1;
          rule_cnt += utf8_len - 1;
        }
        else
        {
          SET_NAME    (rule, rule_buf[rule_pos]);
          SET_P0_CONV (rule, rule_buf[rule_pos]);
          SET_P1      (rule, rule_buf[rule_pos]);
        }
        break;
      }

      case RULE_OP_MANGLE_INSERT_EVERY:
      {
        if ((rule_pos + 1) >= rule_len) return -1;

        const int insert_pos = conv_ctoi (rule_buf[rule_pos + 1]);

        if (insert_pos < 0) return -1;

        const u32 utf8_len = rule_utf8_char_len ((const u8 *) rule_buf, rule_len, rule_pos + 2);

        if (utf8_len > 1)
        {
          if ((rule_cnt + utf8_len) > MAX_KERNEL_RULES) return -1;

          for (u32 j = 0; j < utf8_len; j++)
          {
            rule->cmds[rule_cnt + j] = RULE_OP_MANGLE_INSERT_EVERY
                                     | (((insert_pos + j) & 0xff) << 8)
                                     | ((rule_buf[rule_pos + 2 + j] & 0xff) << 16);
          }

          rule_pos += utf8_len + 1;
          rule_cnt += utf8_len - 1;
        }
        else
        {
          SET_NAME    (rule, rule_buf[rule_pos]);
          SET_P0_CONV (rule, rule_buf[rule_pos]);
          SET_P1      (rule, rule_buf[rule_pos]);
        }
        break;
      }

      case RULE_OP_MANGLE_OVERSTRIKE:
      {
        if ((rule_pos + 1) >= rule_len) return -1;

        const int overstrike_pos = conv_ctoi (rule_buf[rule_pos + 1]);

        if (overstrike_pos < 0) return -1;

        const u32 utf8_len = rule_utf8_char_len ((const u8 *) rule_buf, rule_len, rule_pos + 2);

        if (utf8_len > 1)
        {
          if ((rule_cnt + utf8_len) > MAX_KERNEL_RULES) return -1;

          for (u32 j = 0; j < utf8_len; j++)
          {
            rule->cmds[rule_cnt + j] = RULE_OP_MANGLE_OVERSTRIKE
                                     | (((overstrike_pos + j) & 0xff) << 8)
                                     | ((rule_buf[rule_pos + 2 + j] & 0xff) << 16);
          }

          rule_pos += utf8_len + 1;
          rule_cnt += utf8_len - 1;
        }
        else
        {
          SET_NAME    (rule, rule_buf[rule_pos]);
          SET_P0_CONV (rule, rule_buf[rule_pos]);
          SET_P1      (rule, rule_buf[rule_pos]);
        }
        break;
      }

      case RULE_OP_MANGLE_TRUNCATE_AT:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_REPLACE:
        SET_NAME (rule, rule_buf[rule_pos]);
        SET_P0   (rule, rule_buf[rule_pos]);
        SET_P1   (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_PURGECHAR:
        SET_NAME (rule, rule_buf[rule_pos]);
        SET_P0   (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_TOGGLECASE_REC:
        return -1;

      case RULE_OP_MANGLE_DUPECHAR_FIRST:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_DUPECHAR_LAST:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_DUPECHAR_ALL:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_SWITCH_FIRST:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_SWITCH_LAST:
        SET_NAME (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_SWITCH_AT:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        SET_P1_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_CHR_SHIFTL:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_CHR_SHIFTR:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_CHR_INCR:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_CHR_DECR:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_CHR_ADD:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        SET_P1      (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_REPLACE_NP1:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_REPLACE_NM1:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_DUPEBLOCK_FIRST:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_DUPEBLOCK_LAST:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_TITLE:
        SET_NAME    (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_TITLE_SEP:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0      (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_TOGGLE_AT_SEP:
        SET_NAME    (rule, rule_buf[rule_pos]);
        SET_P0_CONV (rule, rule_buf[rule_pos]);
        SET_P1      (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_TO_HEX_LOWER:
        SET_NAME    (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_MANGLE_TO_HEX_UPPER:
        SET_NAME    (rule, rule_buf[rule_pos]);
        break;

      case RULE_OP_CLASS_BASED: // ~
        if ((rule_pos + 1) >= rule_len) return -1;

        switch (rule_buf[rule_pos+1])
        {
          case RULE_OP_MANGLE_REPLACE: // ~s?CY
            SET_NAME  (rule, RULE_OP_MANGLE_REPLACE_CLASS);
            INCR_POS;
            INCR_POS;
            SET_P0    (rule, rule_buf[rule_pos]);
            SET_P1    (rule, rule_buf[rule_pos]);
            break;

          case RULE_OP_MANGLE_PURGECHAR: // ~@?C
            SET_NAME  (rule, RULE_OP_MANGLE_PURGECHAR_CLASS);
            INCR_POS;
            INCR_POS;
            SET_P0    (rule, rule_buf[rule_pos]);
            break;

          case RULE_OP_MANGLE_TITLE_SEP: // ~e?C
            SET_NAME  (rule, RULE_OP_MANGLE_TITLE_SEP_CLASS);
            INCR_POS;
            INCR_POS;
            SET_P0    (rule, rule_buf[rule_pos]);
            break;

          /*
          case '!': // ~!?C
            SET_NAME  (rule, RULE_OP_REJECT_CONTAIN_CLASS);
            INCR_POS;
            INCR_POS;
            SET_P0    (rule, rule_buf[rule_pos]);
            break;
          case '/': // ~/?C
            SET_NAME  (rule, RULE_OP_REJECT_NOT_CONTAIN_CLASS);
            INCR_POS;
            INCR_POS;
            SET_P0    (rule, rule_buf[rule_pos]);
            break;
          case '(': // ~(?C
            SET_NAME  (rule, RULE_OP_REJECT_EQUAL_FIRST_CLASS);
            INCR_POS;
            INCR_POS;
            SET_P0    (rule, rule_buf[rule_pos]);
            break;
          case '(': // ~)?C
            SET_NAME  (rule, RULE_OP_REJECT_EQUAL_LAST_CLASS);
            INCR_POS;
            INCR_POS;
            SET_P0    (rule, rule_buf[rule_pos]);
            break;
          case '=': // ~=N?C
            SET_NAME  (rule, RULE_OP_REJECT_EQUAL_AT_CLASS);
            INCR_POS;
            SET_P0    (rule, rule_buf[rule_pos]);
            INCR_POS;
            SET_P1    (rule, rule_buf[rule_pos]);
            break;
          case '%': // ~%N?C
            SET_NAME  (rule, RULE_OP_REJECT_CONTAINS_CLASS);
            INCR_POS;
            SET_P0    (rule, rule_buf[rule_pos]);
            INCR_POS;
            SET_P1    (rule, rule_buf[rule_pos]);
            break;
          */
          default:
            return -1;
        }

        break;

      default:
        return -1;
    }
  }

  if (rule_pos < rule_len) return -1;

  return 0;
}

int kernel_rule_to_cpu_rule (char *rule_buf, kernel_rule_t *rule)
{
  u32 rule_cnt;
  u32 rule_pos;
  u32 rule_len = HCBUFSIZ_LARGE - 1; // maximum possible len

  for (rule_cnt = 0, rule_pos = 0; rule_pos < rule_len && rule_cnt < MAX_KERNEL_RULES; rule_pos++, rule_cnt++)
  {
    char rule_cmd;

    GET_NAME (rule);

    if (rule_cnt > 0) rule_buf[rule_pos++] = ' ';

    switch (rule_cmd)
    {
      case RULE_OP_MANGLE_NOOP:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_LREST:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_UREST:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_LREST_UFIRST:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_UREST_LFIRST:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_TREST:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_SHIFT_CASE:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_TOGGLE_AT:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_REVERSE:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_DUPEWORD:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_DUPEWORD_TIMES:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_REFLECT:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_ROTATE_LEFT:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_ROTATE_RIGHT:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_APPEND:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0 (rule);
        break;

      case RULE_OP_MANGLE_PREPEND:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0 (rule);
        break;

      case RULE_OP_MANGLE_DELETE_FIRST:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_DELETE_LAST:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_DELETE_AT:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_EXTRACT:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        GET_P1_CONV (rule);
        break;

      case RULE_OP_MANGLE_OMIT:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        GET_P1_CONV (rule);
        break;

      case RULE_OP_MANGLE_INSERT:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        GET_P1      (rule);
        break;

      case RULE_OP_MANGLE_INSERT_EVERY:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        GET_P1      (rule);
        break;

      case RULE_OP_MANGLE_OVERSTRIKE:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        GET_P1      (rule);
        break;

      case RULE_OP_MANGLE_TRUNCATE_AT:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_REPLACE:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0 (rule);
        GET_P1 (rule);
        break;

      case RULE_OP_MANGLE_PURGECHAR:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0 (rule);
        break;

      case RULE_OP_MANGLE_TOGGLECASE_REC:
        return -1;

      case RULE_OP_MANGLE_DUPECHAR_FIRST:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_DUPECHAR_LAST:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_DUPECHAR_ALL:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_SWITCH_FIRST:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_SWITCH_LAST:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_SWITCH_AT:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        GET_P1_CONV (rule);
        break;

      case RULE_OP_MANGLE_CHR_SHIFTL:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_CHR_SHIFTR:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_CHR_INCR:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_CHR_DECR:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_CHR_ADD:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        GET_P1      (rule);
        break;

      case RULE_OP_MANGLE_REPLACE_NP1:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_REPLACE_NM1:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_DUPEBLOCK_FIRST:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_DUPEBLOCK_LAST:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        break;

      case RULE_OP_MANGLE_TITLE:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_TITLE_SEP:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0 (rule);
        break;

      case RULE_OP_MANGLE_TOGGLE_AT_SEP:
        rule_buf[rule_pos] = rule_cmd;
        GET_P0_CONV (rule);
        GET_P1      (rule);
        break;

      case RULE_OP_MANGLE_TO_HEX_LOWER:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_TO_HEX_UPPER:
        rule_buf[rule_pos] = rule_cmd;
        break;

      case RULE_OP_MANGLE_REPLACE_CLASS:
        rule_buf[rule_pos++] = RULE_OP_CLASS_BASED;
        rule_buf[rule_pos++] = RULE_OP_MANGLE_REPLACE;
        rule_buf[rule_pos]   = '?';
        GET_P0 (rule);
        GET_P1 (rule);
        break;

      case RULE_OP_MANGLE_PURGECHAR_CLASS:
        rule_buf[rule_pos++] = RULE_OP_CLASS_BASED;
        rule_buf[rule_pos++] = RULE_OP_MANGLE_PURGECHAR;
        rule_buf[rule_pos]   = '?';
        GET_P0 (rule);
        break;

      case RULE_OP_MANGLE_TITLE_SEP_CLASS:
        rule_buf[rule_pos++] = RULE_OP_CLASS_BASED;
        rule_buf[rule_pos++] = RULE_OP_MANGLE_TITLE_SEP;
        rule_buf[rule_pos]   = '?';
        GET_P0 (rule);
        break;

      case 0:
        if (rule_pos == 0) return -1;
        return rule_pos - 1;

      default:
        return -1;
    }
  }

  return rule_pos;
}

bool kernel_rules_has_noop (const kernel_rule_t *kernel_rules_buf, const u32 kernel_rules_cnt)
{
  for (u32 kernel_rules_pos = 0; kernel_rules_pos < kernel_rules_cnt; kernel_rules_pos++)
  {
    if (kernel_rules_buf[kernel_rules_pos].cmds[0] != RULE_OP_MANGLE_NOOP) continue;
    if (kernel_rules_buf[kernel_rules_pos].cmds[1] != 0)                   continue;

    return true;
  }

  return false;
}

#define RULE_PARSE_PARALLEL_MINIMUM_BYTES   (1U << 24)
#define RULE_PARSE_PARALLEL_BYTES_PER_THREAD (1U << 20)
#define RULE_PARSE_PARALLEL_THREADS_MAX      64

typedef enum rule_parse_parallel_result
{
  RULE_PARSE_PARALLEL_ERROR  = -1,
  RULE_PARSE_PARALLEL_UNUSED =  0,
  RULE_PARSE_PARALLEL_DONE   =  1,
  RULE_PARSE_PARALLEL_REOPEN =  2

} rule_parse_parallel_result_t;

typedef enum rule_parse_status
{
  RULE_PARSE_STATUS_VALID = 0,
  RULE_PARSE_STATUS_INVALID,
  RULE_PARSE_STATUS_UNCONVERTIBLE

} rule_parse_status_t;

typedef struct rule_parse_parallel_thread_param
{
  const u8 *input;

  size_t from;
  size_t to;

  u64 candidate_base;

  u64 lines;
  u64 candidates;
  u64 valid_rules;
  u64 invalid_rules;

  kernel_rule_t *rules;
  u8            *statuses;

  bool parse;
  bool valid_input;

} rule_parse_parallel_thread_param_t;

#if defined (_WIN)
static HC_API_CALL DWORD rule_parse_parallel_thread (void *p)
#else
static HC_API_CALL void *rule_parse_parallel_thread (void *p)
#endif
{
  rule_parse_parallel_thread_param_t *param = (rule_parse_parallel_thread_param_t *) p;

  const u8 *input = param->input;

  hc_memchr_t hc_memchr = hc_memchr_get ();

  size_t pos = param->from;

  u64 lines         = 0;
  u64 candidates    = 0;
  u64 valid_rules   = 0;
  u64 invalid_rules = 0;

  param->valid_input = true;

  while (pos < param->to)
  {
    const size_t left = param->to - pos;
    const size_t step = hc_memchr (input + pos, '\n', left);

    size_t rule_len = step;

    while ((rule_len > 0) && (input[pos + rule_len - 1] == '\r')) rule_len--;

    if (rule_len >= HCBUFSIZ_LARGE)
    {
      param->valid_input = false;
    }
    else if ((rule_len > 0) && (input[pos] != '#'))
    {
      if (param->parse == true)
      {
        const u64 candidate_pos = param->candidate_base + candidates;

        kernel_rule_t *rule = &param->rules[param->candidate_base + valid_rules];

        char in[RP_PASSWORD_SIZE];
        char out[RP_PASSWORD_SIZE];

        memset (in,  0, sizeof (in));
        memset (out, 0, sizeof (out));
        memset (rule, 0, sizeof (*rule));

        const int result = _old_apply_rule ((const char *) input + pos, (int) rule_len, in, 1, out);

        if (result == -1)
        {
          param->statuses[candidate_pos] = RULE_PARSE_STATUS_INVALID;

          invalid_rules++;
        }
        else if (cpu_rule_to_kernel_rule ((char *) input + pos, (u32) rule_len, rule) == -1)
        {
          param->statuses[candidate_pos] = RULE_PARSE_STATUS_UNCONVERTIBLE;

          invalid_rules++;
        }
        else
        {
          valid_rules++;
        }
      }

      candidates++;
    }

    lines++;

    pos += step;

    if ((pos < param->to) && (input[pos] == '\n')) pos++;
  }

  param->lines         = lines;
  param->candidates    = candidates;
  param->valid_rules   = valid_rules;
  param->invalid_rules = invalid_rules;

  return 0;
}

static void rule_parse_parallel_run_threads (hc_thread_t *threads, rule_parse_parallel_thread_param_t *params, const u32 threads_cnt)
{
  u32 threads_created = 0;

  for (u32 thread_pos = 0; thread_pos < threads_cnt; thread_pos++)
  {
    hc_thread_t thread;

    #if defined (_WIN)
    hc_thread_create (thread, rule_parse_parallel_thread, &params[thread_pos]);

    if (thread == NULL)
    #else
    if (hc_thread_create (thread, rule_parse_parallel_thread, &params[thread_pos]) != 0)
    #endif
    {
      rule_parse_parallel_thread (&params[thread_pos]);
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

static size_t rule_parse_parallel_minimum (void)
{
  const char *value = getenv ("HASHCAT_RULE_PARSE_PARALLEL_MIN");

  if ((value != NULL) && (value[0] != 0))
  {
    char *end = NULL;

    const unsigned long long parsed = strtoull (value, &end, 10);

    if ((end != value) && (*end == 0) && (parsed >= 1) && (parsed <= SIZE_MAX))
    {
      return (size_t) parsed;
    }
  }

  return RULE_PARSE_PARALLEL_MINIMUM_BYTES;
}

static void rule_parse_parallel_report
(
  hashcat_ctx_t *hashcat_ctx,
  const char *rp_file,
  const u8 *input,
  const size_t input_sz,
  const u8 *statuses
)
{
  const user_options_t *user_options = hashcat_ctx->user_options;

  if (user_options->quiet == true) return;

  char *rule_buf = (char *) hcmalloc (HCBUFSIZ_LARGE);

  if (rule_buf == NULL) return;

  hc_memchr_t hc_memchr = hc_memchr_get ();

  size_t pos = 0;

  u64 candidate_pos = 0;

  u32 rule_line = 0;

  while (pos < input_sz)
  {
    const size_t left = input_sz - pos;
    const size_t step = hc_memchr (input + pos, '\n', left);

    size_t rule_len = step;

    while ((rule_len > 0) && (input[pos + rule_len - 1] == '\r')) rule_len--;

    rule_line++;

    if ((rule_len > 0) && (input[pos] != '#'))
    {
      const u8 status = statuses[candidate_pos++];

      if (status != RULE_PARSE_STATUS_VALID)
      {
        memcpy (rule_buf, input + pos, rule_len);

        rule_buf[rule_len] = 0;

        if (status == RULE_PARSE_STATUS_INVALID)
        {
          event_log_warning (hashcat_ctx, "Skipping invalid or unsupported rule in file %s on line %u: %s", rp_file, rule_line, rule_buf);
        }
        else
        {
          event_log_warning (hashcat_ctx, "Cannot convert rule for use on OpenCL device in file %s on line %u: %s", rp_file, rule_line, rule_buf);
        }
      }
    }

    pos += step;

    if ((pos < input_sz) && (input[pos] == '\n')) pos++;
  }

  hcfree (rule_buf);
}

static rule_parse_parallel_result_t kernel_rules_load_parallel
(
  hashcat_ctx_t *hashcat_ctx,
  HCFILE *fp,
  const char *rp_file,
  kernel_rule_t **out_buf,
  u32 *out_cnt
)
{
  const char *disable = getenv ("HASHCAT_RULE_PARSE_PARALLEL_DISABLE");

  if ((disable != NULL) && (disable[0] != 0) && (disable[0] != '0')) return RULE_PARSE_PARALLEL_UNUSED;

  // Compressed rule files retain the original streaming loader. A plain file
  // can be read once into an immutable buffer and split safely at line ends.

  if (fp->pfp == NULL) return RULE_PARSE_PARALLEL_UNUSED;

  struct stat st;

  if (hc_fstat (fp, &st) == -1) return RULE_PARSE_PARALLEL_UNUSED;

  const off_t input_off = hc_ftell (fp);

  if ((input_off < 0) || (st.st_size < input_off)) return RULE_PARSE_PARALLEL_UNUSED;

  const u64 input_sz64 = (u64) (st.st_size - input_off);

  if ((input_sz64 < rule_parse_parallel_minimum ()) || (input_sz64 > SIZE_MAX)) return RULE_PARSE_PARALLEL_UNUSED;

  const size_t input_sz = (size_t) input_sz64;

  const u32 processors = (u32) hc_get_processor_count ();

  u32 threads_cnt = processors;

  threads_cnt = MIN (threads_cnt, RULE_PARSE_PARALLEL_THREADS_MAX);

  const size_t threads_by_size = 1 + ((input_sz - 1) / RULE_PARSE_PARALLEL_BYTES_PER_THREAD);

  threads_cnt = MIN (threads_cnt, (u32) MIN (threads_by_size, RULE_PARSE_PARALLEL_THREADS_MAX));

  if (threads_cnt < 2) return RULE_PARSE_PARALLEL_UNUSED;

  u8 *input = (u8 *) hcmalloc (input_sz);

  if (input == NULL) return RULE_PARSE_PARALLEL_UNUSED;

  const size_t nread = hc_fread (input, 1, input_sz, fp);

  if (nread != input_sz)
  {
    hcfree (input);

    return RULE_PARSE_PARALLEL_REOPEN;
  }

  hc_thread_t *threads = (hc_thread_t *) hccalloc (threads_cnt, sizeof (hc_thread_t));

  rule_parse_parallel_thread_param_t *params = (rule_parse_parallel_thread_param_t *) hccalloc (threads_cnt, sizeof (rule_parse_parallel_thread_param_t));

  if ((threads == NULL) || (params == NULL))
  {
    hcfree (params);
    hcfree (threads);
    hcfree (input);

    return RULE_PARSE_PARALLEL_REOPEN;
  }

  hc_memchr_t hc_memchr = hc_memchr_get ();

  params[0].from = 0;

  for (u32 thread_pos = 1; thread_pos < threads_cnt; thread_pos++)
  {
    size_t from = (input_sz * thread_pos) / threads_cnt;

    const size_t left = input_sz - from;
    const size_t step = hc_memchr (input + from, '\n', left);

    from += step;

    if ((from < input_sz) && (input[from] == '\n')) from++;

    params[thread_pos].from = from;
  }

  for (u32 thread_pos = 0; thread_pos < threads_cnt; thread_pos++)
  {
    rule_parse_parallel_thread_param_t *param = &params[thread_pos];

    param->input = input;
    param->to    = (thread_pos + 1 < threads_cnt) ? params[thread_pos + 1].from : input_sz;
    param->parse = false;
  }

  rule_parse_parallel_run_threads (threads, params, threads_cnt);

  u64 lines      = 0;
  u64 candidates = 0;

  bool valid_input = true;

  for (u32 thread_pos = 0; thread_pos < threads_cnt; thread_pos++)
  {
    rule_parse_parallel_thread_param_t *param = &params[thread_pos];

    if (param->valid_input == false) valid_input = false;

    param->candidate_base = candidates;

    lines      += param->lines;
    candidates += param->candidates;
  }

  if (valid_input == false)
  {
    hcfree (params);
    hcfree (threads);
    hcfree (input);

    return RULE_PARSE_PARALLEL_REOPEN;
  }

  if (lines > UINT32_MAX)
  {
    event_log_error (hashcat_ctx, "Unsupported number of lines in rule file %s.", rp_file);

    hcfree (params);
    hcfree (threads);
    hcfree (input);

    return RULE_PARSE_PARALLEL_ERROR;
  }

  if ((candidates > UINT32_MAX) || (candidates > (SIZE_MAX / sizeof (kernel_rule_t))))
  {
    event_log_error (hashcat_ctx, "Unsupported number of rules in rule file %s.", rp_file);

    hcfree (params);
    hcfree (threads);
    hcfree (input);

    return RULE_PARSE_PARALLEL_ERROR;
  }

  if (candidates == 0)
  {
    hcfree (params);
    hcfree (threads);
    hcfree (input);

    return RULE_PARSE_PARALLEL_REOPEN;
  }

  kernel_rule_t *rules = (kernel_rule_t *) hccalloc ((size_t) candidates, sizeof (kernel_rule_t));
  u8 *statuses         = (u8 *)            hccalloc ((size_t) candidates, sizeof (u8));

  if ((rules == NULL) || (statuses == NULL))
  {
    hcfree (statuses);
    hcfree (rules);
    hcfree (params);
    hcfree (threads);
    hcfree (input);

    return RULE_PARSE_PARALLEL_ERROR;
  }

  for (u32 thread_pos = 0; thread_pos < threads_cnt; thread_pos++)
  {
    rule_parse_parallel_thread_param_t *param = &params[thread_pos];

    param->rules    = rules;
    param->statuses = statuses;
    param->parse    = true;
  }

  rule_parse_parallel_run_threads (threads, params, threads_cnt);

  u64 valid_rules   = 0;
  u64 invalid_rules = 0;

  for (u32 thread_pos = 0; thread_pos < threads_cnt; thread_pos++)
  {
    rule_parse_parallel_thread_param_t *param = &params[thread_pos];

    kernel_rule_t *src = &rules[param->candidate_base];
    kernel_rule_t *dst = &rules[valid_rules];

    if ((param->valid_rules > 0) && (src != dst))
    {
      memmove (dst, src, (size_t) param->valid_rules * sizeof (kernel_rule_t));
    }

    valid_rules   += param->valid_rules;
    invalid_rules += param->invalid_rules;
  }

  if (invalid_rules > 0)
  {
    rule_parse_parallel_report (hashcat_ctx, rp_file, input, input_sz, statuses);
  }

  hcfree (statuses);
  hcfree (params);
  hcfree (threads);
  hcfree (input);

  *out_buf = rules;
  *out_cnt = (u32) valid_rules;

  return RULE_PARSE_PARALLEL_DONE;
}

int kernel_rules_load (hashcat_ctx_t *hashcat_ctx, kernel_rule_t **out_buf, u32 *out_cnt)
{
  const user_options_t *user_options = hashcat_ctx->user_options;

  /**
   * load rules
   */

  u32 *all_kernel_rules_cnt = NULL;

  kernel_rule_t **all_kernel_rules_buf = NULL;

  if (user_options->rp_files_cnt)
  {
    all_kernel_rules_cnt = (u32 *) hccalloc (user_options->rp_files_cnt, sizeof (u32));

    all_kernel_rules_buf = (kernel_rule_t **) hccalloc (user_options->rp_files_cnt, sizeof (kernel_rule_t *));
  }

  char *rule_buf = NULL;

  u32 rule_len = 0;

  for (u32 i = 0; i < user_options->rp_files_cnt; i++)
  {
    u32 kernel_rules_avail = 0;

    u32 kernel_rules_cnt = 0;

    kernel_rule_t *kernel_rules_buf = NULL;

    char *rp_file = user_options->rp_files[i];

    HCFILE fp;

    u32 rule_line = 0;

    if (hc_fopen (&fp, rp_file, "rb") == false)
    {
      event_log_error (hashcat_ctx, "%s: %s", rp_file, strerror (errno));

      for (u32 j = 0; j < i; j++)
      {
        hcfree (all_kernel_rules_buf[j]);
      }

      hcfree (all_kernel_rules_cnt);
      hcfree (all_kernel_rules_buf);

      hcfree (rule_buf);

      return -1;
    }

    const rule_parse_parallel_result_t parallel_result = kernel_rules_load_parallel
    (
      hashcat_ctx,
      &fp,
      rp_file,
      &kernel_rules_buf,
      &kernel_rules_cnt
    );

    if (parallel_result == RULE_PARSE_PARALLEL_DONE)
    {
      hc_fclose (&fp);

      all_kernel_rules_cnt[i] = kernel_rules_cnt;
      all_kernel_rules_buf[i] = kernel_rules_buf;

      continue;
    }

    if (parallel_result == RULE_PARSE_PARALLEL_ERROR)
    {
      hc_fclose (&fp);

      for (u32 j = 0; j < i; j++)
      {
        hcfree (all_kernel_rules_buf[j]);
      }

      hcfree (all_kernel_rules_cnt);
      hcfree (all_kernel_rules_buf);

      hcfree (rule_buf);

      return -1;
    }

    if (parallel_result == RULE_PARSE_PARALLEL_REOPEN)
    {
      hc_fclose (&fp);

      if (hc_fopen (&fp, rp_file, "rb") == false)
      {
        event_log_error (hashcat_ctx, "%s: %s", rp_file, strerror (errno));

        for (u32 j = 0; j < i; j++)
        {
          hcfree (all_kernel_rules_buf[j]);
        }

        hcfree (all_kernel_rules_cnt);
        hcfree (all_kernel_rules_buf);

        hcfree (rule_buf);

        return -1;
      }
    }

    if (rule_buf == NULL)
    {
      rule_buf = (char *) hcmalloc (HCBUFSIZ_LARGE);

      if (rule_buf == NULL)
      {
        hc_fclose (&fp);

        for (u32 j = 0; j < i; j++)
        {
          hcfree (all_kernel_rules_buf[j]);
        }

        hcfree (all_kernel_rules_cnt);
        hcfree (all_kernel_rules_buf);

        return -1;
      }
    }

    while (!hc_feof (&fp))
    {
      rule_len = (u32) fgetl (&fp, rule_buf, HCBUFSIZ_LARGE);

      if ((rule_line == 0) && (rule_len >= 3))
      {
        const u8 *rule_ptr = (const u8 *) rule_buf;

        if ((rule_ptr[0] == 0xef) && (rule_ptr[1] == 0xbb) && (rule_ptr[2] == 0xbf))
        {
          rule_len -= 3;

          memmove (rule_buf, rule_buf + 3, rule_len);

          rule_buf[rule_len] = 0;
        }
      }

      if (rule_line == (u32) -1)
      {
        event_log_error (hashcat_ctx, "Unsupported number of lines in rule file %s.", rp_file);

        hc_fclose (&fp);

        hcfree (kernel_rules_buf);

        for (u32 j = 0; j < i; j++)
        {
          hcfree (all_kernel_rules_buf[j]);
        }

        hcfree (all_kernel_rules_cnt);
        hcfree (all_kernel_rules_buf);

        hcfree (rule_buf);

        return -1;
      }

      rule_line++;

      if (rule_len == 0) continue;

      if (rule_buf[0] == '#') continue;

      if (kernel_rules_avail == kernel_rules_cnt)
      {
        const u32 kernel_rules_avail_old = kernel_rules_avail;

        if (kernel_rules_avail == 0)
        {
          kernel_rules_avail = INCR_RULES;
        }
        else if (kernel_rules_avail <= (UINT32_MAX / 2))
        {
          kernel_rules_avail *= 2;
        }
        else
        {
          kernel_rules_avail = UINT32_MAX;
        }

        if ((kernel_rules_avail <= kernel_rules_avail_old)
         || (kernel_rules_avail > (SIZE_MAX / sizeof (kernel_rule_t))))
        {
          event_log_error (hashcat_ctx, "Unsupported number of rules in rule file %s.", rp_file);

          hc_fclose (&fp);

          hcfree (kernel_rules_buf);

          for (u32 j = 0; j < i; j++)
          {
            hcfree (all_kernel_rules_buf[j]);
          }

          hcfree (all_kernel_rules_cnt);
          hcfree (all_kernel_rules_buf);

          hcfree (rule_buf);

          return -1;
        }

        kernel_rule_t *kernel_rules_buf_new = (kernel_rule_t *) hcrealloc
        (
          kernel_rules_buf,
          (size_t) kernel_rules_avail_old * sizeof (kernel_rule_t),
          (size_t) (kernel_rules_avail - kernel_rules_avail_old) * sizeof (kernel_rule_t)
        );

        if (kernel_rules_buf_new == NULL)
        {
          hc_fclose (&fp);

          hcfree (kernel_rules_buf);

          for (u32 j = 0; j < i; j++)
          {
            hcfree (all_kernel_rules_buf[j]);
          }

          hcfree (all_kernel_rules_cnt);
          hcfree (all_kernel_rules_buf);

          hcfree (rule_buf);

          return -1;
        }

        kernel_rules_buf = kernel_rules_buf_new;
      }

      char in[RP_PASSWORD_SIZE];
      char out[RP_PASSWORD_SIZE];

      memset (in,  0, sizeof (in));
      memset (out, 0, sizeof (out));

      int result = _old_apply_rule (rule_buf, rule_len, in, 1, out);

      if (result == -1)
      {
        if (user_options->quiet == false)
        {
          event_log_warning (hashcat_ctx, "Skipping invalid or unsupported rule in file %s on line %u: %s", rp_file, rule_line, rule_buf);
        }
        continue;
      }

      if (cpu_rule_to_kernel_rule (rule_buf, rule_len, &kernel_rules_buf[kernel_rules_cnt]) == -1)
      {
        if (user_options->quiet == false)
        {
          event_log_warning (hashcat_ctx, "Cannot convert rule for use on OpenCL device in file %s on line %u: %s", rp_file, rule_line, rule_buf);
        }
        memset (&kernel_rules_buf[kernel_rules_cnt], 0, sizeof (kernel_rule_t)); // needs to be cleared otherwise we could have some remaining data

        continue;
      }

      if (kernel_rules_cnt == (u32) -1)
      {
        event_log_error (hashcat_ctx, "Unsupported number of rules in rule file %s.", rp_file);

        hc_fclose (&fp);

        hcfree (kernel_rules_buf);

        for (u32 j = 0; j < i; j++)
        {
          hcfree (all_kernel_rules_buf[j]);
        }

        hcfree (all_kernel_rules_cnt);
        hcfree (all_kernel_rules_buf);

        hcfree (rule_buf);

        return -1;
      }

      kernel_rules_cnt++;
    }

    hc_fclose (&fp);

    all_kernel_rules_cnt[i] = kernel_rules_cnt;
    all_kernel_rules_buf[i] = kernel_rules_buf;
  }

  hcfree (rule_buf);

  if (user_options->rp_files_cnt == 1)
  {
    const u32 kernel_rules_cnt = all_kernel_rules_cnt[0];

    kernel_rule_t *kernel_rules_buf = all_kernel_rules_buf[0];

    hcfree (all_kernel_rules_cnt);
    hcfree (all_kernel_rules_buf);

    if (kernel_rules_cnt == 0)
    {
      event_log_error (hashcat_ctx, "No valid rules left.");

      hcfree (kernel_rules_buf);

      return -1;
    }

    *out_cnt = kernel_rules_cnt;
    *out_buf = kernel_rules_buf;

    return 0;
  }

  /**
   * merge rules
   */

  u32 kernel_rules_cnt = 1;

  u32 *repeats = (u32 *) hccalloc (user_options->rp_files_cnt + 1, sizeof (u32));

  repeats[0] = kernel_rules_cnt;

  for (u32 i = 0; i < user_options->rp_files_cnt; i++)
  {
    if (overflow_check_u32_mul (kernel_rules_cnt, all_kernel_rules_cnt[i]) == true)
    {
      event_log_error (hashcat_ctx, "Unsupported number of rules used in rule chaining.");

      for (u32 j = 0; j < user_options->rp_files_cnt; j++)
      {
        hcfree (all_kernel_rules_buf[j]);
      }

      hcfree (all_kernel_rules_cnt);
      hcfree (all_kernel_rules_buf);

      hcfree (repeats);

      return -1;
    }

    kernel_rules_cnt *= all_kernel_rules_cnt[i];

    repeats[i + 1] = kernel_rules_cnt;
  }

  kernel_rule_t *kernel_rules_buf = (kernel_rule_t *) hccalloc (kernel_rules_cnt, sizeof (kernel_rule_t));

  if (kernel_rules_buf == NULL)
  {
    event_log_error (hashcat_ctx, "Not enough allocatable memory (RAM) for this ruleset.");

    for (u32 j = 0; j < user_options->rp_files_cnt; j++)
    {
      hcfree (all_kernel_rules_buf[j]);
    }

    hcfree (all_kernel_rules_cnt);
    hcfree (all_kernel_rules_buf);

    hcfree (repeats);

    return -1;
  }

  u32 invalid_cnt = 0;
  u32 valid_cnt = 0;

  for (u32 i = 0; i < kernel_rules_cnt; i++)
  {
    u32 out_pos = 0;

    kernel_rule_t *out = &kernel_rules_buf[i - invalid_cnt];

    for (u32 j = 0; j < user_options->rp_files_cnt; j++)
    {
      u32 in_off = (i / repeats[j]) % all_kernel_rules_cnt[j];
      u32 in_pos;

      kernel_rule_t *in = &all_kernel_rules_buf[j][in_off];

      for (in_pos = 0; in->cmds[in_pos]; in_pos++, out_pos++)
      {
        if (out_pos == RULES_MAX - 1)
        {
          invalid_cnt++;

          break;
        }
        else
        {
          valid_cnt++;
        }

        out->cmds[out_pos] = in->cmds[in_pos];
      }
    }
  }

  if (invalid_cnt > 0)
  {
    event_log_warning (hashcat_ctx, "Maximum functions per rule exceeded during chaining of rules.");
    event_log_warning (hashcat_ctx, "Skipped %u rule chains, %u valid chains remain.", invalid_cnt, valid_cnt);
    event_log_warning (hashcat_ctx, NULL);
  }

  hcfree (repeats);

  kernel_rules_cnt -= invalid_cnt;

  for (u32 i = 0; i < user_options->rp_files_cnt; i++)
  {
    hcfree (all_kernel_rules_buf[i]);
  }

  hcfree (all_kernel_rules_cnt);
  hcfree (all_kernel_rules_buf);

  if (kernel_rules_cnt == 0)
  {
    event_log_error (hashcat_ctx, "No valid rules left.");

    hcfree (kernel_rules_buf);

    return -1;
  }

  *out_cnt = kernel_rules_cnt;
  *out_buf = kernel_rules_buf;

  return 0;
}

int kernel_rules_generate (hashcat_ctx_t *hashcat_ctx, kernel_rule_t **out_buf, u32 *out_cnt, const char *rp_gen_func_selection)
{
  const user_options_t *user_options = hashcat_ctx->user_options;

  u32            kernel_rules_cnt = 0;
  kernel_rule_t *kernel_rules_buf = (kernel_rule_t *) hccalloc (user_options->rp_gen, sizeof (kernel_rule_t));

  // operator selection

  rp_gen_ops_t rp_gen_ops;

  rp_gen_ops.grp_op_nop_selection      = hcmalloc (sizeof (grp_op_nop));
  rp_gen_ops.grp_op_pos_p0_selection   = hcmalloc (sizeof (grp_op_pos_p0));
  rp_gen_ops.grp_op_pos_p1_selection   = hcmalloc (sizeof (grp_op_pos_p1));
  rp_gen_ops.grp_op_chr_selection      = hcmalloc (sizeof (grp_op_chr));
  rp_gen_ops.grp_op_chr_chr_selection  = hcmalloc (sizeof (grp_op_chr_chr));
  rp_gen_ops.grp_op_pos_chr_selection  = hcmalloc (sizeof (grp_op_pos_chr));
  rp_gen_ops.grp_op_pos_pos0_selection = hcmalloc (sizeof (grp_op_pos_pos0));
  rp_gen_ops.grp_op_pos_pos1_selection = hcmalloc (sizeof (grp_op_pos_pos1));

  rp_gen_ops.grp_op_nop_cnt      = 0;
  rp_gen_ops.grp_op_pos_p0_cnt   = 0;
  rp_gen_ops.grp_op_pos_p1_cnt   = 0;
  rp_gen_ops.grp_op_chr_cnt      = 0;
  rp_gen_ops.grp_op_chr_chr_cnt  = 0;
  rp_gen_ops.grp_op_pos_chr_cnt  = 0;
  rp_gen_ops.grp_op_pos_pos0_cnt = 0;
  rp_gen_ops.grp_op_pos_pos1_cnt = 0;

  rp_gen_ops.grp_op_alias_cnt = 0;

  for (size_t i = 0; i < sizeof (grp_op_nop); i++)
  {
    if (rp_gen_func_selection == NULL)
    {
      rp_gen_ops.grp_op_nop_selection[rp_gen_ops.grp_op_nop_cnt] = grp_op_nop[i];

      rp_gen_ops.grp_op_nop_cnt++;
    }
    else
    {
      if (strchr (rp_gen_func_selection, grp_op_nop[i]) == NULL) continue;

      rp_gen_ops.grp_op_nop_selection[rp_gen_ops.grp_op_nop_cnt] = grp_op_nop[i];

      rp_gen_ops.grp_op_nop_cnt++;
    }
  }

  for (size_t i = 0; i < sizeof (grp_op_pos_p0); i++)
  {
    if (rp_gen_func_selection == NULL)
    {
      rp_gen_ops.grp_op_pos_p0_selection[rp_gen_ops.grp_op_pos_p0_cnt] = grp_op_pos_p0[i];

      rp_gen_ops.grp_op_pos_p0_cnt++;
    }
    else
    {
      if (strchr (rp_gen_func_selection, grp_op_pos_p0[i]) == NULL) continue;

      rp_gen_ops.grp_op_pos_p0_selection[rp_gen_ops.grp_op_pos_p0_cnt] = grp_op_pos_p0[i];

      rp_gen_ops.grp_op_pos_p0_cnt++;
    }
  }

  for (size_t i = 0; i < sizeof (grp_op_pos_p1); i++)
  {
    if (rp_gen_func_selection == NULL)
    {
      rp_gen_ops.grp_op_pos_p1_selection[rp_gen_ops.grp_op_pos_p1_cnt] = grp_op_pos_p1[i];

      rp_gen_ops.grp_op_pos_p1_cnt++;
    }
    else
    {
      if (strchr (rp_gen_func_selection, grp_op_pos_p1[i]) == NULL) continue;

      rp_gen_ops.grp_op_pos_p1_selection[rp_gen_ops.grp_op_pos_p1_cnt] = grp_op_pos_p1[i];

      rp_gen_ops.grp_op_pos_p1_cnt++;
    }
  }

  for (size_t i = 0; i < sizeof (grp_op_chr); i++)
  {
    if (rp_gen_func_selection == NULL)
    {
      rp_gen_ops.grp_op_chr_selection[rp_gen_ops.grp_op_chr_cnt] = grp_op_chr[i];

      rp_gen_ops.grp_op_chr_cnt++;
    }
    else
    {
      if (strchr (rp_gen_func_selection, grp_op_chr[i]) == NULL) continue;

      rp_gen_ops.grp_op_chr_selection[rp_gen_ops.grp_op_chr_cnt] = grp_op_chr[i];

      rp_gen_ops.grp_op_chr_cnt++;
    }
  }

  for (size_t i = 0; i < sizeof (grp_op_chr_chr); i++)
  {
    if (rp_gen_func_selection == NULL)
    {
      rp_gen_ops.grp_op_chr_chr_selection[rp_gen_ops.grp_op_chr_chr_cnt] = grp_op_chr_chr[i];

      rp_gen_ops.grp_op_chr_chr_cnt++;
    }
    else
    {
      if (strchr (rp_gen_func_selection, grp_op_chr_chr[i]) == NULL) continue;

      rp_gen_ops.grp_op_chr_chr_selection[rp_gen_ops.grp_op_chr_chr_cnt] = grp_op_chr_chr[i];

      rp_gen_ops.grp_op_chr_chr_cnt++;
    }
  }

  for (size_t i = 0; i < sizeof (grp_op_pos_chr); i++)
  {
    if (rp_gen_func_selection == NULL)
    {
      rp_gen_ops.grp_op_pos_chr_selection[rp_gen_ops.grp_op_pos_chr_cnt] = grp_op_pos_chr[i];

      rp_gen_ops.grp_op_pos_chr_cnt++;
    }
    else
    {
      if (strchr (rp_gen_func_selection, grp_op_pos_chr[i]) == NULL) continue;

      rp_gen_ops.grp_op_pos_chr_selection[rp_gen_ops.grp_op_pos_chr_cnt] = grp_op_pos_chr[i];

      rp_gen_ops.grp_op_pos_chr_cnt++;
    }
  }

  for (size_t i = 0; i < sizeof (grp_op_pos_pos0); i++)
  {
    if (rp_gen_func_selection == NULL)
    {
      rp_gen_ops.grp_op_pos_pos0_selection[rp_gen_ops.grp_op_pos_pos0_cnt] = grp_op_pos_pos0[i];

      rp_gen_ops.grp_op_pos_pos0_cnt++;
    }
    else
    {
      if (strchr (rp_gen_func_selection, grp_op_pos_pos0[i]) == NULL) continue;

      rp_gen_ops.grp_op_pos_pos0_selection[rp_gen_ops.grp_op_pos_pos0_cnt] = grp_op_pos_pos0[i];

      rp_gen_ops.grp_op_pos_pos0_cnt++;
    }
  }

  for (size_t i = 0; i < sizeof (grp_op_pos_pos1); i++)
  {
    if (rp_gen_func_selection == NULL)
    {
      rp_gen_ops.grp_op_pos_pos1_selection[rp_gen_ops.grp_op_pos_pos1_cnt] = grp_op_pos_pos1[i];

      rp_gen_ops.grp_op_pos_pos1_cnt++;
    }
    else
    {
      if (strchr (rp_gen_func_selection, grp_op_pos_pos1[i]) == NULL) continue;

      rp_gen_ops.grp_op_pos_pos1_selection[rp_gen_ops.grp_op_pos_pos1_cnt] = grp_op_pos_pos1[i];

      rp_gen_ops.grp_op_pos_pos1_cnt++;
    }
  }

  if (rp_gen_ops.grp_op_nop_cnt)      { rp_gen_ops.grp_op_alias_buf[rp_gen_ops.grp_op_alias_cnt++] = 0; };
  if (rp_gen_ops.grp_op_pos_p0_cnt)   { rp_gen_ops.grp_op_alias_buf[rp_gen_ops.grp_op_alias_cnt++] = 1; };
  if (rp_gen_ops.grp_op_pos_p1_cnt)   { rp_gen_ops.grp_op_alias_buf[rp_gen_ops.grp_op_alias_cnt++] = 2; };
  if (rp_gen_ops.grp_op_chr_cnt)      { rp_gen_ops.grp_op_alias_buf[rp_gen_ops.grp_op_alias_cnt++] = 3; };
  if (rp_gen_ops.grp_op_chr_chr_cnt)  { rp_gen_ops.grp_op_alias_buf[rp_gen_ops.grp_op_alias_cnt++] = 4; };
  if (rp_gen_ops.grp_op_pos_chr_cnt)  { rp_gen_ops.grp_op_alias_buf[rp_gen_ops.grp_op_alias_cnt++] = 5; };
  if (rp_gen_ops.grp_op_pos_pos0_cnt) { rp_gen_ops.grp_op_alias_buf[rp_gen_ops.grp_op_alias_cnt++] = 6; };
  if (rp_gen_ops.grp_op_pos_pos1_cnt) { rp_gen_ops.grp_op_alias_buf[rp_gen_ops.grp_op_alias_cnt++] = 7; };

  char *rule_buf = (char *) hcmalloc (RP_RULE_SIZE);

  for (kernel_rules_cnt = 0; kernel_rules_cnt < user_options->rp_gen; kernel_rules_cnt++)
  {
    memset (rule_buf, 0, RP_RULE_SIZE);

    const int rule_len = generate_random_rule (rule_buf, user_options->rp_gen_func_min, user_options->rp_gen_func_max, &rp_gen_ops);

    if (cpu_rule_to_kernel_rule (rule_buf, rule_len, &kernel_rules_buf[kernel_rules_cnt]) == -1) continue;
  }

  hcfree (rule_buf);

  hcfree (rp_gen_ops.grp_op_nop_selection);
  hcfree (rp_gen_ops.grp_op_pos_p0_selection);
  hcfree (rp_gen_ops.grp_op_pos_p1_selection);
  hcfree (rp_gen_ops.grp_op_chr_selection);
  hcfree (rp_gen_ops.grp_op_chr_chr_selection);
  hcfree (rp_gen_ops.grp_op_pos_chr_selection);
  hcfree (rp_gen_ops.grp_op_pos_pos0_selection);
  hcfree (rp_gen_ops.grp_op_pos_pos1_selection);

  *out_cnt = kernel_rules_cnt;
  *out_buf = kernel_rules_buf;

  return 0;
}

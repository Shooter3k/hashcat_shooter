#!/usr/bin/env python3
"""Generate isolated module_eN wrappers from mdxfind's live registry.

The generator deliberately reads Types[] and Maphashcat[] from mdxfind.c.
HASH_TYPES.md is useful documentation, but it can lag the executable registry.
Existing hashcat module sources are included, never edited or copied.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


MANUAL_HASHCAT_MODES = {
    942: 22000,  # WPA-PMKID; 16800 is deprecated and rejects current input
    536: 26200,  # Progress OpenEdge Encode
    997: 12400,  # BSDiCrypt
    998: 36200,  # Shooter gost-yescrypt mode (mdxfind calls it 46100)
    999: 15100,  # sha1crypt
    1000: 11600, # 7-Zip
    1001: 29970, # Shooter CMIYC 2026 GPU implementation
}

MDXFIND_HASH_MODE_BASE = 90000
MDXFIND_HASH_MODE_COUNT = 1001

SPECIALIZED_ALIAS_HEADERS = {
    987: "mdxfind_argon2_module.h",
}

# These mdxfind formats differ from the similarly named native Hashcat mode's
# parser or exact computation.  Keep the native modules untouched and route
# only their eN compatibility wrappers through the tested Hashpipe verifier.
FORCE_BRIDGE_IDS = {
    2, 118, 303, 379, 521, 530, 533, 577, 579, 829,
    841, 842, 843, 844, 845, 846, 847, 848, 849, 855,
    872, 873, 874, 875, 879, 881, 884, 885, 914, 919,
    922, 923, 925, 927, 940, 941, 968, 992,
}


def parse_registry(mdxfind_c: Path) -> tuple[list[str], dict[int, list[int]]]:
    text = mdxfind_c.read_text(encoding="utf-8", errors="replace")

    types_start = text.index("char *Types[] = {")
    types_end = text.index("\nNULL\n", types_start)
    types_body = text[types_start:types_end]
    names = re.findall(r'"((?:\\.|[^"\\])*)"', types_body)

    if len(names) != 1002 or names[0] != "none":
        raise RuntimeError(
            f"unexpected mdxfind Types[] shape: {len(names)} entries"
        )

    map_start = text.rfind("Maphashcat[]", 0, types_start)
    map_open = text.index("{", map_start)
    map_body = text[map_open:types_start]

    mappings: dict[int, list[int]] = {}

    for hashcat_mode, mdxfind_id in re.findall(
        r"\{\s*(\d+)\s*,\s*(\d+)\s*\}", map_body
    ):
        hashcat_mode_i = int(hashcat_mode)
        mdxfind_id_i = int(mdxfind_id)

        if hashcat_mode_i == 65535:
            continue

        mappings.setdefault(mdxfind_id_i, []).append(hashcat_mode_i)

    return names, mappings


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def alias_source(mdxfind_id: int, name: str, hashcat_mode: int) -> str:
    return f"""/**
 * Auto-generated mdxfind alias module: e{mdxfind_id} {name}
 * Source: tools/generate_mdxfind_modules.py
 *
 * The existing hashcat module is included unchanged.  This file only gives
 * that implementation an mdxfind-compatible module filename.
 */

#include "common.h"
#include "types.h"
#include "modules.h"
#include "mdxfind_modes.h"

#define module_init mdxfind_base_module_init
#include "module_{hashcat_mode:05d}.c"
#undef module_init

static const char *MDXFIND_HASH_NAME = {c_string(f"mdxfind e{mdxfind_id} {name}")};

static int mdxfind_alias_hash_mode (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{{
  return MDXFIND_HASH_MODE_FROM_ID ({mdxfind_id});
}}

static const char *mdxfind_alias_hash_name (MAYBE_UNUSED const hashconfig_t *hashconfig, MAYBE_UNUSED const user_options_t *user_options, MAYBE_UNUSED const user_options_extra_t *user_options_extra)
{{
  return MDXFIND_HASH_NAME;
}}

void module_init (module_ctx_t *module_ctx)
{{
  mdxfind_base_module_init (module_ctx);

  module_ctx->module_hash_mode = mdxfind_alias_hash_mode;
  module_ctx->module_hash_name = mdxfind_alias_hash_name;
}}
"""


def specialized_alias_source(
    mdxfind_id: int, name: str, hashcat_mode: int, header: str
) -> str:
    return f"""/**
 * Auto-generated specialized mdxfind alias: e{mdxfind_id} {name}
 * Source: tools/generate_mdxfind_modules.py
 *
 * The compatibility header preserves mdxfind-specific input formats before
 * delegating computation to the existing hashcat module.
 */

#include "common.h"
#include "types.h"
#include "modules.h"
#include "mdxfind_modes.h"

#define module_init mdxfind_base_module_init
#include "module_{hashcat_mode:05d}.c"
#undef module_init

#define MDXFIND_MODE_ID {mdxfind_id}
#define MDXFIND_MODE_NAME {c_string(name)}
#include "{header}"
"""


def bridge_source(mdxfind_id: int, name: str) -> str:
    return f"""/**
 * Auto-generated mdxfind bridge module: e{mdxfind_id} {name}
 * Source: tools/generate_mdxfind_modules.py
 */

#define MDXFIND_MODE_ID {mdxfind_id}
#define MDXFIND_MODE_NAME {c_string(name)}
#include "mdxfind_bridge_module.h"
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mdxfind", type=Path, required=True)
    parser.add_argument("--hashcat", type=Path, required=True)
    args = parser.parse_args()

    mdxfind_c = args.mdxfind / "mdxfind.c"
    module_dir = args.hashcat / "src" / "modules"
    catalog_path = args.hashcat / "docs" / "mdxfind-modules.json"

    names, mappings = parse_registry(mdxfind_c)
    available = {
        int(path.stem.removeprefix("module_"))
        for path in module_dir.glob("module_[0-9][0-9][0-9][0-9][0-9].c")
    }

    reserved = set(
        range(
            MDXFIND_HASH_MODE_BASE + 1,
            MDXFIND_HASH_MODE_BASE + MDXFIND_HASH_MODE_COUNT + 1,
        )
    )
    collisions = sorted(available & reserved)

    if collisions:
        raise RuntimeError(
            "mdxfind internal mode range collides with numeric modules: "
            + ", ".join(map(str, collisions))
        )

    catalog = []

    for mdxfind_id, name in enumerate(names[1:], 1):
        candidates = list(mappings.get(mdxfind_id, []))

        if mdxfind_id in MANUAL_HASHCAT_MODES:
            candidates.insert(0, MANUAL_HASHCAT_MODES[mdxfind_id])

        hashcat_mode = (
            None
            if mdxfind_id in FORCE_BRIDGE_IDS
            else next((mode for mode in candidates if mode in available), None)
        )
        output = module_dir / f"module_e{mdxfind_id}.c"

        if hashcat_mode is None:
            output.write_text(bridge_source(mdxfind_id, name), encoding="utf-8", newline="\n")
            implementation = "mdxfind-bridge"
        else:
            specialized_header = SPECIALIZED_ALIAS_HEADERS.get(mdxfind_id)

            output.write_text(
                specialized_alias_source(
                    mdxfind_id, name, hashcat_mode, specialized_header
                )
                if specialized_header is not None
                else alias_source(mdxfind_id, name, hashcat_mode),
                encoding="utf-8",
                newline="\n",
            )
            implementation = (
                f"hashcat-{hashcat_mode}-mdxfind-format"
                if specialized_header is not None
                else f"hashcat-{hashcat_mode}"
            )

        catalog.append(
            {
                "mode": f"e{mdxfind_id}",
                "id": mdxfind_id,
                "name": name,
                "implementation": implementation,
                "hashcat_candidates": candidates,
            }
        )

    catalog_path.write_text(
        json.dumps(catalog, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    alias_count = sum(item["implementation"].startswith("hashcat-") for item in catalog)
    bridge_count = len(catalog) - alias_count

    print(f"generated {len(catalog)} modules: {alias_count} aliases, {bridge_count} bridged")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

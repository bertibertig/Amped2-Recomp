#!/usr/bin/env bash
# Runs the xboxrecomp pipeline (steps 2-5 of GETTING_STARTED.md).
#   scripts/run_pipeline.sh            # everything
#   scripts/run_pipeline.sh parse      # xbe_parser only
#   scripts/run_pipeline.sh disasm | names | funcid | abi | recomp
#   (names = apply Ghidra names from scripts/ghidra.sh; runs automatically with "all" once the export exists)
# Intermediate results go to xboxrecomp/tools/*/output/ (git ignores the dirty submodule).
source "$(dirname "$0")/_common.sh"
use_venv; need_xbe; apply_runtime_patches
STEP="${1:-all}"
cd "$TOOLKIT"
run() { echo; echo "=== $* ==="; "$@"; }

[[ "$STEP" =~ ^(all|parse)$  ]] && run python -m tools.xbe_parser "$XBE" --json "${XBE%.xbe}_analysis.json"   # name is mandatory: <xbe-stem>_analysis.json
# Seeds: functions the static analysis does not see (thread starts, ICALL targets from runtime logs,
# generated with tools.seed_from_log). Passed automatically once the file exists.
SEEDS="$ROOT/config/seed_functions.json"
SEED_ARGS=(); [ -f "$SEEDS" ] && SEED_ARGS=(--seed-functions "$SEEDS")
# The XDK libraries live in their own sections (SSX 3: D3D, D3DX, XGRPH, DSOUND, XPP; Amped 2 likely also
# XONLINE/XNET for Xbox Live). With --text-only alone they are not swept linearly, only reached via calls –
# functions get cut off in the middle (SSX 3: D3D device init → device creation failed, see docs/notes.md).
# Default: every preloaded section whose name does not start with "." (XBE "X" flags are set on .rdata/.data
# too, so they are useless here), from the parse output. DOLBY stays out: DSP code, not x86.
# Check the printed list against the parse output and docs/notes.md.
# Override with XDK_SECTIONS="A,B,C".
ANALYSIS="${XBE%.xbe}_analysis.json"
if [ -z "${XDK_SECTIONS:-}" ] && [ -f "$ANALYSIS" ]; then
  XDK_SECTIONS="$(python - "$ANALYSIS" <<'PY'
import json, sys
secs = json.load(open(sys.argv[1]))["sections"]
print(",".join(s["name"] for s in secs
               if s["executable"] and "PRE" in s["flags"] and not s["name"].startswith((".", "$")) and s["name"] != "DOLBY"))
PY
)"
fi
[[ "$STEP" =~ ^(all|disasm)$ ]] && echo "XDK sections: ${XDK_SECTIONS:-<none>}" && run python -m tools.disasm "$XBE" --text-only ${XDK_SECTIONS:+--extra-sections "$XDK_SECTIONS"} -v "${SEED_ARGS[@]}"
# Write Ghidra names (FidDb/CRT + string labels) into functions.json – must be redone after every disasm,
# because disasm rewrites functions.json completely. Create the export with: scripts/ghidra.sh
GHIDRA_EXPORT="$TOOLKIT/tools/ghidra_naming/export/functions.json"
if [[ "$STEP" == names ]] || { [[ "$STEP" == all ]] && [ -f "$GHIDRA_EXPORT" ]; }; then
  run python tools/ghidra_naming/merge_names.py --apply
fi
[[ "$STEP" =~ ^(all|funcid)$ ]] && run python -m tools.func_id "$XBE" -v
[[ "$STEP" =~ ^(all|abi)$    ]] && run python -m tools.abi_analysis "$XBE" -v   # do NOT skip!
if [[ "$STEP" =~ ^(all|recomp)$ ]]; then
  mkdir -p "$GEN_DIR"
  run python -m tools.recomp "$XBE" --all --split 250 --gen-dir "$GEN_DIR"
fi
echo; echo "Done. Health report: scripts/doctor.sh"

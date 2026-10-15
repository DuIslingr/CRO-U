#!/bin/bash
# Builds CRO-U's local reverse-engineering groundwork in re/ (never committed) from your own copies:
#   tools/re_setup.sh <1.0.0 ChaosRingsOmegaHDeng.app> <3.1.0 ChaosRingsOmegaHDeng.app> <Vita PCSG00499 folder>
# Needs llvm-objdump / llvm-nm and Ghidra (GHIDRA=/opt/ghidra by default). Produces:
#   re/symbols.txt, re/disasm.txt, re/stubs.txt     1.0.0 (has symbols): llvm-nm, llvm-objdump, import stubs named
#   re/ios/      decomp.c, functions.tsv, features.jsonl, ghidra/ (project)            1.0.0
#   re/ios310/   the same for 3.1.0 (stripped) + disasm/symbols/stubs, names_from_100.tsv (1.0.0's names carried over)
#   re/vita/     eboot.elf + imports.tsv (tools/vita_self.py), names_from_100/310.tsv, names_vita.tsv (combined),
#                decomp.c, functions.tsv, ghidra/
# Vita import names come from re/vita/nids_vita3k.tsv (NID <tab> name) when present.
set -e
[ $# -eq 3 ] || { sed -n 2,3p "$0"; exit 1; }
R="$(cd "$(dirname "$0")/.." && pwd)"; T="$R/tools"; G="${GHIDRA:-/opt/ghidra}/support/analyzeHeadless"
A100="$1/ChaosRingsOmegaHDeng"; A310="$2/ChaosRingsOmegaHDeng"; V="$3"
mkdir -p "$R/re/ios/ghidra" "$R/re/ios310/ghidra" "$R/re/vita/ghidra"
dis() { # <binary> <out dir>: symbols, disassembly, import stubs named in it
  llvm-nm -n "$1" > "$2/symbols.txt"
  llvm-objdump -d "$1" > "$2/disasm.txt"
  llvm-objdump --macho --indirect-symbols "$1" | awk '/^Indirect symbols for/ {s=$4} /^0x/ && s ~ /symbolstub/ {print $1, $3}' > "$2/stubs.txt"
  python3 -I "$T/annotate_stubs.py" "$2/stubs.txt" "$2/disasm.txt"
}
echo "disassembly"; dis "$A100" "$R/re"; dis "$A310" "$R/re/ios310"
echo "ghidra: 1.0.0"
"$G" "$R/re/ios/ghidra" croios -import "$A100" -overwrite -scriptPath "$T/ghidra" \
  -postScript ExportDecomp.java "$R/re/ios" -postScript ExportFeatures.java "$R/re/ios/features.jsonl" > "$R/re/ios/ghidra.log" 2>&1
echo "ghidra: 3.1.0"
"$G" "$R/re/ios310/ghidra" croios310 -import "$A310" -overwrite -scriptPath "$T/ghidra" \
  -postScript ExportFeatures.java "$R/re/ios310/features.jsonl" > "$R/re/ios310/ghidra.log" 2>&1
python3 -I "$T/ghidra/match_functions.py" "$R/re/ios/features.jsonl" "$R/re/ios310/features.jsonl" \
  "$R/re/ios310/names_from_100.tsv" > "$R/re/ios310/match100.log"
"$G" "$R/re/ios310/ghidra" croios310 -process ChaosRingsOmegaHDeng -noanalysis -scriptPath "$T/ghidra" \
  -postScript ApplyNames.java "$R/re/ios310/names_from_100.tsv" "1.0.0" -postScript ExportDecomp.java "$R/re/ios310" \
  -postScript ExportFeatures.java "$R/re/ios310/features_named.jsonl" > "$R/re/ios310/apply.log" 2>&1
echo "ghidra: Vita"
NIDS=(); [ -f "$R/re/vita/nids_vita3k.tsv" ] && NIDS=("$R/re/vita/nids_vita3k.tsv")
python3 -I "$T/vita_self.py" "$V/eboot.bin" "$R/re/vita/eboot.elf" "$R/re/vita/imports.tsv" "${NIDS[@]}" > "$R/re/vita/self.log"
"$G" "$R/re/vita/ghidra" crovita -import "$R/re/vita/eboot.elf" -overwrite -scriptPath "$T/ghidra" \
  -preScript VitaSetup.java "$R/re/vita/imports.tsv" > "$R/re/vita/ghidra.log" 2>&1
"$G" "$R/re/vita/ghidra" crovita -process eboot.elf -scriptPath "$T/ghidra" -preScript VitaFindFunctions.java \
  -postScript ExportFeatures.java "$R/re/vita/features.jsonl" > "$R/re/vita/ghidra2.log" 2>&1
python3 -I "$T/ghidra/match_functions.py" "$R/re/ios/features.jsonl" "$R/re/vita/features.jsonl" "$R/re/vita/names_from_100.tsv" --cross > "$R/re/vita/match100.log"
python3 -I "$T/ghidra/match_functions.py" "$R/re/ios310/features_named.jsonl" "$R/re/vita/features.jsonl" "$R/re/vita/names_from_310.tsv" --cross > "$R/re/vita/match310.log"
python3 -I "$T/ghidra/combine_names.py" "$R/re/vita/names_vita.tsv" 1.0.0="$R/re/vita/names_from_100.tsv" 3.1.0="$R/re/vita/names_from_310.tsv"
"$G" "$R/re/vita/ghidra" crovita -process eboot.elf -noanalysis -scriptPath "$T/ghidra" \
  -postScript ApplyNames.java "$R/re/vita/names_vita.tsv" "iPad" -postScript ExportDecomp.java "$R/re/vita" > "$R/re/vita/apply.log" 2>&1
grep -h "^matched" "$R/re/ios310/match100.log" "$R/re/vita/match100.log" "$R/re/vita/match310.log"
echo "done: re/ios, re/ios310, re/vita"

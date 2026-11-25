#!/usr/bin/env bash
# Build the libFuzzer targets (clang, ASan + UBSan) and fuzz each one.
#
#   fuzz/run.sh [SECONDS] [TARGET...]     default: 60 s, every target
#   fuzz/run.sh 0 fuzz_json              only build + replay the corpus
#
# Needs clang-18 with libFuzzer (libclang-rt-18-dev; llvm-18 for symbolized
# stack traces), cmake, ninja,
# nlohmann-json3-dev (Ubuntu 24.04; only the fuzz targets use it, to check
# that the output is JSON). The seed
# corpus fuzz/corpus/<target>/ is copied to build/fuzz/corpus/<target>/,
# which keeps what the fuzzer finds; crashes go to build/fuzz/crashes/.
# Reproduce one: build/fuzz/fuzz_<target> build/fuzz/crashes/<target>/crash-...
set -euo pipefail
cd "$(dirname "$0")/.."
SECONDS_PER_TARGET=${1:-60}
shift || true
OUT=build/fuzz
# Symbolized stack traces (llvm-18's llvm-symbolizer), UBSan's too.
if [ -x /usr/lib/llvm-18/bin/llvm-symbolizer ]; then
  export ASAN_SYMBOLIZER_PATH=/usr/lib/llvm-18/bin/llvm-symbolizer
fi
export UBSAN_OPTIONS=print_stacktrace=1
CXX=${CXX:-clang++-18} cmake -S fuzz -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=None >/dev/null
cmake --build "$OUT" -j "${JOBS:-8}"

targets=("$@")
if [ ${#targets[@]} -eq 0 ]; then
  mapfile -t targets < <(cd "$OUT" && ls fuzz_* | grep -v '\.' || true)
fi
failed=()
for t in "${targets[@]}"; do
  name=${t#fuzz_}
  mkdir -p "$OUT/corpus/$name" "$OUT/crashes/$name"
  [ -d "fuzz/corpus/$name" ] && cp -r --update=none "fuzz/corpus/$name/." "$OUT/corpus/$name/"
  dict=()
  [ -f "fuzz/corpus/$name.dict" ] && dict=(-dict="fuzz/corpus/$name.dict")
  if [ "$SECONDS_PER_TARGET" = 0 ]; then
    args=(-runs=0)
  else
    args=(-max_total_time="$SECONDS_PER_TARGET")
  fi
  echo "== $t (${SECONDS_PER_TARGET} s)"
  if ! "$OUT/fuzz_$name" "${args[@]}" "${dict[@]}" -timeout=10 -rss_limit_mb=2048 \
      -print_final_stats=1 -artifact_prefix="$OUT/crashes/$name/" "$OUT/corpus/$name" \
      2>"$OUT/fuzz_$name.log"; then
    tail -n 60 "$OUT/fuzz_$name.log"
    failed+=("$name")
  else
    grep -E '^stat::(number_of_executed_units|new_units_added)' "$OUT/fuzz_$name.log" || true
  fi
done
if [ ${#failed[@]} -gt 0 ]; then
  echo "FAILED: ${failed[*]} (inputs in $OUT/crashes/)"
  exit 1
fi

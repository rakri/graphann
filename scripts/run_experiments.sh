#!/usr/bin/env bash
# Full SIFT1M experiment runner for multi-probe search analysis.
# Usage: ./scripts/run_experiments.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build"
DATA_DIR="$ROOT/tmp"
SIFT_DIR="$DATA_DIR/sift"
RESULTS_DIR="$ROOT/results"
SIFT_URL="ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz"

BASE_FBIN="$DATA_DIR/sift_base.fbin"
QUERY_FBIN="$DATA_DIR/sift_query.fbin"
GT_IBIN="$DATA_DIR/sift_gt.ibin"

mkdir -p "$BUILD_DIR" "$DATA_DIR" "$RESULTS_DIR"

echo "=== Step 1: Building the project ==="
pushd "$BUILD_DIR" > /dev/null
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
popd > /dev/null

echo "=== Step 2: Downloading SIFT1M ==="
if [ -f "$SIFT_DIR/sift_base.fvecs" ]; then
    echo "Already downloaded."
else
    curl -o "$DATA_DIR/sift.tar.gz" "$SIFT_URL"
    tar -xzf "$DATA_DIR/sift.tar.gz" -C "$DATA_DIR"
    rm -f "$DATA_DIR/sift.tar.gz"
fi

echo "=== Step 3: Converting to fbin/ibin ==="
if [ -f "$BASE_FBIN" ]; then
    echo "Already converted."
else
    python3 "$ROOT/scripts/convert_vecs.py" "$SIFT_DIR/sift_base.fvecs"        "$BASE_FBIN"
    python3 "$ROOT/scripts/convert_vecs.py" "$SIFT_DIR/sift_query.fvecs"       "$QUERY_FBIN"
    python3 "$ROOT/scripts/convert_vecs.py" "$SIFT_DIR/sift_groundtruth.ivecs" "$GT_IBIN"
fi

echo "=== Step 4: Building indices ==="
for PARAMS in "R32_L75_a1.2:32:75:1.2" "R64_L125_a1.2:64:125:1.2" "R32_L75_a1.4:32:75:1.4"; do
    IFS=':' read -r NAME R L ALPHA <<< "$PARAMS"
    IDX="$DATA_DIR/sift_index_${NAME}.bin"
    if [ -f "$IDX" ]; then
        echo "Index $NAME already exists."
    else
        echo "--- Building $NAME ---"
        "$BUILD_DIR/build_index" --data "$BASE_FBIN" --output "$IDX" \
            --R "$R" --L "$L" --alpha "$ALPHA" --gamma 1.5
    fi
done

echo "=== Step 5: Running experiments ==="
L_VALUES="10,20,30,50,75,100,150,200"

run_exp() {
    local NAME="$1" IDX="$2" PROBES="$3" MODE="$4"
    echo "--- $NAME ---"
    "$BUILD_DIR/search_index" \
        --index "$IDX" --data "$BASE_FBIN" \
        --queries "$QUERY_FBIN" --gt "$GT_IBIN" \
        --K 10 --L "$L_VALUES" --probes "$PROBES" --mode "$MODE" \
        2>&1 | tee "$RESULTS_DIR/${NAME}.txt"
}

run_exp "exp1_baseline_default"           "$DATA_DIR/sift_index_R32_L75_a1.2.bin" "1"       "shared"
run_exp "exp2_multiprobe_shared_default"  "$DATA_DIR/sift_index_R32_L75_a1.2.bin" "1,2,3,5" "shared"
run_exp "exp3_multiprobe_split_default"   "$DATA_DIR/sift_index_R32_L75_a1.2.bin" "1,3,5"   "split"
run_exp "exp4_baseline_R64"              "$DATA_DIR/sift_index_R64_L125_a1.2.bin" "1"       "shared"
run_exp "exp5_multiprobe_shared_R64"     "$DATA_DIR/sift_index_R64_L125_a1.2.bin" "1,2,3,5" "shared"
run_exp "exp6_baseline_alpha14"          "$DATA_DIR/sift_index_R32_L75_a1.4.bin"  "1"       "shared"
run_exp "exp7_multiprobe_shared_alpha14" "$DATA_DIR/sift_index_R32_L75_a1.4.bin"  "1,2,3,5" "shared"

echo ""
echo "=== ALL EXPERIMENTS COMPLETE ==="
echo "Results saved in: $RESULTS_DIR/"
ls -la "$RESULTS_DIR/"

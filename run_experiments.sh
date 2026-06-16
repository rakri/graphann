#!/usr/bin/env bash
#
# ============================================================================
# COMPLETE SETUP & EXPERIMENT SCRIPT FOR MULTI-PROBE SEARCH ON SIFT1M
# ============================================================================
#
# PREREQUISITES:
#   - Linux or macOS with a C++17 compiler (g++ >= 7 or clang >= 10)
#   - OpenMP support (comes with g++ by default)
#   - cmake (>= 3.14)
#   - python3 with numpy
#   - curl and tar
#   - ~2GB free disk space
#
# HOW TO USE:
#   1. Save this file as run_experiments.sh in your graphann/ directory
#   2. Make it executable: chmod +x run_experiments.sh
#   3. Run it: ./run_experiments.sh
#
#   It will do everything automatically and print results at the end.
#   The whole thing takes ~10-20 minutes depending on your machine.
#
# ============================================================================
set -euo pipefail

require_cmd() {
    local cmd="$1"
    local hint="$2"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "ERROR: Required command '$cmd' not found."
        echo "Hint: $hint"
        exit 1
    fi
}

# Git Bash on Windows may not inherit updated PATH until restart.
if ! command -v cmake >/dev/null 2>&1 && [ -x "/c/Program Files/CMake/bin/cmake.exe" ]; then
    export PATH="/c/Program Files/CMake/bin:$PATH"
fi

if command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN="python3"
elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN="python"
else
    echo "ERROR: python3/python not found in PATH."
    echo "Hint: install Python 3 and ensure it is available in your shell PATH."
    exit 1
fi

require_cmd cmake "Install CMake and ensure it is in PATH. If installed on Windows, restart Git Bash."
require_cmd curl "Install curl and ensure it is in PATH."
require_cmd tar "Install tar and ensure it is in PATH."

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT/build"
DATA_DIR="$ROOT/tmp"
SIFT_DIR="$DATA_DIR/sift"
RESULTS_DIR="$ROOT/results"

# SIFT1M download URL
SIFT_URL="ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz"

# Output files
BASE_FBIN="$DATA_DIR/sift_base.fbin"
QUERY_FBIN="$DATA_DIR/sift_query.fbin"
GT_IBIN="$DATA_DIR/sift_gt.ibin"

mkdir -p "$BUILD_DIR" "$DATA_DIR" "$RESULTS_DIR"

echo "============================================================"
echo "  STEP 1: BUILD THE PROJECT"
echo "============================================================"
pushd "$BUILD_DIR" > /dev/null

# On Git Bash + Windows, explicitly choose NMake + cl to avoid generator/toolchain detection issues.
if [[ "${OSTYPE:-}" == msys* || "${OSTYPE:-}" == cygwin* || "${OSTYPE:-}" == win32* ]]; then
    cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
else
    cmake .. -DCMAKE_BUILD_TYPE=Release
fi

cmake --build . --config Release --parallel
popd > /dev/null
echo ""

echo "============================================================"
echo "  STEP 2: DOWNLOAD SIFT1M"
echo "============================================================"
if [ -f "$SIFT_DIR/sift_base.fvecs" ] && \
   [ -f "$SIFT_DIR/sift_query.fvecs" ] && \
   [ -f "$SIFT_DIR/sift_groundtruth.ivecs" ]; then
    echo "SIFT1M already downloaded."
else
    echo "Downloading SIFT1M (~160MB)..."
    curl -o "$DATA_DIR/sift.tar.gz" "$SIFT_URL"
    echo "Extracting..."
    tar -xzf "$DATA_DIR/sift.tar.gz" -C "$DATA_DIR"
    rm -f "$DATA_DIR/sift.tar.gz"
fi
echo ""

echo "============================================================"
echo "  STEP 3: CONVERT TO FBIN/IBIN FORMAT"
echo "============================================================"
if [ -f "$BASE_FBIN" ] && [ -f "$QUERY_FBIN" ] && [ -f "$GT_IBIN" ]; then
    echo "Binary files already exist."
else
    "$PYTHON_BIN" "$ROOT/scripts/convert_vecs.py" "$SIFT_DIR/sift_base.fvecs"        "$BASE_FBIN"
    "$PYTHON_BIN" "$ROOT/scripts/convert_vecs.py" "$SIFT_DIR/sift_query.fvecs"       "$QUERY_FBIN"
    "$PYTHON_BIN" "$ROOT/scripts/convert_vecs.py" "$SIFT_DIR/sift_groundtruth.ivecs" "$GT_IBIN"
fi
echo ""

echo "============================================================"
echo "  STEP 4: BUILD INDICES"
echo "============================================================"
echo ""

# Index 1: Default parameters (what everyone else is using)
IDX_DEFAULT="$DATA_DIR/sift_index_R32_L75_a1.2.bin"
if [ -f "$IDX_DEFAULT" ]; then
    echo "Default index already exists."
else
    echo "--- Building index: R=32, L=75, alpha=1.2, gamma=1.5 ---"
    "$BUILD_DIR/build_index" \
        --data "$BASE_FBIN" \
        --output "$IDX_DEFAULT" \
        --R 32 --L 75 --alpha 1.2 --gamma 1.5
fi
echo ""

# Index 2: Higher R and L for better quality
IDX_R64="$DATA_DIR/sift_index_R64_L125_a1.2.bin"
if [ -f "$IDX_R64" ]; then
    echo "R=64 index already exists."
else
    echo "--- Building index: R=64, L=125, alpha=1.2, gamma=1.5 ---"
    "$BUILD_DIR/build_index" \
        --data "$BASE_FBIN" \
        --output "$IDX_R64" \
        --R 64 --L 125 --alpha 1.2 --gamma 1.5
fi
echo ""

# Index 3: Higher alpha (more long-range edges, key insight from paper)
IDX_A14="$DATA_DIR/sift_index_R32_L75_a1.4.bin"
if [ -f "$IDX_A14" ]; then
    echo "Alpha=1.4 index already exists."
else
    echo "--- Building index: R=32, L=75, alpha=1.4, gamma=1.5 ---"
    "$BUILD_DIR/build_index" \
        --data "$BASE_FBIN" \
        --output "$IDX_A14" \
        --R 32 --L 75 --alpha 1.4 --gamma 1.5
fi
echo ""

echo "============================================================"
echo "  STEP 5: RUN ALL EXPERIMENTS"
echo "============================================================"
echo ""

L_VALUES="10,20,30,50,75,100,150,200"

# ---- Experiment 1: Baseline search on default index ----
echo "=== EXP 1: Baseline (R=32, L=75, alpha=1.2) ==="
"$BUILD_DIR/search_index" \
    --index "$IDX_DEFAULT" \
    --data "$BASE_FBIN" \
    --queries "$QUERY_FBIN" \
    --gt "$GT_IBIN" \
    --K 10 \
    --L "$L_VALUES" \
    --probes 1 \
    --mode shared \
    2>&1 | tee "$RESULTS_DIR/exp1_baseline_default.txt"
echo ""

# ---- Experiment 2: Multi-probe SHARED on default index ----
echo "=== EXP 2: Multi-probe SHARED (R=32, L=75, alpha=1.2) ==="
"$BUILD_DIR/search_index" \
    --index "$IDX_DEFAULT" \
    --data "$BASE_FBIN" \
    --queries "$QUERY_FBIN" \
    --gt "$GT_IBIN" \
    --K 10 \
    --L "$L_VALUES" \
    --probes 1,2,3,5 \
    --mode shared \
    2>&1 | tee "$RESULTS_DIR/exp2_multiprobe_shared_default.txt"
echo ""

# ---- Experiment 3: Multi-probe SPLIT on default index (to show it hurts) ----
echo "=== EXP 3: Multi-probe SPLIT (R=32, L=75, alpha=1.2) ==="
"$BUILD_DIR/search_index" \
    --index "$IDX_DEFAULT" \
    --data "$BASE_FBIN" \
    --queries "$QUERY_FBIN" \
    --gt "$GT_IBIN" \
    --K 10 \
    --L "$L_VALUES" \
    --probes 1,3,5 \
    --mode split \
    2>&1 | tee "$RESULTS_DIR/exp3_multiprobe_split_default.txt"
echo ""

# ---- Experiment 4: Baseline on R=64 index (stronger baseline) ----
echo "=== EXP 4: Baseline (R=64, L=125, alpha=1.2) ==="
"$BUILD_DIR/search_index" \
    --index "$IDX_R64" \
    --data "$BASE_FBIN" \
    --queries "$QUERY_FBIN" \
    --gt "$GT_IBIN" \
    --K 10 \
    --L "$L_VALUES" \
    --probes 1 \
    --mode shared \
    2>&1 | tee "$RESULTS_DIR/exp4_baseline_R64.txt"
echo ""

# ---- Experiment 5: Multi-probe SHARED on R=64 index ----
echo "=== EXP 5: Multi-probe SHARED (R=64, L=125, alpha=1.2) ==="
"$BUILD_DIR/search_index" \
    --index "$IDX_R64" \
    --data "$BASE_FBIN" \
    --queries "$QUERY_FBIN" \
    --gt "$GT_IBIN" \
    --K 10 \
    --L "$L_VALUES" \
    --probes 1,2,3,5 \
    --mode shared \
    2>&1 | tee "$RESULTS_DIR/exp5_multiprobe_shared_R64.txt"
echo ""

# ---- Experiment 6: Baseline on alpha=1.4 index ----
echo "=== EXP 6: Baseline (R=32, L=75, alpha=1.4) ==="
"$BUILD_DIR/search_index" \
    --index "$IDX_A14" \
    --data "$BASE_FBIN" \
    --queries "$QUERY_FBIN" \
    --gt "$GT_IBIN" \
    --K 10 \
    --L "$L_VALUES" \
    --probes 1 \
    --mode shared \
    2>&1 | tee "$RESULTS_DIR/exp6_baseline_alpha14.txt"
echo ""

# ---- Experiment 7: Multi-probe SHARED on alpha=1.4 index ----
echo "=== EXP 7: Multi-probe SHARED (R=32, L=75, alpha=1.4) ==="
"$BUILD_DIR/search_index" \
    --index "$IDX_A14" \
    --data "$BASE_FBIN" \
    --queries "$QUERY_FBIN" \
    --gt "$GT_IBIN" \
    --K 10 \
    --L "$L_VALUES" \
    --probes 1,2,3,5 \
    --mode shared \
    2>&1 | tee "$RESULTS_DIR/exp7_multiprobe_shared_alpha14.txt"
echo ""

echo "============================================================"
echo "  ALL EXPERIMENTS COMPLETE!"
echo "  Results saved in: $RESULTS_DIR/"
echo "============================================================"
echo ""
echo "Files generated:"
ls -la "$RESULTS_DIR/"
echo ""
echo "NEXT STEP: Copy-paste the contents of ALL files in results/"
echo "back to Claude, and I'll generate plots and analysis."

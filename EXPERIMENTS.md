# Multi-Probe Search: Experimental Analysis

## What This Is

An investigation into whether the fixed entry point in Vamana search is a performance bottleneck. Three multi-probe search variants were implemented, tested on synthetic datasets, and validated on SIFT1M.

**Finding:** Multi-probe improves recall (up to +15% on SIFT1M at L=10) but is not cost-effective — the same recall costs 3.6× fewer distance computations by simply increasing L. The graph structure, not the entry point, is the dominant factor in recall quality.

## What Changed From the Base Repo

**3 files modified** (everything else is untouched):

| File | Lines Added | What |
|------|------------|------|
| `include/vamana_index.h` | ~30 | 3 new search method declarations + 2 internal helper declarations |
| `src/vamana_index.cpp` | ~200 | Implementations: `greedy_search_from()`, `greedy_search_inject()`, `search_multiprobe_split()`, `search_multiprobe_full()`, `search_multiprobe_shared()` |
| `src/search_index.cpp` | ~15 | Added `--probes` and `--mode` CLI flags |

**New files added:**

| File | Purpose |
|------|---------|
| `scripts/run_experiments.sh` | Automated: download SIFT1M → build indices → run all experiments |
| `scripts/generate_test_data.py` | Generate synthetic uniform + clustered test datasets |
| `results/` | Raw experiment output logs |
| `docs/` | Final report and plots |
| `EXPERIMENTS.md` | This file |

## How to Reproduce

### Prerequisites

- C++17 compiler with OpenMP (GCC ≥ 7 or Clang ≥ 10)
- CMake ≥ 3.14
- Python 3 with NumPy
- `curl` and `tar` (for SIFT1M download)
- ~2GB free disk space

### One-Command Run

```bash
chmod +x scripts/run_experiments.sh
./scripts/run_experiments.sh
```

This will:
1. Build the project with Release optimizations
2. Download SIFT1M (~160MB) from IRISA
3. Convert `.fvecs`/`.ivecs` to `.fbin`/`.ibin`
4. Build 3 indices: Default (R=32, α=1.2), Strong (R=64, α=1.2), Long-range (R=32, α=1.4)
5. Run 7 experiments across all (index × probes × mode) combinations
6. Save results to `results/`

Total time: ~15-20 minutes on a modern multi-core machine.

### Manual Run

```bash
# Build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ..

# Build an index
./build/build_index \
  --data tmp/sift_base.fbin \
  --output tmp/sift_index.bin \
  --R 32 --L 75 --alpha 1.2 --gamma 1.5

# Search with multi-probe
./build/search_index \
  --index tmp/sift_index.bin \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --K 10 \
  --L 10,20,30,50,75,100,150,200 \
  --probes 1,2,3,5 \
  --mode shared
```

### CLI Flags Added

| Flag | Values | Default | Description |
|------|--------|---------|-------------|
| `--probes` | Comma-separated ints | `1` | Number of entry points per query |
| `--mode` | `split`, `full`, `shared` | `shared` | Multi-probe variant to use |

## The Three Variants

**V1 — Split Budget (`--mode split`):** k independent searches, each with L/k search list. Same total compute. **Result: degrades recall** — each probe too shallow.

**V2 — Full Budget (`--mode full`):** k independent searches, each with full L. k× more compute. Tests whether diverse starts help at all.

**V3 — Shared State (`--mode shared`):** k probes share one candidate set and visited array. Each probe injects a new random entry point into the ongoing search. No node evaluated twice. **This is the only viable variant.**

## Key Results (SIFT1M)

### Default Index (R=32, α=1.2)

| Probes | L=10 Recall | L=10 Dist Cmps | L=100 Recall | L=100 Dist Cmps |
|--------|-------------|----------------|--------------|-----------------|
| 1 | 0.778 | 643 | 0.988 | 2,436 |
| 2 | 0.836 | 1,280 | 0.988 | 4,870 |
| 3 | 0.863 | 1,917 | 0.989 | 7,304 |
| 5 | 0.894 | 3,189 | 0.989 | 12,169 |

### The Cost-Effectiveness Problem

| Configuration | Recall@10 | Dist Cmps | Verdict |
|--------------|-----------|-----------|---------|
| 1 probe, L=20 | 0.893 | 883 | ← **cheapest for this recall** |
| 5 probes, L=10 | 0.894 | 3,189 | 3.6× more expensive |
| 1 probe, L=30 | 0.933 | 1,100 | higher recall, still cheaper |

### Graph Quality Dominates

| Index | 1 probe, L=10 | Dist Cmps | vs Default |
|-------|---------------|-----------|------------|
| Default (R=32) | 0.778 | 643 | baseline |
| R=64 | 0.866 | 988 | +8.8% absolute, 1.5× cost |
| 5-probe Default | 0.894 | 3,189 | +11.6% absolute, 5× cost |

R=64 gives comparable improvement at **3× less cost** than multi-probe.

## Report

See `docs/final_report.pdf` for the full narrative analysis with figures.

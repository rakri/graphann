#!/usr/bin/env python3
"""Generate synthetic uniform + clustered datasets for testing."""
import numpy as np, os, sys

def write_fbin(path, data):
    npts, dims = data.shape
    with open(path, 'wb') as f:
        np.array([npts, dims], dtype=np.uint32).tofile(f)
        data.astype(np.float32).tofile(f)
    print(f"  {path}: {npts}x{dims}, {os.path.getsize(path)/(1024*1024):.1f}MB")

def write_ibin(path, data):
    npts, dims = data.shape
    with open(path, 'wb') as f:
        np.array([npts, dims], dtype=np.uint32).tofile(f)
        data.astype(np.uint32).tofile(f)
    print(f"  {path}: {npts}x{dims}")

def compute_gt(base, queries, k=100):
    nq = queries.shape[0]
    gt = np.zeros((nq, k), dtype=np.uint32)
    for i in range(nq):
        dists = np.sum((base - queries[i])**2, axis=1)
        gt[i] = np.argpartition(dists, k)[:k]
        gt[i] = gt[i][np.argsort(dists[gt[i]])]
        if (i+1) % 100 == 0: print(f"\r  GT: {i+1}/{nq}", end="", flush=True)
    print()
    return gt

def generate(name, base, nq, k, out_dir, seed=123):
    rng = np.random.RandomState(seed)
    idx = rng.choice(base.shape[0], nq, replace=False)
    queries = base[idx] + rng.randn(nq, base.shape[1]).astype(np.float32) * 0.1
    print(f"\n=== {name}: {base.shape[0]}x{base.shape[1]}, {nq} queries ===")
    os.makedirs(out_dir, exist_ok=True)
    write_fbin(os.path.join(out_dir, f"{name}_base.fbin"), base)
    write_fbin(os.path.join(out_dir, f"{name}_query.fbin"), queries)
    write_ibin(os.path.join(out_dir, f"{name}_gt.ibin"), compute_gt(base, queries, k))

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else "tmp"
    rng = np.random.RandomState(42)
    N, D = 100_000, 128

    print("Generating uniform dataset...")
    generate("uniform", rng.randn(N, D).astype(np.float32), 1000, 100, out, 100)

    print("Generating clustered dataset...")
    centers = rng.randn(10, D).astype(np.float32) * 10
    clustered = np.vstack([centers[c] + rng.randn(N//10, D).astype(np.float32)*0.5 for c in range(10)])
    generate("clustered", clustered, 1000, 100, out, 200)

    print("\nDone!")

#!/usr/bin/env python3
"""Convert fvecs/ivecs files to fbin/ibin format."""

import numpy as np
import sys
import os

def read_fvecs(path):
    with open(path, 'rb') as f:
        dim = np.fromfile(f, dtype=np.int32, count=1)[0]
        vec_size = 4 + dim * 4
        f.seek(0, 2)
        file_size = f.tell()
        npts = file_size // vec_size
        assert npts * vec_size == file_size
        f.seek(0)
        raw = np.fromfile(f, dtype=np.float32).reshape(npts, dim + 1)
        data = raw[:, 1:]
    return npts, dim, data

def read_ivecs(path):
    with open(path, 'rb') as f:
        dim = np.fromfile(f, dtype=np.int32, count=1)[0]
        vec_size = 4 + dim * 4
        f.seek(0, 2)
        file_size = f.tell()
        npts = file_size // vec_size
        assert npts * vec_size == file_size
        f.seek(0)
        raw = np.fromfile(f, dtype=np.int32).reshape(npts, dim + 1)
        data = raw[:, 1:]
    return npts, dim, data

def write_fbin(path, npts, dims, data):
    with open(path, 'wb') as f:
        np.array([npts, dims], dtype=np.uint32).tofile(f)
        data.astype(np.float32).tofile(f)
    print(f"  Written {path}: {npts} points x {dims} dims, "
          f"{os.path.getsize(path)/(1024*1024):.1f} MB")

def write_ibin(path, npts, dims, data):
    with open(path, 'wb') as f:
        np.array([npts, dims], dtype=np.uint32).tofile(f)
        data.astype(np.uint32).tofile(f)
    print(f"  Written {path}: {npts} points x {dims} dims, "
          f"{os.path.getsize(path)/(1024*1024):.1f} MB")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <input.fvecs|ivecs> <output.fbin|ibin>")
        sys.exit(1)

    inp, out = sys.argv[1], sys.argv[2]

    if inp.endswith('.fvecs'):
        npts, dims, data = read_fvecs(inp)
        write_fbin(out, npts, dims, data)
    elif inp.endswith('.ivecs'):
        npts, dims, data = read_ivecs(inp)
        write_ibin(out, npts, dims, data)
    else:
        print(f"Unknown input format: {inp}")
        sys.exit(1)

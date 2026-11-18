"""Just enough of the safetensors format to read headers and map tensors.

Tensors are mapped read-only from the file, so loading a model this way can
never change the file, and the operating system only pages in what is used.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import json
import struct

from ..common import Corrupt

HEADER_MAX = 100 * 1024 * 1024
DTYPES = {"BF16": 2, "F16": 2, "F32": 4, "I64": 8, "I32": 4, "U8": 1, "I8": 1, "BOOL": 1}


def read_header(path):
    with open(path, "rb") as f:
        raw = f.read(8)
        if len(raw) != 8:
            raise Corrupt("not a safetensors file")
        n = struct.unpack("<Q", raw)[0]
        if n < 2 or n > HEADER_MAX:
            raise Corrupt("safetensors header size")
        h = json.loads(f.read(n))
    h.pop("__metadata__", None)
    start = 8 + n
    for name, t in h.items():
        b, e = t["data_offsets"]
        count = 1
        for d in t["shape"]:
            count *= d
        if t["dtype"] not in DTYPES or e - b != count * DTYPES[t["dtype"]]:
            raise Corrupt("tensor %s has an unexpected size" % name)
        t["abs_offset"] = start + b
        t["nbytes"] = e - b
    return h, start


def map_tensor(path, info):
    """A read-only, file-backed torch tensor for one entry of the header."""
    import numpy as np
    import torch
    np_dtype = {"BF16": np.uint16, "F16": np.float16, "F32": np.float32, "I64": np.int64, "I32": np.int32,
                "U8": np.uint8, "I8": np.int8, "BOOL": np.bool_}[info["dtype"]]
    shape = tuple(info["shape"]) or (1,)
    arr = np.memmap(path, dtype=np_dtype, mode="r", offset=info["abs_offset"], shape=shape)
    import warnings
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")  # torch warns that the mapping is read-only, which is the point
        t = torch.from_numpy(arr)
    if info["dtype"] == "BF16":
        t = t.view(torch.bfloat16)
    if not info["shape"]:
        t = t.reshape(())
    return t


def read_tensor(path, info):
    """Copy one tensor into memory (float32)."""
    return map_tensor(path, info).float().clone()

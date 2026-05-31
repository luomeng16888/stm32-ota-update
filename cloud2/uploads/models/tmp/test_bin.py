"""
gen_models.py - 生成10个测试模型文件（差分 10%~100%）

用法：python gen_models.py

生成结果（当前目录）：
  model_base.bin     基准 (512KB)
  model_diff10.bin   10% 不同
  model_diff20.bin   20% 不同
  ...
  model_diff100.bin  100% 不同
"""

import os
import struct
import random
import hashlib

SIZE = 512 * 1024  # 512KB


def gen_base(size, seed=42):
    rng = random.Random(seed)
    data = bytearray(size)
    # 头部
    data[0:4] = b'NEUR'
    struct.pack_into('<I', data, 4, 1)
    struct.pack_into('<I', data, 8, size)
    # 权重（可复现伪随机）
    for i in range(64, size):
        data[i] = rng.randint(0, 255)
    # 校验和
    ck = sum(data[64:]) & 0xFFFFFFFF
    struct.pack_into('<I', data, 20, ck)
    return bytes(data)


def gen_diff(base, pct, seed=100):
    if pct <= 0:
        return base
    data = bytearray(base)
    rng = random.Random(seed)
    n = int((len(base) - 64) * pct / 100)
    positions = set()
    while len(positions) < n:
        positions.add(rng.randint(64, len(base) - 1))
    for p in positions:
        v = rng.randint(0, 255)
        while v == data[p]:
            v = rng.randint(0, 255)
        data[p] = v
    ck = sum(data[64:]) & 0xFFFFFFFF
    struct.pack_into('<I', data, 20, ck)
    return bytes(data)


def main():
    print(f"Size: {SIZE // 1024} KB each\n")

    base = gen_base(SIZE)
    md5 = hashlib.md5(base).hexdigest()[:16]
    with open("model_base.bin", "wb") as f:
        f.write(base)
    print(f"  model_base.bin     {len(base):>8d} B   0% diff   md5={md5}")

    for pct in range(10, 110, 10):
        data = gen_diff(base, pct)
        name = f"model_diff{pct}.bin"
        md5 = hashlib.md5(data).hexdigest()[:16]
        with open(name, "wb") as f:
            f.write(data)
        print(f"  {name:20s} {len(data):>8d} B  {pct:>3}% diff   md5={md5}")

    print(f"\nDone: 11 files in {os.path.abspath('.')}")


if __name__ == '__main__':
    main()
